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
#include <cstdint>
#include <memory>
class OpenALDevice;
class OpenALAudioManager;

class OpenALMovieVoice
{
public:
    ~OpenALMovieVoice();
    void update();
    bool connected() const;
    uint64_t recoveryEpoch() const;
    bool queue(const short *pcm, uint32_t frames);
    uint32_t availableFrames() const;
    uint64_t readFrames();
    int sampleRate() const;
    bool play();
    bool pause();
    bool end();
    bool finished() const;
    bool failed() const;
    bool setGain(float gain);
    bool reset();
    void close();
private:
    friend class OpenALAudioManager;
    OpenALMovieVoice(OpenALDevice &device, int rate);
    struct State;
    std::unique_ptr<State> m_state;
};
