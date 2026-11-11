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

#pragma once

#include "OpenALAudioDevice/OpenALPcmStream.h"
#include "VideoDevice/FFmpeg/FFmpegPcmConverter.h"
#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

class FFmpegAudioFile;

// Appends decoded PCM to pcm up to maxFrames and raises failed when a packet does not fit.
template<typename Flag>
FFmpegPcmCallback appendDecodedPcm(std::vector<short> &pcm, int channels, size_t maxFrames, Flag &failed)
{
	pcm.reserve(maxFrames * channels);
	return [&pcm, &failed, channels, maxFrames](const short *samples, Int frames)
	{
		const size_t count = static_cast<size_t>(frames) * channels;
		if (failed || count > maxFrames * channels - pcm.size())
		{
			failed = true;
			return;
		}
		pcm.insert(pcm.end(), samples, samples + count);
	};
}

class OpenALAudioProducer
{
	struct State;
	struct Work;
public:
	static constexpr std::size_t PacketCapacity = 4096;
	struct Packet
	{
		uint64_t endFrame = 0;
		bool eof = false;
	};
	class Job
	{
	public:
		~Job();
		Job(const Job &) = delete;
		Job &operator=(const Job &) = delete;
		bool failed() const;
		uint64_t producedFrames() const;
		bool peekPacket(Packet &packet) const;
		void consumePacket();
		void prime();
	private:
		friend class OpenALAudioProducer;
		Job(std::shared_ptr<State> service, std::shared_ptr<Work> work, std::unique_ptr<FFmpegAudioFile> decoder);
		std::shared_ptr<State> m_service;
		std::shared_ptr<Work> m_work;
		std::unique_ptr<FFmpegAudioFile> m_decoder;
	};
	OpenALAudioProducer();
	~OpenALAudioProducer();
	OpenALAudioProducer(const OpenALAudioProducer &) = delete;
	OpenALAudioProducer &operator=(const OpenALAudioProducer &) = delete;
	std::unique_ptr<Job> add(std::unique_ptr<FFmpegAudioFile> decoder,
		std::shared_ptr<OpenALPcmStream::Producer> output, int channels);
private:
	std::shared_ptr<State> m_state;
};
