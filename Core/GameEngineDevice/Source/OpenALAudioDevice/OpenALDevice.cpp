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

#include "OpenALAudioDevice/OpenALDevice.h"
#include <exception>
#include <cmath>

OpenALDevice::Context::~Context()
{
    if (!onOwnerThread())
        std::terminate();
    if (context != nullptr) {
        if (alcGetCurrentContext() == context)
            alcMakeContextCurrent(nullptr);
        alcDestroyContext(context);
    }
    if (device != nullptr)
        alcCloseDevice(device);
}

OpenALDevice::~OpenALDevice()
{
    if (!close())
        std::terminate();
}

bool OpenALDevice::close()
{
    if (m_context != nullptr && !m_context->onOwnerThread())
        return false;
    m_context.reset();
    return true;
}

bool OpenALDevice::isOpen() const
{
    return m_context != nullptr;
}

bool OpenALDevice::open(int sampleRate)
{
    if (!close())
        return false;
    if (sampleRate <= 0) {
        m_error = ALC_INVALID_VALUE;
        return false;
    }
    return initialize(alcOpenDevice(nullptr), sampleRate);
}

bool OpenALDevice::initialize(ALCdevice *device, int sampleRate)
{
    m_error = ALC_NO_ERROR;
    if (device == nullptr) {
        m_error = ALC_INVALID_DEVICE;
        return false;
    }
    auto candidate = std::make_shared<Context>();
    candidate->device = device;
    const ALCint attributes[] = { ALC_FREQUENCY, sampleRate, 0 };
    candidate->context = alcCreateContext(device, attributes);
    if (candidate->context == nullptr) {
        m_error = alcGetError(device);
        return false;
    }
    if (alcMakeContextCurrent(candidate->context) != ALC_TRUE || !alIsExtensionPresent("AL_SOFT_callback_buffer")) {
        m_error = ALC_INVALID_CONTEXT;
        return false;
    }
    candidate->bufferCallback = reinterpret_cast<LPALBUFFERCALLBACKSOFT>(alGetProcAddress("alBufferCallbackSOFT"));
    if (candidate->bufferCallback == nullptr) {
        m_error = ALC_INVALID_CONTEXT;
        return false;
    }
    m_context = candidate;
    alDopplerFactor(0.0f);
    return true;
}

bool OpenALDevice::setListener(float x, float y, float z, const float *forward, const float *up)
{
    if (!isOpen() || forward == nullptr || up == nullptr ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    float orientation[6];
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(forward[i]) || !std::isfinite(up[i]))
            return false;
        orientation[i] = forward[i];
        orientation[i + 3] = up[i];
    }
    alListener3f(AL_POSITION, x, y, z);
    alListenerfv(AL_ORIENTATION, orientation);
    return alGetError() == AL_NO_ERROR;
}
