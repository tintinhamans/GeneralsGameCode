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
#include <limits>

OpenALDevice::Context::~Context()
{
    if (!onOwnerThread())
        std::terminate();
    if (context != nullptr) {
        if (getCurrent != nullptr && getCurrent() == context)
            setCurrent(nullptr);
        alcDestroyContext(context);
    }
    if (device != nullptr)
        alcCloseDevice(device);
}

OpenALDevice::Scope::Scope(Context &value) : context(value)
{
    if (!context.onOwnerThread())
        return;
    previous = context.getCurrent();
    valid = context.setCurrent(context.context) == ALC_TRUE;
}

OpenALDevice::Scope::~Scope()
{
    if (valid)
        context.setCurrent(previous);
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
    return m_context != nullptr && m_context->onOwnerThread();
}

bool OpenALDevice::open(int sampleRate, const char *name)
{
    if (!close())
        return false;
    if (sampleRate <= 0) {
        m_error = ALC_INVALID_VALUE;
        return false;
    }
    return initialize(alcOpenDevice(name), sampleRate, false);
}

bool OpenALDevice::openLoopback(int sampleRate)
{
    if (!close())
        return false;
    if (sampleRate <= 0) {
        m_error = ALC_INVALID_VALUE;
        return false;
    }
    auto openLoopback = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
    if (openLoopback == nullptr) {
        m_error = ALC_INVALID_DEVICE;
        return false;
    }
    return initialize(openLoopback(nullptr), sampleRate, true);
}

bool OpenALDevice::initialize(ALCdevice *device, int sampleRate, bool loopback)
{
    m_error = ALC_NO_ERROR;
    if (device == nullptr) {
        m_error = ALC_INVALID_DEVICE;
        return false;
    }
    std::shared_ptr<Context> candidate;
    try {
        candidate = std::make_shared<Context>();
    } catch (const std::bad_alloc &) {
        alcCloseDevice(device);
        m_error = ALC_OUT_OF_MEMORY;
        return false;
    }
    candidate->device = device;
    if (!alcIsExtensionPresent(device, "ALC_EXT_thread_local_context")) {
        m_error = ALC_INVALID_CONTEXT;
        return false;
    }
    candidate->setCurrent = reinterpret_cast<PFNALCSETTHREADCONTEXTPROC>(alcGetProcAddress(device, "alcSetThreadContext"));
    candidate->getCurrent = reinterpret_cast<PFNALCGETTHREADCONTEXTPROC>(alcGetProcAddress(device, "alcGetThreadContext"));
    if (candidate->setCurrent == nullptr || candidate->getCurrent == nullptr) {
        m_error = ALC_INVALID_CONTEXT;
        return false;
    }
    const ALCint playbackAttributes[] = { ALC_FREQUENCY, sampleRate, 0 };
    const ALCint loopbackAttributes[] = { ALC_FREQUENCY, sampleRate,
        ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT, 0 };
    const ALCint *attributes = playbackAttributes;
    if (loopback)
        attributes = loopbackAttributes;
    candidate->context = alcCreateContext(device, attributes);
    if (candidate->context == nullptr) {
        m_error = alcGetError(device);
        return false;
    }
    Scope scope(*candidate);
    if (!scope.valid || !alIsExtensionPresent("AL_SOFT_callback_buffer")) {
        m_error = ALC_INVALID_CONTEXT;
        return false;
    }
    candidate->bufferCallback = reinterpret_cast<LPALBUFFERCALLBACKSOFT>(alGetProcAddress("alBufferCallbackSOFT"));
    if (candidate->bufferCallback == nullptr) {
        m_error = ALC_INVALID_CONTEXT;
        return false;
    }
    if (loopback) {
        candidate->render = reinterpret_cast<LPALCRENDERSAMPLESSOFT>(alcGetProcAddress(device, "alcRenderSamplesSOFT"));
        if (candidate->render == nullptr) {
            m_error = ALC_INVALID_DEVICE;
            return false;
        }
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
    Scope scope(*m_context);
    if (!scope.valid)
        return false;
    alListener3f(AL_POSITION, x, y, z);
    alListenerfv(AL_ORIENTATION, orientation);
    return alGetError() == AL_NO_ERROR;
}

bool OpenALDevice::render(short *stereo, int frames)
{
    if (!isOpen() || m_context->render == nullptr || stereo == nullptr ||
        frames <= 0 || frames > std::numeric_limits<int>::max() / 2)
        return false;
    m_context->render(m_context->device, stereo, frames);
    m_error = alcGetError(m_context->device);
    return m_error == ALC_NO_ERROR;
}
