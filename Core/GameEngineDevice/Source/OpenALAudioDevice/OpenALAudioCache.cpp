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
        m_bytes -= oldest->second.clip->samples.capacity() * sizeof(short);
        m_files.erase(oldest);
    }
    return true;
}

namespace {
std::shared_ptr<OpenALAudioClip> decodeClip(const AsciiString &filename, int rate, int channels,
    const std::function<bool(size_t)> &reserve, const std::function<size_t()> &available)
{
    if (TheFileSystem == nullptr)
        return nullptr;
    auto clip = std::make_shared<OpenALAudioClip>();
    clip->sampleRate = rate;
    clip->channels = channels;
    FFmpegAudioFile decoder;
    if (!decoder.open(TheFileSystem->openFile(filename.str()), rate, channels))
        return nullptr;
    bool valid = true;
    decoder.setPcmCallback([&](const short *pcm, int frames) {
        const size_t count = static_cast<size_t>(frames) * channels;
        if (!valid || count > std::numeric_limits<size_t>::max() / sizeof(short) - clip->samples.size()) {
            valid = false;
            return;
        }
        const size_t required = clip->samples.size() + count;
        if (required > clip->samples.capacity()) {
            const size_t maxSamples = std::numeric_limits<size_t>::max() / sizeof(short);
            size_t proposed = maxSamples;
            if (clip->samples.capacity() <= maxSamples / 2)
                proposed = clip->samples.capacity() * 2;
            const size_t headroom = available() / sizeof(short);
            proposed = std::max(required, std::min(proposed, headroom));
            if (!reserve(proposed * sizeof(short))) {
                proposed = required;
                if (!reserve(required * sizeof(short))) {
                    valid = false;
                    return;
                }
            }
            clip->samples.reserve(proposed);
            if (!reserve(clip->samples.capacity() * sizeof(short))) {
                valid = false;
                return;
            }
        }
        clip->samples.insert(clip->samples.end(), pcm, pcm + count);
    });
    while (valid && decoder.decodePacket()) {}
    if (!valid || decoder.hasError() || clip->samples.empty())
        return nullptr;
    return clip;
}
}

std::shared_ptr<const OpenALAudioClip> OpenALAudioCache::load(const AsciiString &filename, int rate, int channels)
{
    const std::string key = std::string(filename.str()) + "|" + std::to_string(rate) + "|" + std::to_string(channels);
    auto found = m_files.find(key);
    if (found != m_files.end()) {
        found->second.accessed = ++m_clock;
        return found->second.clip;
    }
    if (m_budget == 0)
        return nullptr;
    auto clip = decodeClip(filename, rate, channels,
        [this](size_t bytes) { return reserve(bytes); }, [this]() {
            if (m_bytes >= m_budget)
                return size_t(0);
            return m_budget - m_bytes;
        });
    if (clip == nullptr)
        return nullptr;
    m_bytes += clip->samples.capacity() * sizeof(short);
    m_files.emplace(key, Entry{clip, ++m_clock});
    return clip;
}

std::shared_ptr<const OpenALAudioClip> OpenALAudioCache::loadUncached(const AsciiString &filename, int rate, int channels, size_t budget)
{
    return decodeClip(filename, rate, channels, [budget](size_t bytes) { return bytes <= budget; },
        [budget]() { return budget; });
}
