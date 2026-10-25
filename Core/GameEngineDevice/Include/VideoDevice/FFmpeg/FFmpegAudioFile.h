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

#include "VideoDevice/FFmpeg/FFmpegFile.h"
#include <memory>

// Samples are interleaved signed 16-bit PCM, borrowed for the callback duration.
using FFmpegPcmCallback = std::function<void(const short *, Int)>;

class FFmpegAudioFile
{
public:
    FFmpegAudioFile();
    ~FFmpegAudioFile();
    FFmpegAudioFile(const FFmpegAudioFile &) = delete;
    FFmpegAudioFile &operator=(const FFmpegAudioFile &) = delete;

    Bool open(File *file, Int sampleRate, Int channels);
    void close();
    Bool decodePacket();
    Bool rewind();
    Bool hasError() const;
    void setPcmCallback(FFmpegPcmCallback callback);
    Int getSampleRate() const;
    Int getNumChannels() const;
    Int64 getDecodedSampleCount() const;
    // Duration of emitted PCM, final only after successful EOF/drain.
    double getDecodedDurationMS() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};
