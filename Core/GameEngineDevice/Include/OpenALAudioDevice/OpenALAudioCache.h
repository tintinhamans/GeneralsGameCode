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

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
class AsciiString;
class OpenALCachedFloatPcm;

struct OpenALAudioClip
{
    std::vector<short> samples;
    std::shared_ptr<const OpenALCachedFloatPcm> filteredSamples;
    int sampleRate = 0;
    int channels = 0;
    size_t frameCount() const;
    size_t byteSize() const;
};

class OpenALAudioCache
{
public:
    explicit OpenALAudioCache(size_t budget = 0) : m_budget(budget) {}
    void setBudget(size_t budget);
    void clear();
    std::shared_ptr<const OpenALAudioClip> load(const AsciiString &filename, int rate, int channels, float normalizedCutoff = 1.0f);
    static std::shared_ptr<const OpenALAudioClip> loadUncached(const AsciiString &filename, int rate, int channels, size_t budget, float normalizedCutoff = 1.0f);
    size_t usedBytes() const { return m_bytes; }
private:
    struct Entry
    {
        std::shared_ptr<const OpenALAudioClip> clip;
        uint64_t accessed = 0;
    };
    bool reserve(size_t bytes);
    using Key = std::tuple<std::string, int, int, float>;
    std::map<Key, Entry> m_files;
    size_t m_budget = 0;
    size_t m_bytes = 0;
    uint64_t m_clock = 0;
};
