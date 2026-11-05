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
#include <atomic>
#include <mutex>
#include <cmath>
#include <limits>
#include <vector>

namespace {
static_assert(std::atomic<unsigned>::is_always_lock_free, "Audio notification counters must not block");

struct PlaybackEvents
{
    std::mutex mutex;
    unsigned users = 0;
    std::atomic<unsigned> added{0};
    std::atomic<unsigned> removed{0};
    LPALCEVENTCALLBACKSOFT setCallback = nullptr;
    LPALCEVENTCONTROLSOFT control = nullptr;
    std::vector<ALCenum> enabled;

    static void ALC_APIENTRY notify(ALCenum event, ALCenum type, ALCdevice *, ALCsizei,
        const ALCchar *, void *user) noexcept
    {
        if (type != ALC_PLAYBACK_DEVICE_SOFT)
            return;
        auto &events = *static_cast<PlaybackEvents *>(user);
        if (event == ALC_EVENT_TYPE_DEVICE_ADDED_SOFT)
            events.added.fetch_add(1, std::memory_order_relaxed);
        if (event == ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT)
            events.removed.fetch_add(1, std::memory_order_relaxed);
    }

    bool retain()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (users != 0) {
            ++users;
            return true;
        }
        if (!alcIsExtensionPresent(nullptr, "ALC_SOFT_system_events"))
            return false;
        auto supported = reinterpret_cast<LPALCEVENTISSUPPORTEDSOFT>(alcGetProcAddress(nullptr, "alcEventIsSupportedSOFT"));
        control = reinterpret_cast<LPALCEVENTCONTROLSOFT>(alcGetProcAddress(nullptr, "alcEventControlSOFT"));
        setCallback = reinterpret_cast<LPALCEVENTCALLBACKSOFT>(alcGetProcAddress(nullptr, "alcEventCallbackSOFT"));
        if (supported == nullptr || control == nullptr || setCallback == nullptr ||
            supported(ALC_EVENT_TYPE_DEVICE_ADDED_SOFT, ALC_PLAYBACK_DEVICE_SOFT) != ALC_EVENT_SUPPORTED_SOFT) {
            alcGetError(nullptr);
            return false;
        }
        enabled = {ALC_EVENT_TYPE_DEVICE_ADDED_SOFT};
        if (supported(ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT, ALC_PLAYBACK_DEVICE_SOFT) == ALC_EVENT_SUPPORTED_SOFT)
            enabled.push_back(ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT);
        setCallback(notify, this);
        if (!control(static_cast<ALCsizei>(enabled.size()), enabled.data(), ALC_TRUE)) {
            setCallback(nullptr, nullptr);
            enabled.clear();
            alcGetError(nullptr);
            return false;
        }
        users = 1;
        return true;
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (--users != 0)
            return;
        // The pinned OpenAL implementation drains callbacks before replacing them.
        setCallback(nullptr, nullptr);
        control(static_cast<ALCsizei>(enabled.size()), enabled.data(), ALC_FALSE);
        enabled.clear();
    }
};

PlaybackEvents &playbackEvents()
{
    static PlaybackEvents events;
    return events;
}

ALCenum outputModeValue(OpenALDevice::OutputMode mode)
{
    using Mode = OpenALDevice::OutputMode;
    switch (mode) {
    case Mode::Stereo: return ALC_STEREO_BASIC_SOFT;
    case Mode::Headphones: return ALC_STEREO_HRTF_SOFT;
    case Mode::SurroundStereo: return ALC_STEREO_UHJ_SOFT;
    case Mode::Quad: return ALC_QUAD_SOFT;
    case Mode::Surround51: return ALC_SURROUND_5_1_SOFT;
    case Mode::Surround71: return ALC_SURROUND_7_1_SOFT;
    default: return ALC_ANY_SOFT;
    }
}

