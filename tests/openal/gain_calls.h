#pragma once
#include <AL/al.h>

namespace GainTest {
unsigned calls();
float applied();
void failNext();
void nativeErrorNext();
}
ALenum AL_APIENTRY gainGetError();
void AL_APIENTRY gainSourcef(ALuint source, ALenum parameter, ALfloat value);
#define alGetError gainGetError
#define alSourcef gainSourcef
