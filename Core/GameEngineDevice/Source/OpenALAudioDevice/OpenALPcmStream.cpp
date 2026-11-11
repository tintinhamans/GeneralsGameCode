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
#include "OpenALMonoMix.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <vector>

struct OpenALPcmStream::ProducerState
{
	std::mutex mutex;
	Data *data = nullptr;
};

struct OpenALPcmStream::Data
{
	std::shared_ptr<OpenALDevice::Context> context;
	uint64_t recoveryEpoch = 0;
	std::shared_ptr<ProducerState> producerState;
	ALuint source = 0;
	ALuint buffer = 0;
	int channels = 0;
	bool stereoMono = false;
	bool cachedSpans = false;
	struct Segment
	{
		const void *samples = nullptr;
		uint32_t frames = 0;
		uint32_t ringStart = 0;
		int channels = 0;
		bool ring = false;
	};
	std::vector<Segment> segments;
	std::vector<std::shared_ptr<const void>> segmentOwners;
	std::atomic<uint32_t> segmentRead{0}, segmentWrite{0};
	uint32_t segmentOffset = 0;
	uint32_t segmentRetired = 0;
	std::atomic<uint32_t> mixSequence{0};
	std::atomic<uint32_t> mixValues[4]{};
	float leftGain = 0.5f, rightGain = 0.5f, frontBack = 0;
	bool surround = false;
	double previousRear = 0;
	std::vector<uint32_t> portionFrames;
	std::atomic<uint32_t> portionRead{0}, portionWrite{0};
	bool replacementPortionStarted = false;
	bool hasPortionFrame = false;
	uint32_t lastPortionFrame = 0;
	uint32_t ringFrames = 0;
	std::vector<short> pcm;
	std::vector<float> floatPcm;
	std::atomic<uint32_t> readPosition{0};
	std::atomic<uint32_t> writePosition{0};
	std::atomic<uint32_t> readFrames{0};
	std::atomic<bool> ended{false};
	std::vector<short> replacement;
	std::vector<float> floatReplacement;
	uint32_t replaceAfter = 0;
	uint32_t replacementPosition = 0;
	std::atomic<uint32_t> writtenFrames{0};
	enum : uint32_t { InputNormal, InputReplacement, InputClosed };
	std::atomic<uint32_t> inputState{InputNormal};
	std::atomic<bool> starved{false};
	std::atomic<bool> callbackFailed{false};
	float appliedGain = 0;
	bool gainApplied = false;
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

	void reclaimSegments()
	{
		if (!cachedSpans)
			return;
		const uint32_t read = segmentRead.load(std::memory_order_acquire);
		while (segmentRetired != read)
		{
			segmentOwners[segmentRetired].reset();
			segmentRetired = (segmentRetired + 1) % static_cast<uint32_t>(segments.size());
		}
	}

	bool segmentAvailable()
	{
		reclaimSegments();
		const uint32_t write = segmentWrite.load(std::memory_order_relaxed);
		const uint32_t next = (write + 1) % static_cast<uint32_t>(segments.size());
		return next != segmentRead.load(std::memory_order_acquire);
	}

	void publishSegment(const Segment &segment, std::shared_ptr<const void> owner)
	{
		const uint32_t write = segmentWrite.load(std::memory_order_relaxed);
		segments[write] = segment;
		segmentOwners[write] = std::move(owner);
		segmentWrite.store((write + 1) % static_cast<uint32_t>(segments.size()), std::memory_order_release);
	}

	uint32_t ringAvailable() const noexcept
	{
		const uint32_t write = writePosition.load(std::memory_order_relaxed);
		const uint32_t read = readPosition.load(std::memory_order_acquire);
		return ringFrames - 1 - distance(write, read);
	}

