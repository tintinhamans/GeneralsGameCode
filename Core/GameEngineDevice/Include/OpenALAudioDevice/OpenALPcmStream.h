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

#pragma once

#include "OpenALAudioDevice/OpenALDevice.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class OpenALPcmStream
{
	struct ProducerState;
public:
	enum class InputMode { Queued, CachedSpans };
	enum class QueueResult { Accepted, Full, Invalid, Error };
	enum class State { Stopped, Playing, Paused, Starved, Finished, Error, Disconnected };
	class Producer
	{
	public:
		Producer(const Producer &) = delete;
		Producer &operator=(const Producer &) = delete;
		QueueResult queue(const short *interleaved, uint32_t frames);
		uint32_t availableFrames() const;
	private:
		friend class OpenALPcmStream;
		explicit Producer(std::shared_ptr<ProducerState> state);
		std::shared_ptr<ProducerState> m_state;
	};
	OpenALPcmStream();
	~OpenALPcmStream();
	OpenALPcmStream(const OpenALPcmStream &) = delete;
	OpenALPcmStream &operator=(const OpenALPcmStream &) = delete;
	bool open(OpenALDevice &device, int sampleRate, int channels, uint32_t capacityFrames, bool stereoMono = false, InputMode mode = InputMode::Queued);
	bool close();
	// Only queued input takes a producer. Release it before EOF and before queueing on the owner thread again.
	std::shared_ptr<Producer> acquireProducer();
	void releaseProducer();
	QueueResult queue(const short *interleaved, uint32_t frames);
	// Cached storage is immutable and its references are released only on the owner thread.
	// A stereo stream also takes mono spans (sourceChannels 1) and plays them on both channels. A sourceChannels of 0 means the stream's channels.
	QueueResult queueCached(std::shared_ptr<const std::vector<short>> pcm, uint32_t offsetFrames, uint32_t frames, int sourceChannels = 0);
	void reclaimConsumed();
	// Free capacity of the copied ring, independent of queued cached spans.
	uint32_t availableFrames() const;
	// Source frames the callback has read, mixer prefetch included. This is not a presentation clock.
	uint32_t getReadFrames() const;
	// Publishes a finite immutable tail after a source frame cutoff. Normal queueing ends there.
	bool replaceQueuedAfter(uint32_t endFrame, const short *tail, uint32_t frames, int sourceChannels = 0);
	// Restores playback intent after the owning device is serviced.
	void update();
	bool play();
	bool pause();
	bool stop();
	bool markEnd();
	bool setGain(float gain);
	bool setPitch(float pitch);
	// Selects linear interpolation, the quality Miles used for sound effects. Returns false when the device has no such resampler.
	bool useLinearResampler();
	bool setStereoMix(float leftGain, float rightGain, float frontBack, bool surround = false);
	// Publish before the PCM of the new portion. The callback resets its one sample delay there.
	bool schedulePortionBoundary(uint32_t sourceFrame);
	State state() const;
	int getLastError() const;

private:
	struct Data;
	std::unique_ptr<Data> m_data;
};
