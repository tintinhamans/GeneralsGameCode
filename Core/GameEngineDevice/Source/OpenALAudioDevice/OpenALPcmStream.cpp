/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

#include "OpenALAudioDevice/OpenALPcmStream.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <vector>

struct OpenALPcmStream::Data
{
    std::shared_ptr<OpenALDevice::Context> context;
    ALuint source = 0;
    ALuint buffer = 0;
    int channels = 0;
    uint32_t ringFrames = 0;
    std::vector<short> pcm;
    std::atomic<uint32_t> readPosition{0};
    std::atomic<uint32_t> writePosition{0};
    std::atomic<uint32_t> readFrames{0};
    std::atomic<bool> ended{false};
    std::vector<short> replacement;
    uint32_t replaceAfter = 0;
    uint32_t replacementPosition = 0;
    uint32_t writtenFrames = 0;
    enum : uint32_t { InputNormal, InputReplacement, InputClosed };
    std::atomic<uint32_t> inputState{InputNormal};
    std::atomic<bool> starved{false};
    std::atomic<bool> callbackFailed{false};
    State requested = State::Stopped;
    int error = AL_NO_ERROR;

    static_assert(std::atomic<uint32_t>::is_always_lock_free, "Callback indices must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free, "Callback flags must be lock-free");

    uint32_t distance(uint32_t write, uint32_t read) const noexcept
    {
        if (write >= read)
            return write - read;
        return ringFrames - read + write;
    }

    static ALsizei AL_APIENTRY readPcm(void *opaque, void *output, ALsizei bytes) noexcept
    {
        Data &data = *static_cast<Data *>(opaque);
        const uint32_t frameBytes = sizeof(short) * data.channels;
        if (bytes <= 0 || static_cast<uint32_t>(bytes) % frameBytes != 0) {
            data.callbackFailed.store(true, std::memory_order_relaxed);
            return 0;
        }
        const uint32_t requested = static_cast<uint32_t>(bytes) / frameBytes;
        // Observe EOF before the write index so its final published samples cannot be missed.
        const bool ended = data.ended.load(std::memory_order_acquire);
        const bool replacing = data.inputState.load(std::memory_order_acquire) == InputReplacement;
        const uint32_t read = data.readPosition.load(std::memory_order_relaxed);
        const uint32_t write = data.writePosition.load(std::memory_order_acquire);
        uint32_t limit = requested;
        if (replacing) {
            const uint32_t remaining = data.replaceAfter - data.readFrames.load(std::memory_order_relaxed);
            if (remaining > std::numeric_limits<uint32_t>::max() / 2)
                limit = 0;
            else
                limit = std::min(limit, remaining);
        }
        const uint32_t count = std::min(limit, data.distance(write, read));
        if (replacing && count < limit) {
            data.callbackFailed.store(true, std::memory_order_relaxed);
            return 0;
        }
        const uint32_t first = std::min(count, data.ringFrames - read);
        auto destination = static_cast<unsigned char *>(output);
        std::memcpy(destination, data.pcm.data() + read * data.channels, first * frameBytes);
        std::memcpy(destination + first * frameBytes, data.pcm.data(), (count - first) * frameBytes);
        data.readPosition.store((read + count) % data.ringFrames, std::memory_order_release);
        if (replacing) {
            const uint32_t remaining = static_cast<uint32_t>(data.replacement.size() / data.channels) - data.replacementPosition;
            const uint32_t tail = std::min(requested - count, remaining);
            if (tail != 0)
                std::memcpy(destination + count * frameBytes,
                    data.replacement.data() + data.replacementPosition * data.channels, tail * frameBytes);
            data.replacementPosition += tail;
            data.readFrames.fetch_add(count + tail, std::memory_order_relaxed);
            data.starved.store(false, std::memory_order_relaxed);
            return static_cast<ALsizei>((count + tail) * frameBytes);
        }
        data.readFrames.fetch_add(count, std::memory_order_relaxed);
        data.starved.store(!ended && count < requested, std::memory_order_relaxed);
        if (ended && count < requested) {
            uint32_t normal = InputNormal;
            if (!data.inputState.compare_exchange_strong(normal, InputClosed, std::memory_order_acq_rel) &&
                normal == InputReplacement) {
                // Publication won the EOF race; fill the remainder before the driver latches EOF.
                const ALsizei tail = readPcm(opaque, destination + count * frameBytes,
                    static_cast<ALsizei>((requested - count) * frameBytes));
                return static_cast<ALsizei>(count * frameBytes) + tail;
            }
            return static_cast<ALsizei>(count * frameBytes);
        }
        std::memset(destination + count * frameBytes, 0, (requested - count) * frameBytes);
        return bytes;
    }

    ~Data()
    {
        if (context == nullptr)
            return;
        OpenALDevice::Scope scope(*context);
        if (!scope.valid)
            std::terminate();
        reportCleanupError("pending operation");
        if (source != 0) {
            alSourceStop(source);
            // A failed stop cannot guarantee the callback has left PCM storage.
            if (reportCleanupError("stop source"))
                std::terminate();
            alSourcei(source, AL_BUFFER, 0);
            reportCleanupError("detach buffer");
            alDeleteSources(1, &source);
            reportCleanupError("delete source");
        }
        if (buffer != 0) {
            alDeleteBuffers(1, &buffer);
            reportCleanupError("delete buffer");
        }
    }

    static bool reportCleanupError(const char *operation) noexcept
    {
        const ALenum error = alGetError();
        if (error == AL_NO_ERROR)
            return false;
        std::fprintf(stderr, "OpenAL PCM cleanup %s failed: 0x%x\n", operation, static_cast<unsigned int>(error));
        return true;
    }
};

OpenALPcmStream::OpenALPcmStream() = default;

OpenALPcmStream::~OpenALPcmStream()
{
    if (!close())
        std::terminate();
}

bool OpenALPcmStream::close()
{
    if (m_data != nullptr && !m_data->context->onOwnerThread())
        return false;
    m_data.reset();
    return true;
}

bool OpenALPcmStream::open(OpenALDevice &device, int sampleRate, int channels, uint32_t capacityFrames)
{
    if (!close() || !device.isOpen() || sampleRate <= 0 || (channels != 1 && channels != 2) ||
        capacityFrames == 0 || capacityFrames >= static_cast<uint32_t>(std::numeric_limits<int>::max()) / (sizeof(short) * channels))
        return false;
    std::unique_ptr<Data> candidate;
    try {
        candidate = std::make_unique<Data>();
        candidate->context = device.m_context;
        candidate->channels = channels;
        candidate->ringFrames = capacityFrames + 1;
        candidate->pcm.resize(static_cast<size_t>(candidate->ringFrames) * channels);
    } catch (const std::bad_alloc &) {
        return false;
    }
    OpenALDevice::Scope scope(*candidate->context);
    if (!scope.valid)
        return false;
    ALenum format = AL_FORMAT_MONO16;
    if (channels == 2)
        format = AL_FORMAT_STEREO16;
    alGenSources(1, &candidate->source);
    alGenBuffers(1, &candidate->buffer);
    if (alGetError() != AL_NO_ERROR)
        return false;
    candidate->context->bufferCallback(candidate->buffer, format, sampleRate, &Data::readPcm, candidate.get());
    alSourcei(candidate->source, AL_BUFFER, candidate->buffer);
    alSourcei(candidate->source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcef(candidate->source, AL_ROLLOFF_FACTOR, 0.0f);
    if (alIsExtensionPresent("AL_SOFT_direct_channels"))
        alSourcei(candidate->source, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);
    if (alGetError() != AL_NO_ERROR)
        return false;
    m_data = std::move(candidate);
    return true;
}

uint32_t OpenALPcmStream::availableFrames() const
{
    if (m_data == nullptr || !m_data->context->onOwnerThread())
        return 0;
    if (m_data->inputState.load(std::memory_order_relaxed) == Data::InputReplacement)
        return 0;
    const uint32_t write = m_data->writePosition.load(std::memory_order_relaxed);
    const uint32_t read = m_data->readPosition.load(std::memory_order_acquire);
    return m_data->ringFrames - 1 - m_data->distance(write, read);
}

uint32_t OpenALPcmStream::getReadFrames() const
{
    if (m_data == nullptr || !m_data->context->onOwnerThread())
        return 0;
    return m_data->readFrames.load(std::memory_order_relaxed);
}

bool OpenALPcmStream::replaceQueuedAfter(uint32_t endFrame, const short *tail, uint32_t frames)
{
    if (m_data == nullptr || !m_data->context->onOwnerThread() ||
        m_data->inputState.load(std::memory_order_acquire) != Data::InputNormal ||
        (frames != 0 && tail == nullptr) || frames > (16 * 1024 * 1024) / (sizeof(short) * m_data->channels))
        return false;
    const uint32_t ahead = endFrame - m_data->writtenFrames;
    if (ahead != 0 && ahead <= std::numeric_limits<uint32_t>::max() / 2)
        return false;
    try {
        std::vector<short> replacement;
        if (frames != 0)
            replacement.assign(tail, tail + static_cast<size_t>(frames) * m_data->channels);
        if (replacement.capacity() > (16 * 1024 * 1024) / sizeof(short))
            return false;
        m_data->replacement.swap(replacement);
    } catch (const std::bad_alloc &) {
        m_data->error = AL_OUT_OF_MEMORY;
        return false;
    }
    m_data->replaceAfter = endFrame;
    m_data->replacementPosition = 0;
    uint32_t normal = Data::InputNormal;
    return m_data->inputState.compare_exchange_strong(normal, Data::InputReplacement, std::memory_order_release);
}

OpenALPcmStream::QueueResult OpenALPcmStream::queue(const short *interleaved, uint32_t frames)
{
    if (m_data == nullptr || !m_data->context->onOwnerThread() || m_data->error != AL_NO_ERROR ||
        m_data->callbackFailed.load(std::memory_order_relaxed))
        return QueueResult::Error;
    if (interleaved == nullptr || frames == 0 || frames >= m_data->ringFrames || m_data->ended.load(std::memory_order_relaxed) ||
        m_data->inputState.load(std::memory_order_relaxed) != Data::InputNormal)
        return QueueResult::Invalid;
    if (frames > availableFrames())
        return QueueResult::Full;
    const uint32_t write = m_data->writePosition.load(std::memory_order_relaxed);
    const uint32_t first = std::min(frames, m_data->ringFrames - write);
    const size_t frameBytes = sizeof(short) * m_data->channels;
    std::memcpy(m_data->pcm.data() + write * m_data->channels, interleaved, first * frameBytes);
    std::memcpy(m_data->pcm.data(), interleaved + first * m_data->channels, (frames - first) * frameBytes);
    m_data->writePosition.store((write + frames) % m_data->ringFrames, std::memory_order_release);
    m_data->writtenFrames += frames;
    return QueueResult::Accepted;
}

bool OpenALPcmStream::play()
{
    if (m_data == nullptr)
        return false;
    OpenALDevice::Scope scope(*m_data->context);
    if (!scope.valid)
        return false;
    ALint current = AL_INITIAL;
    alGetSourcei(m_data->source, AL_SOURCE_STATE, &current);
    if (current == AL_STOPPED && (m_data->ended.load(std::memory_order_relaxed) ||
        m_data->inputState.load(std::memory_order_relaxed) != Data::InputNormal))
        return false;
    if (current != AL_PLAYING)
        alSourcePlay(m_data->source);
    m_data->error = alGetError();
    if (m_data->error != AL_NO_ERROR)
        return false;
    m_data->requested = State::Playing;
    return true;
}

bool OpenALPcmStream::pause()
{
    if (m_data == nullptr || !m_data->context->onOwnerThread() || m_data->requested != State::Playing)
        return false;
    OpenALDevice::Scope scope(*m_data->context);
    if (!scope.valid)
        return false;
    alSourcePause(m_data->source);
    m_data->error = alGetError();
    if (m_data->error != AL_NO_ERROR)
        return false;
    m_data->requested = State::Paused;
    return true;
}

bool OpenALPcmStream::stop()
{
    if (m_data == nullptr)
        return false;
    OpenALDevice::Scope scope(*m_data->context);
    if (!scope.valid)
        return false;
    // OpenAL Soft synchronizes the mixer on stop before callback storage is reset.
    alSourceStop(m_data->source);
    alSourceRewind(m_data->source);
    m_data->error = alGetError();
    if (m_data->error != AL_NO_ERROR)
        return false;
    m_data->readPosition.store(0, std::memory_order_relaxed);
    m_data->writePosition.store(0, std::memory_order_relaxed);
    m_data->readFrames.store(0, std::memory_order_relaxed);
    m_data->inputState.store(Data::InputNormal, std::memory_order_relaxed);
    m_data->replacement.clear();
    m_data->replacementPosition = 0;
    m_data->writtenFrames = 0;
    m_data->ended.store(false, std::memory_order_relaxed);
    m_data->starved.store(false, std::memory_order_relaxed);
    m_data->callbackFailed.store(false, std::memory_order_relaxed);
    m_data->requested = State::Stopped;
    return true;
}

bool OpenALPcmStream::markEnd()
{
    if (m_data == nullptr || !m_data->context->onOwnerThread())
        return false;
    m_data->ended.store(true, std::memory_order_release);
    return true;
}

bool OpenALPcmStream::setGain(float gain)
{
    if (m_data == nullptr || !std::isfinite(gain) || gain < 0.0f)
        return false;
    OpenALDevice::Scope scope(*m_data->context);
    if (!scope.valid)
        return false;
    alSourcef(m_data->source, AL_GAIN, gain);
    m_data->error = alGetError();
    return m_data->error == AL_NO_ERROR;
}

bool OpenALPcmStream::setPitch(float pitch)
{
    if (m_data == nullptr || !std::isfinite(pitch) || pitch <= 0.0f)
        return false;
    OpenALDevice::Scope scope(*m_data->context);
    if (!scope.valid)
        return false;
    alSourcef(m_data->source, AL_PITCH, pitch);
    m_data->error = alGetError();
    return m_data->error == AL_NO_ERROR;
}

bool OpenALPcmStream::setPosition(float x, float y, float z, bool positional)
{
    if (m_data == nullptr || (positional && m_data->channels != 1) ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    OpenALDevice::Scope scope(*m_data->context);
    if (!scope.valid)
        return false;
    ALint relative = AL_TRUE;
    if (positional)
        relative = AL_FALSE;
    alSourcei(m_data->source, AL_SOURCE_RELATIVE, relative);
    alSource3f(m_data->source, AL_POSITION, x, y, z);
    m_data->error = alGetError();
    return m_data->error == AL_NO_ERROR;
}

OpenALPcmStream::State OpenALPcmStream::state() const
{
    if (m_data == nullptr || !m_data->context->onOwnerThread())
        return State::Error;
    OpenALDevice::Scope scope(*m_data->context);
    if (!scope.valid || m_data->error != AL_NO_ERROR || m_data->callbackFailed.load(std::memory_order_relaxed))
        return State::Error;
    ALint current = AL_INITIAL;
    alGetSourcei(m_data->source, AL_SOURCE_STATE, &current);
    m_data->error = alGetError();
    if (m_data->error != AL_NO_ERROR)
        return State::Error;
    if (current == AL_STOPPED && (m_data->ended.load(std::memory_order_relaxed) ||
        m_data->inputState.load(std::memory_order_relaxed) != Data::InputNormal))
        return State::Finished;
    if (m_data->requested == State::Playing && m_data->starved.load(std::memory_order_relaxed))
        return State::Starved;
    return m_data->requested;
}

int OpenALPcmStream::getLastError() const
{
    if (m_data == nullptr || !m_data->context->onOwnerThread())
        return AL_INVALID_OPERATION;
    if (m_data->callbackFailed.load(std::memory_order_relaxed))
        return AL_INVALID_VALUE;
    return m_data->error;
}