	QueueResult queueRing(const short *interleaved, uint32_t frames)
	{
		if (callbackFailed.load(std::memory_order_relaxed))
			return QueueResult::Error;
		if (interleaved == nullptr || frames == 0 || frames >= ringFrames ||
			ended.load(std::memory_order_relaxed) || inputState.load(std::memory_order_relaxed) != Data::InputNormal)
			return QueueResult::Invalid;
		if (frames > ringAvailable())
			return QueueResult::Full;
		if (cachedSpans && !segmentAvailable())
			return QueueResult::Full;
		const uint32_t write = writePosition.load(std::memory_order_relaxed);
		const uint32_t first = std::min(frames, ringFrames - write);
		copyInput(write, interleaved, first);
		copyInput(0, interleaved + static_cast<size_t>(first) * channels, frames - first);
		writePosition.store((write + frames) % ringFrames, std::memory_order_release);
		writtenFrames.fetch_add(frames, std::memory_order_relaxed);
		if (cachedSpans)
		{
			Data::Segment segment;
			segment.frames = frames;
			segment.ringStart = write;
			segment.ring = true;
			publishSegment(segment, {});
		}
		return QueueResult::Accepted;
	}

	static uint32_t bits(float value) noexcept
	{
		uint32_t result;
		std::memcpy(&result, &value, sizeof(result));
		return result;
	}

	static float value(uint32_t bits) noexcept
	{
		float result;
		std::memcpy(&result, &bits, sizeof(result));
		return result;
	}

	void readMix() noexcept
	{
		for (unsigned attempt = 0; attempt < 4; ++attempt)
		{
			const uint32_t before = mixSequence.load(std::memory_order_acquire);
			if ((before & 1) != 0)
				continue;
			const uint32_t left = mixValues[0].load(std::memory_order_relaxed);
			const uint32_t right = mixValues[1].load(std::memory_order_relaxed);
			const uint32_t front = mixValues[2].load(std::memory_order_relaxed);
			const uint32_t mode = mixValues[3].load(std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_acquire);
			if (mixSequence.load(std::memory_order_acquire) != before)
				continue;
			leftGain = value(left);
			rightGain = value(right);
			frontBack = value(front);
			surround = mode != 0;
			return;
		}
	}

	const void *inputAt(uint32_t frame) const noexcept
	{
		if (stereoMono)
			return floatPcm.data() + frame;
		return pcm.data() + frame * channels;
	}

	const void *replacementAt(uint32_t frame) const noexcept
	{
		if (stereoMono)
			return floatReplacement.data() + frame;
		return replacement.data() + frame * channels;
	}

	uint32_t replacementFrames() const noexcept
	{
		if (stereoMono)
			return static_cast<uint32_t>(floatReplacement.size());
		return static_cast<uint32_t>(replacement.size() / channels);
	}

	bool playbackFinished(ALint nativeState) const
	{
		if (nativeState != AL_STOPPED)
			return false;
		return inputState.load(std::memory_order_acquire) == InputClosed;
	}

	void copyInput(uint32_t frame, const short *input, uint32_t count) noexcept
	{
		if (!stereoMono)
		{
			std::memcpy(pcm.data() + frame * channels, input, count * channels * sizeof(short));
			return;
		}
		for (uint32_t i = 0; i < count; ++i)
			floatPcm[frame + i] = input[i] / 32768.0f;
	}

	template<typename Sample>
	void writeMonoOutput(float *stereo, const Sample *mono, uint32_t frames,
		uint32_t firstFrame, bool replacement) noexcept
	{
		if (replacement && !replacementPortionStarted)
		{
			portionRead.store(portionWrite.load(std::memory_order_acquire), std::memory_order_release);
			previousRear = 0;
			replacementPortionStarted = true;
		}
		uint32_t command = portionRead.load(std::memory_order_relaxed);
		const uint32_t end = portionWrite.load(std::memory_order_acquire);
		uint32_t frame = 0;
		while (frame < frames)
		{
			while (!replacement && command != end &&
				firstFrame + frame - portionFrames[command] < 0x80000000u)
			{
				previousRear = 0;
				command = (command + 1) % static_cast<uint32_t>(portionFrames.size());
			}
			uint32_t count = frames - frame;
			if (!replacement && command != end)
				count = std::min(count, portionFrames[command] - (firstFrame + frame));
			const uint32_t chunkEnd = frame + count;
			if (surround)
			{
				for (; frame < chunkEnd; ++frame)
				{
					const double sample = OpenALMonoMix::normalized(mono[frame]);
					const double left = sample * leftGain;
					const double right = sample * rightGain;
					stereo[frame * 2] = static_cast<float>(((1 - frontBack) * left + frontBack * previousRear));
					stereo[frame * 2 + 1] = static_cast<float>(((1 - frontBack) * right - frontBack * previousRear));
					previousRear = (left + right) * 0.5;
				}
			}
			else
			{
				OpenALMonoMix::writeStereo(stereo + frame * 2, mono + frame, count, leftGain, rightGain);
				frame = chunkEnd;
				const double sample = OpenALMonoMix::normalized(mono[chunkEnd - 1]);
				const double left = sample * leftGain;
				const double right = sample * rightGain;
				previousRear = (left + right) * 0.5;
			}
		}
		portionRead.store(command, std::memory_order_release);
	}

