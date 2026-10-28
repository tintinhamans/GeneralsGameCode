/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "OpenALAudioDevice/OpenALAudioProducer.h"
#include "VideoDevice/FFmpeg/FFmpegAudioFile.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <cfenv>

namespace { constexpr size_t MaxFrames = 65536; }

struct OpenALAudioProducer::Work
{
    FFmpegAudioFile *decoder = nullptr;
    std::shared_ptr<OpenALPcmStream::Producer> output;
    int channels = 0;
    bool floatPcm = false;
    bool cancelled = false;
    bool inFlight = false;
    bool eof = false;
    std::fenv_t environment;
    std::atomic<bool> failed{false};
    std::atomic<uint64_t> produced{0};
    uint64_t decoded = 0;
    std::vector<short> pcm;
    std::vector<float> floats;
    size_t offset = 0;
    std::array<Packet, PacketCapacity> packets;
    std::atomic<uint64_t> packetWrite{0};
    std::atomic<uint64_t> packetRead{0};

    bool settled() const
    {
        return failed.load() || output->availableFrames() == 0 ||
            packetWrite.load() - packetRead.load() == PacketCapacity ||
            (eof && pcm.empty() && floats.empty());
    }
    bool step()
    {
        if (failed.load())
            return false;
        size_t samples = pcm.size();
        if (floatPcm)
            samples = floats.size();
        if (offset < samples) {
            const auto frames = static_cast<uint32_t>(std::min<size_t>(output->availableFrames(), (samples - offset) / channels));
            if (frames == 0)
                return false;
            OpenALPcmStream::QueueResult result;
            if (floatPcm)
                result = output->queueFloat(floats.data() + offset, frames);
            else
                result = output->queue(pcm.data() + offset, frames);
            if (result == OpenALPcmStream::QueueResult::Full)
                return false;
            if (result != OpenALPcmStream::QueueResult::Accepted) {
                failed.store(true);
                return false;
            }
            produced.fetch_add(frames);
            offset += static_cast<size_t>(frames) * channels;
            if (offset == samples) {
                pcm.clear();
                floats.clear();
                offset = 0;
            }
            return true;
        }
        const uint64_t written = packetWrite.load(std::memory_order_relaxed);
        if (eof || written - packetRead.load(std::memory_order_acquire) == PacketCapacity || output->availableFrames() == 0)
            return false;
        if (std::fesetenv(&environment) != 0) {
            failed.store(true);
            return false;
        }
        const bool decodedPacket = decoder->decodePacket();
        if (decoder->hasError() || failed.load()) {
            failed.store(true);
            return false;
        }
        samples = pcm.size();
        if (floatPcm)
            samples = floats.size();
        decoded += samples / channels;
        eof = !decodedPacket && samples == 0;
        packets[written % PacketCapacity] = {decoded, eof};
        packetWrite.store(written + 1, std::memory_order_release);
        return true;
    }
};

struct OpenALAudioProducer::State
{
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::shared_ptr<Work>> jobs;
    bool stopping = false;
    std::thread thread;
    State() : thread([this] { run(); }) {}
    ~State()
    {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        changed.notify_all();
        thread.join();
    }
    void run()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stopping) {
            if (jobs.empty()) {
                changed.wait(lock, [this] { return stopping || !jobs.empty(); });
                if (stopping)
                    break;
            }
            bool progress = false;
            for (size_t i = 0; i < jobs.size() && !stopping; ++i) {
                auto work = jobs[i];
                if (work->cancelled)
                    continue;
                work->inFlight = true;
                lock.unlock();
                try { progress = work->step() || progress; }
                catch (...) { work->failed.store(true); }
                lock.lock();
                work->inFlight = false;
                changed.notify_all();
            }
            if (!progress)
                changed.wait_for(lock, std::chrono::milliseconds(1));
        }
    }
};

