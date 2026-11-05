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
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

class OpenALPcmStream;

// Controls, queueing and destruction belong to the thread that opens the device.
class OpenALDevice
{
public:
    enum class OutputMode { Unknown, Stereo, Headphones, SurroundStereo, Quad, Surround51, Surround71 };
    OpenALDevice() = default;
    ~OpenALDevice();
    OpenALDevice(const OpenALDevice &) = delete;
    OpenALDevice &operator=(const OpenALDevice &) = delete;
    bool open(int sampleRate = 48000, const char *name = nullptr, OutputMode mode = OutputMode::Stereo);
    bool openLoopback(int sampleRate = 48000, OutputMode mode = OutputMode::Stereo);
    bool setOutputMode(OutputMode mode);
    OutputMode getOutputMode() const;
    bool close();
    bool isOpen() const;
    bool isConnected() const;
    // Owner thread: service once per tick, before updating this device's streams.
    bool update();
    uint64_t recoveryEpoch() const;
    int getLastError() const { return m_error; }
    bool render(short *stereo, int frames);
    bool setListener(float x, float y, float z, const float *forward, const float *up);

private:
    friend class OpenALPcmStream;
    struct Context
    {
        ALCdevice *device = nullptr;
        ALCcontext *context = nullptr;
        PFNALCSETTHREADCONTEXTPROC setCurrent = nullptr;
        PFNALCGETTHREADCONTEXTPROC getCurrent = nullptr;
        LPALBUFFERCALLBACKSOFT bufferCallback = nullptr;
        LPALCRENDERSAMPLESSOFT render = nullptr;
        LPALCRESETDEVICESOFT reset = nullptr;
        int sampleRate = 0;
        bool loopback = false;
        bool disconnectSupported = false;
        bool deviceConnected = true;
        bool systemEvents = false;
        unsigned availabilityGeneration = 0;
        unsigned removalGeneration = 0;
        OutputMode outputMode = OutputMode::Unknown;
        LPALCREOPENDEVICESOFT reopen = nullptr;
        std::string outputName;
        std::chrono::steady_clock::time_point nextRetry;
        std::chrono::steady_clock::time_point retryDeadline;
        std::chrono::steady_clock::time_point nextCheck;
        uint64_t recoveryEpoch = 0;
        bool connected() const { return deviceConnected; }
        std::thread::id owner = std::this_thread::get_id();
        ~Context();
        bool onOwnerThread() const { return owner == std::this_thread::get_id(); }
    };
    struct Scope
    {
        Context &context;
        ALCcontext *previous = nullptr;
        bool valid = false;
        explicit Scope(Context &value);
        ~Scope();
    };
    bool initialize(ALCdevice *device, int sampleRate, bool loopback, OutputMode mode);
    std::shared_ptr<Context> m_context;
    int m_error = 0;
};