	void writeOutput(void *output, const void *input, uint32_t frames, uint32_t firstFrame,
		bool replacement = false, bool floatInput = true, int inputChannels = 0) noexcept
	{
		if (!stereoMono && inputChannels == 1 && channels == 2)
		{
			const auto mono = static_cast<const short *>(input);
			auto stereo = static_cast<short *>(output);
			for (uint32_t frame = 0; frame < frames; ++frame)
			{
				stereo[frame * 2] = mono[frame];
				stereo[frame * 2 + 1] = mono[frame];
			}
			return;
		}
		if (!stereoMono)
		{
			std::memcpy(output, input, frames * channels * sizeof(short));
			return;
		}
		if (frames == 0)
			return;
		auto stereo = static_cast<float *>(output);
		if (floatInput)
			writeMonoOutput(stereo, static_cast<const float *>(input), frames, firstFrame, replacement);
		else
			writeMonoOutput(stereo, static_cast<const short *>(input), frames, firstFrame, replacement);
	}

	uint32_t readSegments(unsigned char *output, uint32_t frames, uint32_t firstFrame,
		uint32_t frameBytes) noexcept
	{
		uint32_t count = 0;
		uint32_t read = segmentRead.load(std::memory_order_relaxed);
		const uint32_t end = segmentWrite.load(std::memory_order_acquire);
		while (count < frames && read != end)
		{
			const Segment &segment = segments[read];
			const uint32_t take = std::min(frames - count, segment.frames - segmentOffset);
			if (segment.ring)
			{
				const uint32_t position = (segment.ringStart + segmentOffset) % ringFrames;
				const uint32_t first = std::min(take, ringFrames - position);
				writeOutput(output + count * frameBytes, inputAt(position), first, firstFrame + count);
				writeOutput(output + (count + first) * frameBytes, inputAt(0), take - first,
					firstFrame + count + first);
				readPosition.store((position + take) % ringFrames, std::memory_order_release);
			}
			else
			{
				const auto input = static_cast<const short *>(segment.samples) +
					static_cast<size_t>(segmentOffset) * segment.channels;
				writeOutput(output + count * frameBytes, input, take,
					firstFrame + count, false, false, segment.channels);
			}
			segmentOffset += take;
			count += take;
			if (segmentOffset == segment.frames)
			{
				segmentOffset = 0;
				read = (read + 1) % static_cast<uint32_t>(segments.size());
				segmentRead.store(read, std::memory_order_release);
			}
		}
		return count;
	}

