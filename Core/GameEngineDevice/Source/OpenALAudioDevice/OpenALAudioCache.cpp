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

#include "OpenALAudioDevice/OpenALAudioCache.h"
#include "VideoDevice/FFmpeg/FFmpegAudioFile.h"
#include "Common/AsciiString.h"
#include "Common/FileSystem.h"
#include <limits>
#include <algorithm>
#include <functional>
#include <cmath>

size_t OpenALAudioClip::frameCount() const
{
	if (channels <= 0)
		return 0;
	return samples.size() / channels;
}

size_t OpenALAudioClip::byteSize() const
{
	return samples.capacity() * sizeof(short);
}

void OpenALAudioCache::clear()
{
	m_files.clear();
	m_formats.clear();
	m_bytes = 0;
}

// AudioFootprintInBytes was sized for Miles, which caches the files as stored. Retail files decoded at their own rate are 1.4 times bigger, ADPCM files 4 times.
size_t OpenALAudioCache::budgetForFootprint(size_t footprint)
{
	constexpr size_t DECODED_SCALE = 2;
	constexpr size_t MAX_SCALED_BUDGET = 128 * 1024 * 1024;
	if (footprint >= MAX_SCALED_BUDGET / DECODED_SCALE)
		return std::max(footprint, MAX_SCALED_BUDGET);
	return footprint * DECODED_SCALE;
}

void OpenALAudioCache::setBudget(size_t budget)
{
	m_budget = budget;
	reserve(0);
}

bool OpenALAudioCache::reserve(size_t bytes)
{
	if (bytes > m_budget)
		return false;
	while (m_bytes > m_budget - bytes)
	{
		auto oldest = m_files.end();
		for (auto it = m_files.begin(); it != m_files.end(); ++it)
		{
			if (it->second.clip.use_count() != 1)
				continue;
			if (oldest == m_files.end() || it->second.accessed < oldest->second.accessed)
				oldest = it;
		}
		if (oldest == m_files.end())
			return false;
		m_bytes -= oldest->second.clip->byteSize();
		m_files.erase(oldest);
	}
	return true;
}

namespace
{
std::shared_ptr<OpenALAudioClip> decodeClip(const AsciiString &filename, int rate, int channels, float normalizedCutoff,
	const std::function<bool(size_t)> &reserve, const std::function<size_t()> &available, std::pair<int, int> *sourceFormat = nullptr)
{
	if (TheFileSystem == nullptr)
		return nullptr;
	auto clip = std::make_shared<OpenALAudioClip>();
	FFmpegAudioFile decoder;
	if (!decoder.open(TheFileSystem->openFile(filename.str()), rate, channels, normalizedCutoff))
		return nullptr;
	if (sourceFormat != nullptr)
		*sourceFormat = std::make_pair(decoder.getSourceSampleRate(), decoder.getSourceNumChannels());
	clip->sampleRate = decoder.getSampleRate();
	clip->channels = decoder.getNumChannels();
	bool valid = true;
	auto &samples = clip->samples;
	decoder.setPcmCallback([&](const short *pcm, int frames)
	{
		const size_t sampleBytes = sizeof(short);
		const size_t count = static_cast<size_t>(frames) * clip->channels;
		if (!valid || count > std::numeric_limits<size_t>::max() / sampleBytes - samples.size())
		{
			valid = false;
			return;
		}
		const size_t required = samples.size() + count;
		if (required > samples.capacity())
		{
			const size_t maxSamples = std::numeric_limits<size_t>::max() / sampleBytes;
			size_t proposed = maxSamples;
			if (samples.capacity() <= maxSamples / 2)
				proposed = samples.capacity() * 2;
			const size_t headroom = available() / sampleBytes;
			proposed = std::max(required, std::min(proposed, headroom));
			if (!reserve(proposed * sampleBytes))
			{
				proposed = required;
				if (!reserve(required * sampleBytes))
				{
					valid = false;
					return;
				}
			}
			samples.reserve(proposed);
			if (!reserve(samples.capacity() * sampleBytes))
			{
				valid = false;
				return;
			}
		}
		samples.insert(samples.end(), pcm, pcm + count);
	});
	while (valid && decoder.decodePacket())
	{
	}
	if (!valid || decoder.hasError())
		return nullptr;
	if (clip->frameCount() == 0)
		return nullptr;
	samples.shrink_to_fit();
	return clip;
}
}

std::shared_ptr<const OpenALAudioClip> OpenALAudioCache::load(const AsciiString &filename, int rate, int channels, float normalizedCutoff)
{
	if (!std::isfinite(normalizedCutoff))
		return nullptr;
	if (normalizedCutoff <= 0.0f || normalizedCutoff >= 0.999f)
		normalizedCutoff = 1.0f;
	const std::string name(filename.str());
	// The file's own rate and channel count, 0 until any request has opened it.
	auto &format = m_formats[name];
	if (rate <= 0)
		rate = format.first;
	if (channels <= 0)
		channels = format.second;
	if (rate > 0 && channels > 0)
	{
		auto found = m_files.find(std::make_tuple(name, rate, channels, normalizedCutoff));
		if (found != m_files.end())
		{
			found->second.accessed = ++m_clock;
			return found->second.clip;
		}
	}
	if (m_budget == 0)
		return nullptr;
	auto clip = decodeClip(filename, rate, channels, normalizedCutoff,
		[this](size_t bytes)
		{
			return reserve(bytes);
		},
		[this]()
		{
			if (m_bytes >= m_budget)
				return size_t(0);
			return m_budget - m_bytes;
		}, &format);
	if (clip == nullptr)
		return nullptr;
	const auto key = std::make_tuple(name, clip->sampleRate, clip->channels, normalizedCutoff);
	auto found = m_files.find(key);
	if (found != m_files.end())
	{
		found->second.accessed = ++m_clock;
		return found->second.clip;
	}
	m_bytes += clip->byteSize();
	m_files.emplace(key, Entry{clip, ++m_clock});
	return clip;
}

std::shared_ptr<const OpenALAudioClip> OpenALAudioCache::loadUncached(const AsciiString &filename, int rate, int channels, size_t budget, float normalizedCutoff)
{
	if (!std::isfinite(normalizedCutoff))
		return nullptr;
	return decodeClip(filename, rate, channels, normalizedCutoff,
		[budget](size_t bytes)
		{
			return bytes <= budget;
		},
		[budget]()
		{
			return budget;
		});
}
