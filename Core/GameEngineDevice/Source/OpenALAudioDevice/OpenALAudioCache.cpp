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
#include "OpenALAudioDevice/OpenALPcmStream.h"
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
    if (filteredSamples != nullptr)
        return filteredSamples->size() / channels;
    return samples.size() / channels;
}

size_t OpenALAudioClip::byteSize() const
{
    size_t bytes = samples.capacity() * sizeof(short);
    if (filteredSamples != nullptr)
        bytes += filteredSamples->capacity() * sizeof(float);
    return bytes;
}

void OpenALAudioCache::clear()
{
    m_files.clear();
    m_bytes = 0;
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
    while (m_bytes > m_budget - bytes) {
        auto oldest = m_files.end();
        for (auto it = m_files.begin(); it != m_files.end(); ++it) {
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

namespace {
std::shared_ptr<OpenALAudioClip> decodeClip(const AsciiString &filename, int rate, int channels, float normalizedCutoff,
    const std::function<bool(size_t)> &reserve, const std::function<size_t()> &available)
{
    if (TheFileSystem == nullptr)
        return nullptr;
    auto clip = std::make_shared<OpenALAudioClip>();
    clip->sampleRate = rate;
    clip->channels = channels;
    FFmpegAudioFile decoder;
    if (!decoder.open(TheFileSystem->openFile(filename.str()), rate, channels, normalizedCutoff))
        return nullptr;
    bool valid = true;
    std::vector<float> filteredSamples;
    auto append = [&](const auto *pcm, int frames, auto &samples) {
        const size_t sampleBytes = sizeof(*pcm);
        const size_t count = static_cast<size_t>(frames) * channels;
        if (!valid || count > std::numeric_limits<size_t>::max() / sampleBytes - samples.size()) {
            valid = false;
            return;
        }
        const size_t required = samples.size() + count;
        if (required > samples.capacity()) {
            const size_t maxSamples = std::numeric_limits<size_t>::max() / sampleBytes;
            size_t proposed = maxSamples;
            if (samples.capacity() <= maxSamples / 2)
                proposed = samples.capacity() * 2;
            const size_t headroom = available() / sampleBytes;
            proposed = std::max(required, std::min(proposed, headroom));
            if (!reserve(proposed * sampleBytes)) {
                proposed = required;
                if (!reserve(required * sampleBytes)) {
                    valid = false;
                    return;
                }
            }
            samples.reserve(proposed);
            if (!reserve(samples.capacity() * sampleBytes)) {
                valid = false;
                return;
            }
        }
        samples.insert(samples.end(), pcm, pcm + count);
    };
    if (normalizedCutoff > 0.0f && normalizedCutoff < 0.999f) {
        if (!decoder.setFloatPcmCallback([&](const float *pcm, int frames) {
                append(pcm, frames, filteredSamples);
            }))
            return nullptr;
    } else {
        decoder.setPcmCallback([&](const short *pcm, int frames) {
            append(pcm, frames, clip->samples);
        });
    }
    while (valid && decoder.decodePacket()) {}
    if (!valid || decoder.hasError())
        return nullptr;
    if (!filteredSamples.empty()) {
        clip->filteredSamples = OpenALCachedFloatPcm::create(std::move(filteredSamples));
        if (clip->filteredSamples == nullptr)
            return nullptr;
    }
    if (clip->frameCount() == 0)
        return nullptr;
    return clip;
}
}

std::shared_ptr<const OpenALAudioClip> OpenALAudioCache::load(const AsciiString &filename, int rate, int channels, float normalizedCutoff)
{
    if (!std::isfinite(normalizedCutoff))
        return nullptr;
    if (normalizedCutoff <= 0.0f || normalizedCutoff >= 0.999f)
        normalizedCutoff = 1.0f;
    const auto key = std::make_tuple(std::string(filename.str()), rate, channels, normalizedCutoff);
    auto found = m_files.find(key);
    if (found != m_files.end()) {
        found->second.accessed = ++m_clock;
        return found->second.clip;
    }
    if (m_budget == 0)
        return nullptr;
    auto clip = decodeClip(filename, rate, channels, normalizedCutoff,
        [this](size_t bytes) { return reserve(bytes); }, [this]() {
            if (m_bytes >= m_budget)
                return size_t(0);
            return m_budget - m_bytes;
        });
    if (clip == nullptr)
        return nullptr;
    m_bytes += clip->byteSize();
    m_files.emplace(key, Entry{clip, ++m_clock});
    return clip;
}

std::shared_ptr<const OpenALAudioClip> OpenALAudioCache::loadUncached(const AsciiString &filename, int rate, int channels, size_t budget, float normalizedCutoff)
{
    if (!std::isfinite(normalizedCutoff))
        return nullptr;
    return decodeClip(filename, rate, channels, normalizedCutoff, [budget](size_t bytes) { return bytes <= budget; },
        [budget]() { return budget; });
}