	static ALsizei AL_APIENTRY readPcm(void *opaque, void *output, ALsizei bytes) noexcept
	{
		Data &data = *static_cast<Data *>(opaque);
		uint32_t sampleBytes = sizeof(short);
		if (data.stereoMono)
			sampleBytes = sizeof(float);
		const uint32_t frameBytes = sampleBytes * 2;
		if (bytes <= 0 || static_cast<uint32_t>(bytes) % frameBytes != 0)
		{
			data.callbackFailed.store(true, std::memory_order_relaxed);
			return 0;
		}
		const uint32_t requested = static_cast<uint32_t>(bytes) / frameBytes;
		const uint32_t firstFrame = data.readFrames.load(std::memory_order_relaxed);
		if (data.stereoMono)
			data.readMix();
		// Observe EOF before the write index so its final published samples cannot be missed.
		const bool ended = data.ended.load(std::memory_order_acquire);
		const auto input = data.inputState.load(std::memory_order_acquire);
		if (input == InputClosed)
			return 0;
		const bool replacing = input == InputReplacement;
		const uint32_t read = data.readPosition.load(std::memory_order_relaxed);
		const uint32_t write = data.writePosition.load(std::memory_order_acquire);
		uint32_t limit = requested;
		if (replacing)
		{
			const uint32_t remaining = data.replaceAfter - data.readFrames.load(std::memory_order_relaxed);
			if (remaining > std::numeric_limits<uint32_t>::max() / 2)
				limit = 0;
			else
				limit = std::min(limit, remaining);
		}
		auto destination = static_cast<unsigned char *>(output);
		uint32_t count = 0;
		if (data.cachedSpans)
			count = data.readSegments(destination, limit, firstFrame, frameBytes);
		else
		{
			count = std::min(limit, data.distance(write, read));
			const uint32_t first = std::min(count, data.ringFrames - read);
			data.writeOutput(destination, data.inputAt(read), first, firstFrame);
			data.writeOutput(destination + first * frameBytes, data.inputAt(0), count - first, firstFrame + first);
			data.readPosition.store((read + count) % data.ringFrames, std::memory_order_release);
		}
		if (replacing && count < limit)
		{
			data.callbackFailed.store(true, std::memory_order_relaxed);
			return 0;
		}
		if (replacing)
		{
			if (data.cachedSpans && count < requested)
			{
				data.segmentOffset = 0;
				data.segmentRead.store(data.segmentWrite.load(std::memory_order_acquire), std::memory_order_release);
				data.readPosition.store(data.writePosition.load(std::memory_order_acquire), std::memory_order_release);
			}
			const uint32_t remaining = data.replacementFrames() - data.replacementPosition;
			const uint32_t tail = std::min(requested - count, remaining);
			if (tail != 0)
				data.writeOutput(destination + count * frameBytes,
					data.replacementAt(data.replacementPosition), tail,
					firstFrame + count, true);
			data.replacementPosition += tail;
			data.readFrames.fetch_add(count + tail, std::memory_order_relaxed);
			data.starved.store(false, std::memory_order_relaxed);
			if (count + tail < requested)
				data.inputState.store(InputClosed, std::memory_order_release);
			return static_cast<ALsizei>((count + tail) * frameBytes);
		}
		data.readFrames.fetch_add(count, std::memory_order_relaxed);
		data.starved.store(!ended && count < requested, std::memory_order_relaxed);
		if (ended && count < requested)
		{
			uint32_t normal = InputNormal;
			if (!data.inputState.compare_exchange_strong(normal, InputClosed, std::memory_order_acq_rel) &&
				normal == InputReplacement)
			{
				// Publication won the EOF race, so the remainder is filled before the driver latches EOF.
				const ALsizei tail = readPcm(opaque, destination + count * frameBytes,
					static_cast<ALsizei>((requested - count) * frameBytes));
				return static_cast<ALsizei>(count * frameBytes) + tail;
			}
			return static_cast<ALsizei>(count * frameBytes);
		}
		std::memset(destination + count * frameBytes, 0, (requested - count) * frameBytes);
		if (data.stereoMono && count < requested)
			data.previousRear = 0;
		return bytes;
	}

	~Data()
	{
		if (context == nullptr)
			return;
		if (!context->onOwnerThread())
			std::terminate();
		reportCleanupError("pending operation");
		if (source != 0)
		{
			alSourceStop(source);
			// A failed stop cannot guarantee the callback has left PCM storage.
			if (reportCleanupError("stop source"))
				std::terminate();
			alSourcei(source, AL_BUFFER, 0);
			reportCleanupError("detach buffer");
			alDeleteSources(1, &source);
			reportCleanupError("delete source");
		}
		if (buffer != 0)
		{
			alDeleteBuffers(1, &buffer);
			reportCleanupError("delete buffer");
		}
	}