std::vector<ALCint> outputAttributes(int sampleRate, bool loopback, OpenALDevice::OutputMode mode)
{
    std::vector<ALCint> attributes = {ALC_FREQUENCY, sampleRate,
        ALC_OUTPUT_MODE_SOFT, outputModeValue(mode), ALC_HRTF_SOFT, ALC_FALSE};
    if (mode == OpenALDevice::OutputMode::Headphones)
        attributes.back() = ALC_TRUE;
    if (loopback) {
        attributes.insert(attributes.end(), {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
            ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT});
    }
    attributes.push_back(0);
    return attributes;
}
}

OpenALDevice::Context::~Context()
{
    if (!onOwnerThread())
        std::terminate();
    if (systemEvents)
        playbackEvents().release();
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

bool OpenALDevice::isConnected() const
{
    return isOpen() && m_context->connected();
}

uint64_t OpenALDevice::recoveryEpoch() const
{
    if (!isOpen())
        return 0;
    return m_context->recoveryEpoch;
}

bool OpenALDevice::update()
{
    if (!isOpen())
        return false;
    auto &context = *m_context;
    if (context.loopback)
        return true;
    const auto now = std::chrono::steady_clock::now();
    bool available = false;
    bool removed = false;
    if (context.systemEvents) {
        auto &events = playbackEvents();
        const auto addedGeneration = events.added.load(std::memory_order_relaxed);
        const auto removedGeneration = events.removed.load(std::memory_order_relaxed);
        available = addedGeneration != context.availabilityGeneration;
        removed = removedGeneration != context.removalGeneration;
        context.availabilityGeneration = addedGeneration;
        context.removalGeneration = removedGeneration;
    }
    if (context.disconnectSupported && (removed || available || now >= context.nextCheck)) {
        context.nextCheck = now + std::chrono::milliseconds(100);
        ALCint connected = ALC_FALSE;
        alcGetIntegerv(context.device, ALC_CONNECTED, 1, &connected);
        if (connected != ALC_TRUE && context.deviceConnected) {
            context.deviceConnected = false;
            context.retryDeadline = now + std::chrono::seconds(8);
            context.nextRetry = now;
        }
    }
    if (context.connected())
        return true;
    if (context.systemEvents && available) {
        if (now >= context.retryDeadline)
            context.retryDeadline = now + std::chrono::seconds(8);
        context.nextRetry = now;
    }
    if (context.systemEvents && now >= context.retryDeadline)
        return false;
    if (context.reopen == nullptr || context.outputName.empty() || now < context.nextRetry)
        return false;
    alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
    const auto attributes = outputAttributes(context.sampleRate, false, context.outputMode);
    alcGetError(context.device);
    const bool reopened = context.reopen(context.device, context.outputName.c_str(), attributes.data()) == ALC_TRUE;
    context.nextRetry = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    if (!context.systemEvents)
        context.nextRetry = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    m_error = alcGetError(context.device);
    ALCint connected = ALC_FALSE;
    alcGetIntegerv(context.device, ALC_CONNECTED, 1, &connected);
    if (!reopened || connected != ALC_TRUE || getOutputMode() != context.outputMode) {
        if (m_error == ALC_NO_ERROR)
            m_error = ALC_INVALID_DEVICE;
        return false;
    }
    context.deviceConnected = true;
    context.nextRetry = {};
    ++context.recoveryEpoch;
    m_error = ALC_NO_ERROR;
    return true;
}

bool OpenALDevice::open(int sampleRate, const char *name, OutputMode mode)
{
    if (!close())
        return false;
    if (sampleRate <= 0) {
        m_error = ALC_INVALID_VALUE;
        return false;
    }
    return initialize(alcOpenDevice(name), sampleRate, false, mode);
}

bool OpenALDevice::openLoopback(int sampleRate, OutputMode mode)
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
    return initialize(openLoopback(nullptr), sampleRate, true, mode);
}

