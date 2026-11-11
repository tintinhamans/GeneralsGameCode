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

namespace
{
constexpr size_t MaxFrames = 65536;
}

struct OpenALAudioProducer::Work
{
	FFmpegAudioFile *decoder = nullptr;
	std::shared_ptr<OpenALPcmStream::Producer> output;
	int channels = 0;
	bool cancelled = false;
	bool inFlight = false;
	bool eof = false;
	std::atomic<bool> failed{false};
	std::atomic<uint64_t> produced{0};
	uint64_t decoded = 0;
	std::vector<short> pcm;
	size_t offset = 0;
	std::array<Packet, PacketCapacity> packets;
	std::atomic<uint64_t> packetWrite{0};
	std::atomic<uint64_t> packetRead{0};

	bool settled() const
	{
		return failed.load() || output->availableFrames() == 0 ||
			packetWrite.load() - packetRead.load() == PacketCapacity ||
			(eof && pcm.empty());
	}
	bool step()
	{
		if (failed.load())
			return false;
		if (offset < pcm.size())
		{
			const auto frames = static_cast<uint32_t>(std::min<size_t>(output->availableFrames(), (pcm.size() - offset) / channels));
			if (frames == 0)
				return false;
			const OpenALPcmStream::QueueResult result = output->queue(pcm.data() + offset, frames);
			if (result == OpenALPcmStream::QueueResult::Full)
				return false;
			if (result != OpenALPcmStream::QueueResult::Accepted)
			{
				failed.store(true);
				return false;
			}
			produced.fetch_add(frames);
			offset += static_cast<size_t>(frames) * channels;
			if (offset == pcm.size())
			{
				pcm.clear();
				offset = 0;
			}
			return true;
		}
		const uint64_t written = packetWrite.load(std::memory_order_relaxed);
		if (eof || written - packetRead.load(std::memory_order_acquire) == PacketCapacity || output->availableFrames() == 0)
			return false;
		const bool decodedPacket = decoder->decodePacket();
		if (decoder->hasError() || failed.load())
		{
			failed.store(true);
			return false;
		}
		decoded += pcm.size() / channels;
		eof = !decodedPacket && pcm.empty();
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
	State() : thread([this]
		{
			run();
		})
	{
	}
	~State()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
		}
		changed.notify_all();
		thread.join();
	}
	void run()
	{
		std::unique_lock<std::mutex> lock(mutex);
		while (!stopping)
		{
			if (jobs.empty())
			{
				changed.wait(lock, [this]
				{
					return stopping || !jobs.empty();
				});
				if (stopping)
					break;
			}
			bool progress = false;
			for (size_t i = 0; i < jobs.size() && !stopping; ++i)
			{
				auto work = jobs[i];
				if (work->cancelled)
					continue;
				work->inFlight = true;
				lock.unlock();
				try
				{
					progress = work->step() || progress;
				}
				catch (...)
				{
					work->failed.store(true);
				}
				lock.lock();
				work->inFlight = false;
				changed.notify_all();
			}
			// Queues and consumed packets notify. The timeout only covers output draining, which buffers far more than 10 ms.
			if (!progress)
				changed.wait_for(lock, std::chrono::milliseconds(10));
		}
	}
};

OpenALAudioProducer::Job::Job(std::shared_ptr<State> service, std::shared_ptr<Work> work,
	std::unique_ptr<FFmpegAudioFile> decoder)
	: m_service(std::move(service)), m_work(std::move(work)), m_decoder(std::move(decoder))
{
}
OpenALAudioProducer::Job::~Job()
{
	std::unique_lock<std::mutex> lock(m_service->mutex);
	m_work->cancelled = true;
	m_service->changed.wait(lock, [this]
	{
		return !m_work->inFlight;
	});
	auto &jobs = m_service->jobs;
	jobs.erase(std::remove(jobs.begin(), jobs.end(), m_work), jobs.end());
}
bool OpenALAudioProducer::Job::failed() const
{
	return m_work->failed.load();
}
uint64_t OpenALAudioProducer::Job::producedFrames() const
{
	return m_work->produced.load();
}
bool OpenALAudioProducer::Job::peekPacket(Packet &packet) const
{
	const auto read = m_work->packetRead.load(std::memory_order_relaxed);
	if (read == m_work->packetWrite.load(std::memory_order_acquire))
		return false;
	packet = m_work->packets[read % PacketCapacity];
	return true;
}
void OpenALAudioProducer::Job::consumePacket()
{
	m_work->packetRead.fetch_add(1, std::memory_order_release);
	m_service->changed.notify_all();
}
void OpenALAudioProducer::Job::prime()
{
	std::unique_lock<std::mutex> lock(m_service->mutex);
	m_service->changed.wait(lock, [this]
	{
		return !m_work->inFlight && m_work->settled();
	});
}
OpenALAudioProducer::OpenALAudioProducer() : m_state(std::make_shared<State>())
{
}
OpenALAudioProducer::~OpenALAudioProducer() = default;
std::unique_ptr<OpenALAudioProducer::Job> OpenALAudioProducer::add(std::unique_ptr<FFmpegAudioFile> decoder,
	std::shared_ptr<OpenALPcmStream::Producer> output, int channels)
{
	if (!decoder || !output || channels <= 0)
		return {};
	auto work = std::make_shared<Work>();
	work->decoder = decoder.get();
	work->output = std::move(output);
	work->channels = channels;
	std::unique_ptr<Job> job(new Job(m_state, work, std::move(decoder)));
	work->decoder->setPcmCallback(appendDecodedPcm(work->pcm, channels, MaxFrames, work->failed));
	work->decoder->setErrorLogging(false);
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		m_state->jobs.push_back(work);
	}
	m_state->changed.notify_all();
	return job;
}