OpenALAudioProducer::Job::Job(std::shared_ptr<State> service, std::shared_ptr<Work> work,
    std::unique_ptr<FFmpegAudioFile> decoder)
    : m_service(std::move(service)), m_work(std::move(work)), m_decoder(std::move(decoder)) {}
OpenALAudioProducer::Job::~Job()
{
    std::unique_lock<std::mutex> lock(m_service->mutex);
    m_work->cancelled = true;
    m_service->changed.wait(lock, [this] { return !m_work->inFlight; });
    auto &jobs = m_service->jobs;
    jobs.erase(std::remove(jobs.begin(), jobs.end(), m_work), jobs.end());
}
bool OpenALAudioProducer::Job::failed() const { return m_work->failed.load(); }
uint64_t OpenALAudioProducer::Job::producedFrames() const { return m_work->produced.load(); }
bool OpenALAudioProducer::Job::peekPacket(Packet &packet) const
{
    const auto read = m_work->packetRead.load(std::memory_order_relaxed);
    if (read == m_work->packetWrite.load(std::memory_order_acquire))
        return false;
    packet = m_work->packets[read % PacketCapacity];
    return true;
}
uint64_t OpenALAudioProducer::Job::pendingPackets() const
{
    return m_work->packetWrite.load(std::memory_order_acquire) -
        m_work->packetRead.load(std::memory_order_relaxed);
}
void OpenALAudioProducer::Job::consumePacket()
{
    m_work->packetRead.fetch_add(1, std::memory_order_release);
    m_service->changed.notify_all();
}
void OpenALAudioProducer::Job::prime()
{
    std::unique_lock<std::mutex> lock(m_service->mutex);
    m_service->changed.wait(lock, [this] { return !m_work->inFlight && m_work->settled(); });
}
OpenALAudioProducer::OpenALAudioProducer() : m_state(std::make_shared<State>()) {}
OpenALAudioProducer::~OpenALAudioProducer() = default;
std::unique_ptr<OpenALAudioProducer::Job> OpenALAudioProducer::add(std::unique_ptr<FFmpegAudioFile> decoder,
    std::shared_ptr<OpenALPcmStream::Producer> output, int channels, bool floatPcm)
{
    if (!decoder || !output || channels <= 0)
        return {};
    auto work = std::make_shared<Work>();
    if (std::fegetenv(&work->environment) != 0)
        return {};
    work->decoder = decoder.get();
    work->output = std::move(output);
    work->channels = channels;
    work->floatPcm = floatPcm;
    std::unique_ptr<Job> job(new Job(m_state, work, std::move(decoder)));
    if (floatPcm) {
        work->floats.reserve(MaxFrames * channels);
        if (!work->decoder->setFloatPcmCallback([raw = work.get()](const float *pcm, int frames) {
            const size_t count = static_cast<size_t>(frames) * raw->channels;
            if (count > MaxFrames * raw->channels - raw->floats.size()) {
                raw->failed.store(true);
                return;
            }
            raw->floats.insert(raw->floats.end(), pcm, pcm + count);
        }))
            return {};
    } else {
        work->pcm.reserve(MaxFrames * channels);
        work->decoder->setPcmCallback([raw = work.get()](const short *pcm, int frames) {
            const size_t count = static_cast<size_t>(frames) * raw->channels;
            if (count > MaxFrames * raw->channels - raw->pcm.size()) {
                raw->failed.store(true);
                return;
            }
            raw->pcm.insert(raw->pcm.end(), pcm, pcm + count);
        });
    }
    work->decoder->setErrorLogging(false);
    { std::lock_guard<std::mutex> lock(m_state->mutex); m_state->jobs.push_back(work); }
    m_state->changed.notify_all();
    return job;
}
void OpenALAudioProducer::waitForIdle()
{
    std::unique_lock<std::mutex> lock(m_state->mutex);
    m_state->changed.notify_all();
    m_state->changed.wait(lock, [this] {
        for (const auto &work : m_state->jobs) {
            if (work->inFlight || !work->settled())
                return false;
        }
        return true;
    });
}