bool OpenALDevice::initialize(ALCdevice *device, int sampleRate, bool loopback, OutputMode mode)
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
    if ((loopback && mode != OutputMode::Stereo && mode != OutputMode::Headphones && mode != OutputMode::SurroundStereo) ||
        mode == OutputMode::Unknown || !alcIsExtensionPresent(device, "ALC_SOFT_output_mode") ||
        !alcIsExtensionPresent(device, "ALC_SOFT_HRTF")) {
        m_error = ALC_INVALID_VALUE;
        return false;
    }
    candidate->sampleRate = sampleRate;
    candidate->loopback = loopback;
    candidate->outputMode = mode;
    if (!loopback) {
        candidate->disconnectSupported = alcIsExtensionPresent(device, "ALC_EXT_disconnect") == ALC_TRUE;
        if (alcIsExtensionPresent(device, "ALC_SOFT_reopen_device"))
            candidate->reopen = reinterpret_cast<LPALCREOPENDEVICESOFT>(alcGetProcAddress(device, "alcReopenDeviceSOFT"));
        const char *name = alcGetString(device, ALC_ALL_DEVICES_SPECIFIER);
        if (name != nullptr)
            candidate->outputName = name;
    }
    candidate->reset = reinterpret_cast<LPALCRESETDEVICESOFT>(alcGetProcAddress(device, "alcResetDeviceSOFT"));
    const auto attributes = outputAttributes(sampleRate, loopback, mode);
    candidate->context = alcCreateContext(device, attributes.data());
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
    m_context->outputMode = getOutputMode();
    if (!loopback && candidate->disconnectSupported && candidate->reopen != nullptr) {
        candidate->systemEvents = playbackEvents().retain();
        candidate->availabilityGeneration = playbackEvents().added.load(std::memory_order_relaxed);
        candidate->removalGeneration = playbackEvents().removed.load(std::memory_order_relaxed);
    }
    alDopplerFactor(0.0f);
    return true;
}

OpenALDevice::OutputMode OpenALDevice::getOutputMode() const
{
    if (!isOpen())
        return OutputMode::Unknown;
    ALCint value = ALC_ANY_SOFT;
    alcGetIntegerv(m_context->device, ALC_OUTPUT_MODE_SOFT, 1, &value);
    switch (value) {
    case ALC_STEREO_BASIC_SOFT: return OutputMode::Stereo;
    case ALC_STEREO_HRTF_SOFT: return OutputMode::Headphones;
    case ALC_STEREO_UHJ_SOFT: return OutputMode::SurroundStereo;
    case ALC_QUAD_SOFT: return OutputMode::Quad;
    case ALC_SURROUND_5_1_SOFT: return OutputMode::Surround51;
    case ALC_SURROUND_7_1_SOFT: return OutputMode::Surround71;
    default: return OutputMode::Unknown;
    }
}

bool OpenALDevice::setOutputMode(OutputMode mode)
{
    if (!isOpen() || mode == OutputMode::Unknown || m_context->reset == nullptr) {
        m_error = ALC_INVALID_VALUE;
        return false;
    }
    if (m_context->loopback && mode != OutputMode::Stereo && mode != OutputMode::Headphones && mode != OutputMode::SurroundStereo) {
        m_error = ALC_INVALID_VALUE;
        return false;
    }
    if (getOutputMode() == mode) {
        m_context->outputMode = mode;
        m_error = ALC_NO_ERROR;
        return true;
    }
    const auto attributes = outputAttributes(m_context->sampleRate, m_context->loopback, mode);
    if (!m_context->reset(m_context->device, attributes.data())) {
        m_error = alcGetError(m_context->device);
        return false;
    }
    // A successful device reset does not guarantee that its requested attributes were honored.
    if (getOutputMode() != mode) {
        m_error = ALC_INVALID_VALUE;
        return false;
    }
    m_context->outputMode = mode;
    m_error = ALC_NO_ERROR;
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
