#pragma once
#include <AL/al.h>

namespace CleanupTest {
enum class Fault { None, Pending, Stop, Detach, DeleteSource, DeleteBuffer };
void arm(Fault fault);
bool injected();
void releaseLeakedObjects();
}
ALenum AL_APIENTRY cleanupGetError();
void AL_APIENTRY cleanupSourceStop(ALuint source);
void AL_APIENTRY cleanupSourcei(ALuint source, ALenum parameter, ALint value);
void AL_APIENTRY cleanupDeleteSources(ALsizei count, const ALuint *sources);
void AL_APIENTRY cleanupDeleteBuffers(ALsizei count, const ALuint *buffers);
#define alGetError cleanupGetError
#define alSourceStop cleanupSourceStop
#define alSourcei cleanupSourcei
#define alDeleteSources cleanupDeleteSources
#define alDeleteBuffers cleanupDeleteBuffers
