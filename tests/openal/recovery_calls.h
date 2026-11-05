#pragma once
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <string>

namespace RecoveryTest {
void reset();
void enableSystemEvents(bool enable);
void supportPlaybackAdded(bool support);
void failEventControl(bool fail);
void notify(ALCenum event, ALCenum type = ALC_PLAYBACK_DEVICE_SOFT);
unsigned eventRegistrations();
unsigned eventUnregistrations();
bool eventCallbackRegistered();
void disconnect();
void failReopen(bool fail);
void staleSuccess(bool stale);
void suppressDisconnectExtension(bool suppress);
unsigned connectionQueries();
void failPlay(bool fail);
void forceStereo(bool force);
ALCint reopenOutputMode();
unsigned reopenCalls();
unsigned sourcePlayCalls();
const std::string &reopenName();
}
ALCdevice *ALC_APIENTRY recoveryOpenDevice(const ALCchar *name);
ALCboolean ALC_APIENTRY recoveryCloseDevice(ALCdevice *device);
ALCcontext *ALC_APIENTRY recoveryCreateContext(ALCdevice *device, const ALCint *attributes);
void ALC_APIENTRY recoveryGetIntegerv(ALCdevice *device, ALCenum parameter, ALCsizei count, ALCint *values);
ALCboolean ALC_APIENTRY recoveryIsExtensionPresent(ALCdevice *device, const ALCchar *name);
ALCenum ALC_APIENTRY recoveryGetError(ALCdevice *device);
void *ALC_APIENTRY recoveryGetProcAddress(ALCdevice *device, const ALCchar *name);
void AL_APIENTRY recoveryGenSources(ALsizei count, ALuint *sources);
void AL_APIENTRY recoveryDeleteSources(ALsizei count, const ALuint *sources);
void AL_APIENTRY recoverySourcePlay(ALuint source);
ALenum AL_APIENTRY recoveryAlGetError();
#ifndef RECOVERY_CALLS_IMPLEMENTATION
#define alcOpenDevice recoveryOpenDevice
#define alcCloseDevice recoveryCloseDevice
#define alcCreateContext recoveryCreateContext
#define alcGetIntegerv recoveryGetIntegerv
#define alcGetError recoveryGetError
#define alcIsExtensionPresent recoveryIsExtensionPresent
#define alcGetProcAddress recoveryGetProcAddress
#define alGenSources recoveryGenSources
#define alDeleteSources recoveryDeleteSources
#define alSourcePlay recoverySourcePlay
#define alGetError recoveryAlGetError
#endif
