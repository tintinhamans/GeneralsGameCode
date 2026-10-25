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

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <memory>
#include <thread>

class OpenALPcmStream;

// Controls, queueing and destruction belong to the thread that opens the device.
class OpenALDevice
{
public:
    OpenALDevice() = default;
    ~OpenALDevice();
    OpenALDevice(const OpenALDevice &) = delete;
    OpenALDevice &operator=(const OpenALDevice &) = delete;
    bool open(int sampleRate = 48000);
    bool close();
    bool isOpen() const;
    int getLastError() const { return m_error; }
    bool setListener(float x, float y, float z, const float *forward, const float *up);

private:
    friend class OpenALPcmStream;
    struct Context
    {
        ALCdevice *device = nullptr;
        ALCcontext *context = nullptr;
        LPALBUFFERCALLBACKSOFT bufferCallback = nullptr;
        std::thread::id owner = std::this_thread::get_id();
        ~Context();
        bool onOwnerThread() const { return owner == std::this_thread::get_id(); }
    };
    bool initialize(ALCdevice *device, int sampleRate);
    std::shared_ptr<Context> m_context;
    int m_error = 0;
};