	static bool reportCleanupError(const char *operation) noexcept
	{
		const ALenum error = alGetError();
		if (error == AL_NO_ERROR)
			return false;
		DEBUG_LOG(("OpenAL PCM cleanup %s failed: 0x%x", operation, static_cast<unsigned int>(error)));
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
	releaseProducer();
	m_data.reset();
	return true;
}

bool OpenALPcmStream::open(OpenALDevice &device, int sampleRate, int channels, uint32_t capacityFrames, bool stereoMono, InputMode mode)
{
	if (!close() || !device.isOpen() || sampleRate <= 0 || (channels != 1 && channels != 2) ||
		stereoMono != (channels == 1) ||
		(mode != InputMode::Queued && mode != InputMode::CachedSpans) || capacityFrames == 0 || capacityFrames >= static_cast<uint32_t>(std::numeric_limits<int>::max()) / (sizeof(short) * channels))
		return false;
	auto candidate = std::make_unique<Data>();
	candidate->context = device.m_context;
	candidate->recoveryEpoch = device.m_context->recoveryEpoch;
	candidate->channels = channels;
	candidate->stereoMono = stereoMono;
	candidate->cachedSpans = mode == InputMode::CachedSpans;
	if (candidate->cachedSpans)
	{
		candidate->segments.resize(static_cast<size_t>(capacityFrames) + 2);
		candidate->segmentOwners.resize(candidate->segments.size());
	}
	candidate->mixValues[0].store(Data::bits(0.5f));
	candidate->mixValues[1].store(Data::bits(0.5f));
	candidate->ringFrames = capacityFrames + 1;
	if (stereoMono)
	{
		candidate->floatPcm.resize(candidate->ringFrames);
		candidate->portionFrames.resize(candidate->ringFrames + 1);
	}
	else
	{
		candidate->pcm.resize(static_cast<size_t>(candidate->ringFrames) * channels);
	}
	ALenum format = AL_FORMAT_STEREO16;
	if (stereoMono)
	{
		if (!alIsExtensionPresent("AL_EXT_FLOAT32") || !alIsExtensionPresent("AL_SOFT_direct_channels"))
			return false;
		format = AL_FORMAT_STEREO_FLOAT32;
	}
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

void OpenALPcmStream::reclaimConsumed()
{
	if (m_data != nullptr)
		m_data->reclaimSegments();
}

uint32_t OpenALPcmStream::availableFrames() const
{
	if (m_data == nullptr)
		return 0;
	if (m_data->inputState.load(std::memory_order_relaxed) == Data::InputReplacement)
		return 0;
	const uint32_t write = m_data->writePosition.load(std::memory_order_relaxed);
	const uint32_t read = m_data->readPosition.load(std::memory_order_acquire);
	return m_data->ringFrames - 1 - m_data->distance(write, read);
}

OpenALPcmStream::Producer::Producer(std::shared_ptr<ProducerState> state) : m_state(std::move(state))
{
}

OpenALPcmStream::QueueResult OpenALPcmStream::Producer::queue(const short *interleaved, uint32_t frames)
{
	std::lock_guard<std::mutex> lock(m_state->mutex);
	if (m_state->data == nullptr)
		return QueueResult::Error;
	return m_state->data->queueRing(interleaved, frames);
}

uint32_t OpenALPcmStream::Producer::availableFrames() const
{
	std::lock_guard<std::mutex> lock(m_state->mutex);
	const Data *data = m_state->data;
	if (data == nullptr || data->callbackFailed.load(std::memory_order_relaxed) ||
		data->ended.load(std::memory_order_acquire) ||
		data->inputState.load(std::memory_order_acquire) != Data::InputNormal)
		return 0;
	return data->ringAvailable();
}

std::shared_ptr<OpenALPcmStream::Producer> OpenALPcmStream::acquireProducer()
{
	if (m_data == nullptr || m_data->error != AL_NO_ERROR ||
		m_data->cachedSpans || m_data->producerState != nullptr ||
		m_data->ended.load(std::memory_order_relaxed) ||
		m_data->inputState.load(std::memory_order_relaxed) != Data::InputNormal ||
		m_data->callbackFailed.load(std::memory_order_relaxed))
		return {};
	auto state = std::make_shared<ProducerState>();
	auto producer = std::shared_ptr<Producer>(new Producer(state));
	state->data = m_data.get();
	m_data->producerState = std::move(state);
	return producer;
}

void OpenALPcmStream::releaseProducer()
{
	if (m_data == nullptr || m_data->producerState == nullptr)
		return;
	auto state = std::move(m_data->producerState);
	std::lock_guard<std::mutex> lock(state->mutex);
	state->data = nullptr;
}

uint32_t OpenALPcmStream::getReadFrames() const
{
	if (m_data == nullptr)
		return 0;
	return m_data->readFrames.load(std::memory_order_relaxed);
}

bool OpenALPcmStream::replaceQueuedAfter(uint32_t endFrame, const short *tail, uint32_t frames, int sourceChannels)
{
	if (m_data == nullptr)
		return false;
	if (sourceChannels == 0)
		sourceChannels = m_data->channels;
	if (m_data->producerState != nullptr ||
		(sourceChannels != m_data->channels && (sourceChannels != 1 || m_data->stereoMono)) ||
		m_data->inputState.load(std::memory_order_acquire) != Data::InputNormal || (frames != 0 && tail == nullptr))
		return false;
	size_t frameBytes = sizeof(short) * m_data->channels;
	if (m_data->stereoMono)
		frameBytes = sizeof(float);
	if (frames > (16 * 1024 * 1024) / frameBytes)
		return false;
	const uint32_t ahead = endFrame - m_data->writtenFrames.load(std::memory_order_acquire);
	if (ahead != 0 && ahead <= std::numeric_limits<uint32_t>::max() / 2)
		return false;
	if (m_data->stereoMono)
	{
		std::vector<float> replacement(frames);
		for (uint32_t i = 0; i < frames; ++i)
			replacement[i] = tail[i] / 32768.0f;
		if (replacement.capacity() > (16 * 1024 * 1024) / sizeof(float))
			return false;
		m_data->floatReplacement.swap(replacement);
	}
	else
	{
		std::vector<short> replacement;
		if (frames != 0 && sourceChannels != m_data->channels)
		{
			replacement.resize(static_cast<size_t>(frames) * m_data->channels);
			for (uint32_t frame = 0; frame < frames; ++frame)
			{
				replacement[frame * 2] = tail[frame];
				replacement[frame * 2 + 1] = tail[frame];
			}
		}
		else if (frames != 0)
		{
			replacement.assign(tail, tail + static_cast<size_t>(frames) * m_data->channels);
		}
		if (replacement.capacity() > (16 * 1024 * 1024) / sizeof(short))
			return false;
		m_data->replacement.swap(replacement);
	}
	m_data->replaceAfter = endFrame;
	m_data->replacementPosition = 0;
	uint32_t normal = Data::InputNormal;
	return m_data->inputState.compare_exchange_strong(normal, Data::InputReplacement, std::memory_order_release);
}

OpenALPcmStream::QueueResult OpenALPcmStream::queue(const short *interleaved, uint32_t frames)
{
	if (m_data == nullptr || m_data->error != AL_NO_ERROR || m_data->producerState != nullptr)
		return QueueResult::Error;
	return m_data->queueRing(interleaved, frames);
}

OpenALPcmStream::QueueResult OpenALPcmStream::queueCached(
	std::shared_ptr<const std::vector<short>> pcm, uint32_t offsetFrames, uint32_t frames, int sourceChannels)
{
	if (pcm == nullptr)
		return QueueResult::Invalid;
	if (m_data == nullptr || m_data->error != AL_NO_ERROR ||
		m_data->callbackFailed.load(std::memory_order_relaxed) || m_data->producerState != nullptr)
		return QueueResult::Error;
	if (sourceChannels == 0)
		sourceChannels = m_data->channels;
	const size_t available = pcm->size();
	if (!m_data->cachedSpans || frames == 0 || frames >= 0x80000000u ||
		(sourceChannels != m_data->channels && (sourceChannels != 1 || m_data->stereoMono)) ||
		m_data->ended.load(std::memory_order_relaxed) ||
		m_data->inputState.load(std::memory_order_relaxed) != Data::InputNormal ||
		offsetFrames > available / sourceChannels ||
		frames > available / sourceChannels - offsetFrames)
		return QueueResult::Invalid;
	if (!m_data->segmentAvailable())
		return QueueResult::Full;
	Data::Segment segment;
	segment.samples = pcm->data() + static_cast<size_t>(offsetFrames) * sourceChannels;
	segment.frames = frames;
	segment.channels = sourceChannels;
	m_data->writtenFrames.fetch_add(frames, std::memory_order_relaxed);
	m_data->publishSegment(segment, std::move(pcm));
	return QueueResult::Accepted;
}

void OpenALPcmStream::update()
{
	if (m_data == nullptr || !m_data->context->connected() ||
		m_data->error != AL_NO_ERROR || m_data->callbackFailed.load(std::memory_order_relaxed) ||
		m_data->recoveryEpoch == m_data->context->recoveryEpoch || m_data->requested == State::Paused)
		return;
	if (m_data->requested == State::Playing &&
		m_data->inputState.load(std::memory_order_acquire) != Data::InputClosed)
	{
		ALint current = AL_INITIAL;
		alGetSourcei(m_data->source, AL_SOURCE_STATE, &current);
		if (!m_data->playbackFinished(current))
			alSourcePlay(m_data->source);
		m_data->error = alGetError();
	}
	m_data->recoveryEpoch = m_data->context->recoveryEpoch;
}

bool OpenALPcmStream::play()
{
	if (m_data == nullptr)
		return false;
	if (m_data->error != AL_NO_ERROR || m_data->callbackFailed.load(std::memory_order_relaxed))
		return false;
	if (!m_data->context->connected())
	{
		if (m_data->inputState.load(std::memory_order_acquire) == Data::InputClosed)
			return false;
		m_data->requested = State::Playing;
		return true;
	}
	ALint current = AL_INITIAL;
	alGetSourcei(m_data->source, AL_SOURCE_STATE, &current);
	if (m_data->playbackFinished(current))
		return false;
	if (current != AL_PLAYING)
		alSourcePlay(m_data->source);
	m_data->error = alGetError();
	if (m_data->error != AL_NO_ERROR)
		return false;
	m_data->requested = State::Playing;
	m_data->recoveryEpoch = m_data->context->recoveryEpoch;
	return true;
}

bool OpenALPcmStream::pause()
{
	if (m_data == nullptr || m_data->requested != State::Playing)
		return false;
	if (!m_data->context->connected())
	{
		m_data->requested = State::Paused;
		return true;
	}
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
	releaseProducer();
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
	m_data->floatReplacement.clear();
	m_data->replacementPosition = 0;
	m_data->writtenFrames.store(0, std::memory_order_relaxed);
	m_data->ended.store(false, std::memory_order_relaxed);
	m_data->starved.store(false, std::memory_order_relaxed);
	m_data->callbackFailed.store(false, std::memory_order_relaxed);
	m_data->portionRead.store(0, std::memory_order_relaxed);
	m_data->portionWrite.store(0, std::memory_order_relaxed);
	m_data->replacementPortionStarted = false;
	m_data->hasPortionFrame = false;
	m_data->previousRear = 0;
	m_data->segmentOffset = 0;
	m_data->segmentRead.store(0, std::memory_order_relaxed);
	m_data->segmentWrite.store(0, std::memory_order_relaxed);
	m_data->segmentRetired = 0;
	for (auto &owner : m_data->segmentOwners)
		owner.reset();
	m_data->requested = State::Stopped;
	return true;
}

bool OpenALPcmStream::markEnd()
{
	if (m_data == nullptr || m_data->producerState != nullptr)
		return false;
	m_data->ended.store(true, std::memory_order_release);
	return true;
}

bool OpenALPcmStream::setGain(float gain)
{
	if (m_data == nullptr || !std::isfinite(gain) || gain < 0.0f)
		return false;
	if (!m_data->gainApplied || gain != m_data->appliedGain)
		alSourcef(m_data->source, AL_GAIN, gain);
	m_data->error = alGetError();
	if (m_data->error != AL_NO_ERROR)
	{
		m_data->gainApplied = false;
		return false;
	}
	m_data->appliedGain = gain;
	m_data->gainApplied = true;
	return true;
}

bool OpenALPcmStream::setPitch(float pitch)
{
	if (m_data == nullptr || !std::isfinite(pitch) || pitch <= 0.0f)
		return false;
	alSourcef(m_data->source, AL_PITCH, pitch);
	m_data->error = alGetError();
	return m_data->error == AL_NO_ERROR;
}

bool OpenALPcmStream::useLinearResampler()
{
	if (m_data == nullptr || m_data->context->linearResampler < 0)
		return false;
	alSourcei(m_data->source, AL_SOURCE_RESAMPLER_SOFT, m_data->context->linearResampler);
	m_data->error = alGetError();
	return m_data->error == AL_NO_ERROR;
}

bool OpenALPcmStream::setStereoMix(float leftGain, float rightGain, float frontBack, bool surround)
{
	if (m_data == nullptr || !m_data->stereoMono ||
		!std::isfinite(leftGain) || !std::isfinite(rightGain) || !std::isfinite(frontBack) ||
		leftGain < 0 || leftGain > 1 || rightGain < 0 || rightGain > 1 || frontBack < 0 || frontBack > 1)
		return false;
	m_data->mixSequence.fetch_add(1, std::memory_order_acq_rel);
	std::atomic_thread_fence(std::memory_order_release);
	m_data->mixValues[0].store(Data::bits(leftGain), std::memory_order_relaxed);
	m_data->mixValues[1].store(Data::bits(rightGain), std::memory_order_relaxed);
	m_data->mixValues[2].store(Data::bits(frontBack), std::memory_order_relaxed);
	m_data->mixValues[3].store(static_cast<uint32_t>(surround), std::memory_order_relaxed);
	m_data->mixSequence.fetch_add(1, std::memory_order_release);
	return true;
}

bool OpenALPcmStream::schedulePortionBoundary(uint32_t sourceFrame)
{
	if (m_data == nullptr || !m_data->stereoMono ||
		m_data->inputState.load(std::memory_order_acquire) != Data::InputNormal)
		return false;
	const uint32_t readFrame = m_data->readFrames.load(std::memory_order_acquire);
	if (sourceFrame - readFrame >= 0x80000000u)
		return false;
	if (m_data->hasPortionFrame)
	{
		if (sourceFrame == m_data->lastPortionFrame)
			return true;
		if (sourceFrame - m_data->lastPortionFrame >= 0x80000000u)
			return false;
	}
	const uint32_t write = m_data->portionWrite.load(std::memory_order_relaxed);
	const uint32_t next = (write + 1) % static_cast<uint32_t>(m_data->portionFrames.size());
	if (next == m_data->portionRead.load(std::memory_order_acquire))
		return false;
	m_data->portionFrames[write] = sourceFrame;
	m_data->lastPortionFrame = sourceFrame;
	m_data->hasPortionFrame = true;
	m_data->portionWrite.store(next, std::memory_order_release);
	return true;
}

OpenALPcmStream::State OpenALPcmStream::state() const
{
	if (m_data == nullptr || m_data->error != AL_NO_ERROR || m_data->callbackFailed.load(std::memory_order_relaxed))
		return State::Error;
	ALint current = AL_INITIAL;
	alGetSourcei(m_data->source, AL_SOURCE_STATE, &current);
	m_data->error = alGetError();
	if (m_data->error != AL_NO_ERROR)
		return State::Error;
	if (m_data->playbackFinished(current))
		return State::Finished;
	if (!m_data->context->connected())
		return State::Disconnected;
	if (m_data->recoveryEpoch != m_data->context->recoveryEpoch &&
		m_data->inputState.load(std::memory_order_acquire) != Data::InputClosed)
	{
		if (m_data->requested == State::Paused)
			return State::Paused;
		if (m_data->requested == State::Playing)
			return State::Disconnected;
	}
	if (m_data->requested == State::Playing && m_data->starved.load(std::memory_order_relaxed))
		return State::Starved;
	return m_data->requested;
}

int OpenALPcmStream::getLastError() const
{
	if (m_data == nullptr)
		return AL_INVALID_OPERATION;
	if (m_data->callbackFailed.load(std::memory_order_relaxed))
		return AL_INVALID_VALUE;
	return m_data->error;
}
