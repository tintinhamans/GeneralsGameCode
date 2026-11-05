#define RECOVERY_CALLS_IMPLEMENTATION
#include "recovery_calls.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <mutex>

namespace {
ALCdevice *capturedDevice = nullptr;
std::mutex eventMutex;
ALCEVENTPROCTYPESOFT eventCallback = nullptr;
void *eventUser = nullptr;
bool systemEvents = false;
bool supportsAdded = true;
bool eventControlFails = false;
ALCenum globalError = ALC_NO_ERROR;
unsigned registrations = 0;
unsigned unregistrations = 0;
ALCenum ALC_APIENTRY eventSupported(ALCenum event, ALCenum type) noexcept
{
    if (!supportsAdded && event == ALC_EVENT_TYPE_DEVICE_ADDED_SOFT)
        return ALC_EVENT_NOT_SUPPORTED_SOFT;
    if (type == ALC_PLAYBACK_DEVICE_SOFT &&
        (event == ALC_EVENT_TYPE_DEVICE_ADDED_SOFT || event == ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT))
        return ALC_EVENT_SUPPORTED_SOFT;
    return ALC_EVENT_NOT_SUPPORTED_SOFT;
}
ALCboolean ALC_APIENTRY eventControl(ALCsizei, const ALCenum *, ALCboolean) noexcept
{
    if (eventControlFails) {
        globalError = ALC_INVALID_VALUE;
        return ALC_FALSE;
    }
    return ALC_TRUE;
}
void ALC_APIENTRY setEventCallback(ALCEVENTPROCTYPESOFT callback, void *user) noexcept
{
    std::lock_guard<std::mutex> lock(eventMutex);
    eventCallback = callback;
    eventUser = user;
    if (callback != nullptr)
        ++registrations;
    else
        ++unregistrations;
}
ALCcontext *capturedContext = nullptr;
struct Source { ALCcontext *context; ALuint source; };
std::vector<Source> capturedSources;
bool overridden = false;
bool connected = true;
bool fail = false;
bool stale = false;
unsigned reopens = 0;
unsigned queries = 0;
bool suppressExtension = false;
unsigned plays = 0;
ALCenum error = ALC_NO_ERROR;
ALenum alError = AL_NO_ERROR;
bool failPlaying = false;
bool stereo = false;
ALCint reopenMode = ALC_ANY_SOFT;
std::string target;
auto currentContext()
{
    auto get = reinterpret_cast<PFNALCGETTHREADCONTEXTPROC>(alcGetProcAddress(nullptr, "alcGetThreadContext"));
    return get();
}
ALCboolean ALC_APIENTRY reopenDevice(ALCdevice *device, const ALCchar *name, const ALCint *attributes)
{
    ++reopens;
    target = name;
    for (const ALCint *attribute = attributes; attribute != nullptr && attribute[0] != 0; attribute += 2) {
        if (attribute[0] == ALC_OUTPUT_MODE_SOFT)
            reopenMode = attribute[1];
    }
    if (fail) {
        error = ALC_INVALID_DEVICE;
        return ALC_FALSE;
    }
    auto reopen = reinterpret_cast<LPALCREOPENDEVICESOFT>(alcGetProcAddress(device, "alcReopenDeviceSOFT"));
    const auto result = reopen(device, name, attributes);
    if (result == ALC_TRUE && !stale)
        connected = true;
    return result;
}
}
namespace RecoveryTest {
void reset()
{
    systemEvents = false;
    supportsAdded = true;
    eventControlFails = false;
    globalError = ALC_NO_ERROR;
    registrations = 0;
    unregistrations = 0;
    overridden = false;
    connected = true;
    fail = false;
    stale = false;
    reopens = 0;
    queries = 0;
    suppressExtension = false;
    plays = 0;
    error = ALC_NO_ERROR;
    target.clear();
    alError = AL_NO_ERROR;
    failPlaying = false;
    stereo = false;
    reopenMode = ALC_ANY_SOFT;
}
void enableSystemEvents(bool enable) { systemEvents = enable; }
void supportPlaybackAdded(bool support) { supportsAdded = support; }
void failEventControl(bool fail) { eventControlFails = fail; }
void notify(ALCenum event, ALCenum type)
{
    std::lock_guard<std::mutex> lock(eventMutex);
    if (eventCallback != nullptr)
        eventCallback(event, type, nullptr, 0, nullptr, eventUser);
}
unsigned eventRegistrations() { return registrations; }
unsigned eventUnregistrations() { return unregistrations; }
bool eventCallbackRegistered()
{
    std::lock_guard<std::mutex> lock(eventMutex);
    return eventCallback != nullptr;
}
void disconnect()
{
    overridden = true;
    connected = false;
    auto get = reinterpret_cast<PFNALCGETTHREADCONTEXTPROC>(alcGetProcAddress(nullptr, "alcGetThreadContext"));
    auto set = reinterpret_cast<PFNALCSETTHREADCONTEXTPROC>(alcGetProcAddress(nullptr, "alcSetThreadContext"));
    const auto previous = get();
    set(capturedContext);
    for (const auto &source : capturedSources) {
        if (source.context == capturedContext)
            alSourceStop(source.source);
    }
    set(previous);
}
void failReopen(bool value) { fail = value; }
void staleSuccess(bool value) { stale = value; }
void suppressDisconnectExtension(bool value) { suppressExtension = value; }
unsigned connectionQueries() { return queries; }
void failPlay(bool value) { failPlaying = value; }
void forceStereo(bool value) { stereo = value; }
ALCint reopenOutputMode() { return reopenMode; }
unsigned reopenCalls() { return reopens; }
unsigned sourcePlayCalls() { return plays; }
const std::string &reopenName() { return target; }
}
ALCdevice *ALC_APIENTRY recoveryOpenDevice(const ALCchar *name)
{
    auto device = alcOpenDevice(name);
    if (device != nullptr)
        capturedDevice = device;
    return device;
}
ALCboolean ALC_APIENTRY recoveryCloseDevice(ALCdevice *device)
{
    const auto result = alcCloseDevice(device);
    if (result == ALC_TRUE && device == capturedDevice) {
        capturedDevice = nullptr;
        capturedContext = nullptr;
    }
    return result;
}
ALCcontext *ALC_APIENTRY recoveryCreateContext(ALCdevice *device, const ALCint *attributes)
{
    auto context = alcCreateContext(device, attributes);
    if (device == capturedDevice)
        capturedContext = context;
    return context;
}
void ALC_APIENTRY recoveryGetIntegerv(ALCdevice *device, ALCenum parameter, ALCsizei count, ALCint *values)
{
    if (stereo && device == capturedDevice && parameter == ALC_OUTPUT_MODE_SOFT && count > 0) {
        values[0] = ALC_STEREO_BASIC_SOFT;
        return;
    }
    if (parameter == ALC_CONNECTED)
        ++queries;
    if (overridden && device == capturedDevice && parameter == ALC_CONNECTED && count > 0) {
        values[0] = ALC_FALSE;
        if (connected)
            values[0] = ALC_TRUE;
        return;
    }
    alcGetIntegerv(device, parameter, count, values);
}
ALCboolean ALC_APIENTRY recoveryIsExtensionPresent(ALCdevice *device, const ALCchar *name)
{
    if (std::strcmp(name, "ALC_SOFT_system_events") == 0)
        return systemEvents;
    if (suppressExtension && std::strcmp(name, "ALC_EXT_disconnect") == 0)
        return ALC_FALSE;
    return alcIsExtensionPresent(device, name);
}
ALCenum ALC_APIENTRY recoveryGetError(ALCdevice *device)
{
    const auto native = alcGetError(device);
    if (device == nullptr && globalError != ALC_NO_ERROR) {
        const auto result = globalError;
        globalError = ALC_NO_ERROR;
        return result;
    }
    if (device == capturedDevice && error != ALC_NO_ERROR) {
        const auto result = error;
        error = ALC_NO_ERROR;
        return result;
    }
    return native;
}
void *ALC_APIENTRY recoveryGetProcAddress(ALCdevice *device, const ALCchar *name)
{
    if (std::strcmp(name, "alcEventIsSupportedSOFT") == 0)
        return reinterpret_cast<void *>(&eventSupported);
    if (std::strcmp(name, "alcEventControlSOFT") == 0)
        return reinterpret_cast<void *>(&eventControl);
    if (std::strcmp(name, "alcEventCallbackSOFT") == 0)
        return reinterpret_cast<void *>(&setEventCallback);
    if (std::strcmp(name, "alcReopenDeviceSOFT") == 0)
        return reinterpret_cast<void *>(&reopenDevice);
    return alcGetProcAddress(device, name);
}
void AL_APIENTRY recoveryGenSources(ALsizei count, ALuint *sources)
{
    alGenSources(count, sources);
    for (ALsizei i = 0; i < count; ++i)
        capturedSources.push_back({currentContext(), sources[i]});
}
void AL_APIENTRY recoveryDeleteSources(ALsizei count, const ALuint *sources)
{
    const auto context = currentContext();
    capturedSources.erase(std::remove_if(capturedSources.begin(), capturedSources.end(), [&](const Source &source) {
        return source.context == context && std::find(sources, sources + count, source.source) != sources + count;
    }), capturedSources.end());
    alDeleteSources(count, sources);
}
void AL_APIENTRY recoverySourcePlay(ALuint source)
{
    ++plays;
    if (failPlaying) {
        alError = AL_INVALID_OPERATION;
        return;
    }
    alSourcePlay(source);
}
ALenum AL_APIENTRY recoveryAlGetError()
{
    const auto native = alGetError();
    if (alError != AL_NO_ERROR) {
        const auto value = alError;
        alError = AL_NO_ERROR;
        return value;
    }
    return native;
}
