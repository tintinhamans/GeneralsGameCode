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
#include <vector>

namespace {
std::vector<ALCint> outputAttributes(int sampleRate)
{
    // Basic stereo with HRTF off keeps a headphone endpoint from switching HRTF on behind the measured Miles mix.
    std::vector<ALCint> attributes = {ALC_FREQUENCY, sampleRate,
        ALC_OUTPUT_MODE_SOFT, ALC_STEREO_BASIC_SOFT, ALC_HRTF_SOFT, ALC_FALSE};
    attributes.push_back(0);
    return attributes;
}
}

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
    if (!alcIsExtensionPresent(device, "ALC_SOFT_output_mode") || !alcIsExtensionPresent(device, "ALC_SOFT_HRTF")) {
        m_error = ALC_INVALID_VALUE;
        return false;
    }
    candidate->sampleRate = sampleRate;
    const auto attributes = outputAttributes(sampleRate);
    candidate->context = alcCreateContext(device, attributes.data());
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

ALCint OpenALDevice::outputMode() const
{
    ALCint value = ALC_ANY_SOFT;
    if (isOpen())
        alcGetIntegerv(m_context->device, ALC_OUTPUT_MODE_SOFT, 1, &value);
    return value;
}

bool OpenALDevice::isStereoOutput() const
{
    return outputMode() == ALC_STEREO_BASIC_SOFT;
}
