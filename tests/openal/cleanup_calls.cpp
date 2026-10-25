#include "cleanup_calls.h"
#undef alGetError
#undef alSourceStop
#undef alSourcei
#undef alDeleteSources
#undef alDeleteBuffers
#include <AL/alc.h>
#include <AL/alext.h>
#include <cstdlib>

namespace {
CleanupTest::Fault active = CleanupTest::Fault::None;
ALenum pending = AL_NO_ERROR;
bool didInject = false;
ALuint leakedSource = 0;
ALuint leakedBuffer = 0;
ALCcontext *cleanupContext = nullptr;
bool inject(CleanupTest::Fault fault)
{
    if (active != fault || didInject)
        return false;
    didInject = true;
    pending = AL_INVALID_OPERATION;
    cleanupContext = alcGetCurrentContext();
    return true;
}
}
void CleanupTest::arm(Fault fault) { active = fault; didInject = false; }
bool CleanupTest::injected() { return didInject; }
void CleanupTest::releaseLeakedObjects()
{
    auto setCurrent = reinterpret_cast<PFNALCSETTHREADCONTEXTPROC>(alcGetProcAddress(nullptr, "alcSetThreadContext"));
    auto getCurrent = reinterpret_cast<PFNALCGETTHREADCONTEXTPROC>(alcGetProcAddress(nullptr, "alcGetThreadContext"));
    if (setCurrent == nullptr || getCurrent == nullptr)
        std::abort();
    ALCcontext *previous = getCurrent();
    if (!setCurrent(cleanupContext))
        std::abort();
    if (leakedSource != 0)
        alDeleteSources(1, &leakedSource);
    if (leakedBuffer != 0)
        alDeleteBuffers(1, &leakedBuffer);
    if (alGetError() != AL_NO_ERROR || !setCurrent(previous))
        std::abort();
}
ALenum AL_APIENTRY cleanupGetError()
{
    const ALenum real = alGetError();
    inject(CleanupTest::Fault::Pending);
    if (pending != AL_NO_ERROR) {
        const ALenum result = pending;
        pending = AL_NO_ERROR;
        return result;
    }
    return real;
}
void AL_APIENTRY cleanupSourceStop(ALuint source)
{
    if (!inject(CleanupTest::Fault::Stop))
        alSourceStop(source);
}
void AL_APIENTRY cleanupSourcei(ALuint source, ALenum parameter, ALint value)
{
    if (parameter == AL_BUFFER && value == 0 && inject(CleanupTest::Fault::Detach))
        return;
    alSourcei(source, parameter, value);
}
void AL_APIENTRY cleanupDeleteSources(ALsizei count, const ALuint *sources)
{
    if (inject(CleanupTest::Fault::DeleteSource)) {
        leakedSource = sources[0];
        return;
    }
    alDeleteSources(count, sources);
}
void AL_APIENTRY cleanupDeleteBuffers(ALsizei count, const ALuint *buffers)
{
    if (inject(CleanupTest::Fault::DeleteBuffer)) {
        leakedBuffer = buffers[0];
        return;
    }
    alDeleteBuffers(count, buffers);
}
