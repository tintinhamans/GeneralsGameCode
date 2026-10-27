#include "gain_calls.h"
#undef alGetError
#undef alSourcef

namespace {
unsigned gainCalls = 0;
float appliedGain = 0;
bool failNextGain = false;
ALenum pending = AL_NO_ERROR;
bool nativeError = false;
ALuint lastSource = 0;
}
unsigned GainTest::calls() { return gainCalls; }
float GainTest::applied() { return appliedGain; }
void GainTest::failNext() { failNextGain = true; }
void GainTest::nativeErrorNext() { nativeError = true; }

ALenum AL_APIENTRY gainGetError()
{
    if (nativeError) {
        nativeError = false;
        alSourcef(lastSource, AL_GAIN, -1);
    }
    const ALenum real = alGetError();
    if (pending != AL_NO_ERROR) {
        const ALenum result = pending;
        pending = AL_NO_ERROR;
        return result;
    }
    return real;
}

void AL_APIENTRY gainSourcef(ALuint source, ALenum parameter, ALfloat value)
{
    if (parameter == AL_GAIN) {
        ++gainCalls;
        lastSource = source;
        if (failNextGain) {
            failNextGain = false;
            pending = AL_INVALID_OPERATION;
            return;
        }
    }
    alSourcef(source, parameter, value);
    if (parameter == AL_GAIN)
        alGetSourcef(source, AL_GAIN, &appliedGain);
}
