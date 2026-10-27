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
#include <cstdint>
#include <memory>

class OpenALPcmStream
{
public:
    enum class QueueResult { Accepted, Full, Invalid, Error };
    enum class State { Stopped, Playing, Paused, Starved, Finished, Error };
    OpenALPcmStream();
    ~OpenALPcmStream();
    OpenALPcmStream(const OpenALPcmStream &) = delete;
    OpenALPcmStream &operator=(const OpenALPcmStream &) = delete;
    bool open(OpenALDevice &device, int sampleRate, int channels, uint32_t capacityFrames, bool stereoMono = false);
    bool close();
    QueueResult queue(const short *interleaved, uint32_t frames);
    // Float input preserves filtered mono headroom until the final device mix.
    QueueResult queueFloat(const float *interleaved, uint32_t frames);
    uint32_t availableFrames() const;
    // Callback-read source frames; includes mixer prefetch, not a presentation clock.
    uint32_t getReadFrames() const;
    // Publish a finite immutable tail after a source-frame cutoff; normal queueing then ends.
    bool replaceQueuedAfter(uint32_t endFrame, const short *tail, uint32_t frames);
    bool replaceQueuedAfterFloat(uint32_t endFrame, const float *tail, uint32_t frames);
    bool play();
    bool pause();
    bool stop();
    bool markEnd();
    bool setGain(float gain);
    bool setPitch(float pitch);
    bool setStereoMix(float leftGain, float rightGain, float frontBack, bool surround = false);
    // Publish before the new portion PCM; the callback resets its one-sample delay here.
    bool schedulePortionBoundary(uint32_t sourceFrame);
    bool setPosition(float x, float y, float z, bool positional);
    State state() const;
    int getLastError() const;

private:
    QueueResult queueData(const void *interleaved, uint32_t frames, bool floatInput);
    bool replaceQueuedDataAfter(uint32_t endFrame, const void *tail, uint32_t frames, bool floatInput);
    struct Data;
    std::unique_ptr<Data> m_data;
};
