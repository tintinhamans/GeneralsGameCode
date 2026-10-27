#include "OpenALAudioDevice/OpenALDevice.h"
#include "OpenALAudioDevice/OpenALPcmStream.h"
#include "gain_calls.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

int main()
{
    OpenALDevice device;
    OpenALPcmStream stream;
    CHECK(!stream.setGain(1));
    CHECK(GainTest::calls() == 0);
    CHECK(device.openLoopback() && stream.open(device, 48000, 1, 1024, true));
    CHECK(stream.setGain(1));
    CHECK(GainTest::calls() == 1 && GainTest::applied() == 1);
    CHECK(stream.setGain(1) && GainTest::calls() == 1);
    bool wrongThread = true;
    std::thread other([&] { wrongThread = stream.setGain(1); });
    other.join();
    CHECK(!wrongThread && GainTest::calls() == 1);
    CHECK(!stream.setGain(-1));
    CHECK(!stream.setGain(std::numeric_limits<float>::quiet_NaN()));
    CHECK(!stream.setGain(std::numeric_limits<float>::infinity()));
    CHECK(GainTest::calls() == 1);
    CHECK(stream.setGain(0.5f) && GainTest::calls() == 2 && GainTest::applied() == 0.5f);
    CHECK(stream.setGain(0.5f) && GainTest::calls() == 2);
    GainTest::nativeErrorNext();
    CHECK(!stream.setGain(0.75f) && GainTest::calls() == 3 && GainTest::applied() == 0.75f);
    CHECK(stream.getLastError() == AL_INVALID_VALUE);
    CHECK(stream.setGain(0.5f) && GainTest::calls() == 4 && GainTest::applied() == 0.5f);
    CHECK(stream.setGain(0.5f) && GainTest::calls() == 4);
    GainTest::failNext();
    CHECK(!stream.setGain(0.25f) && GainTest::calls() == 5);
    CHECK(stream.getLastError() == AL_INVALID_OPERATION && GainTest::applied() == 0.5f);
    CHECK(stream.setGain(0.25f) && GainTest::calls() == 6 && GainTest::applied() == 0.25f);
    CHECK(stream.getLastError() == AL_NO_ERROR);
    CHECK(stream.setGain(0.25f) && GainTest::calls() == 6);
    GainTest::failNext();
    CHECK(!stream.setGain(0.75f) && GainTest::calls() == 7 && GainTest::applied() == 0.25f);
    CHECK(stream.setGain(0.25f) && GainTest::calls() == 8 && stream.getLastError() == AL_NO_ERROR);
    GainTest::failNext();
    CHECK(stream.setGain(0.25f) && GainTest::calls() == 8);
    CHECK(!stream.setGain(0.125f) && GainTest::calls() == 9);
    CHECK(stream.setGain(0.125f) && GainTest::calls() == 10 && GainTest::applied() == 0.125f);
    CHECK(stream.setGain(0) && GainTest::calls() == 11);
    CHECK(stream.setGain(-0.0f) && GainTest::calls() == 11);
    GainTest::nativeErrorNext();
    CHECK(!stream.setGain(0) && GainTest::calls() == 11);
    CHECK(stream.getLastError() == AL_INVALID_VALUE);
    CHECK(stream.setGain(0) && GainTest::calls() == 12 && stream.getLastError() == AL_NO_ERROR);
    CHECK(stream.stop());
    CHECK(stream.setGain(0) && GainTest::calls() == 12);
    CHECK(stream.close());
    CHECK(!stream.setGain(0) && GainTest::calls() == 12);
    CHECK(stream.open(device, 48000, 2, 1024));
    GainTest::failNext();
    CHECK(!stream.setGain(0) && GainTest::calls() == 13);
    CHECK(stream.setGain(0) && GainTest::calls() == 14 && GainTest::applied() == 0);
    CHECK(stream.setGain(0) && GainTest::calls() == 14);
    CHECK(stream.close() && device.close());
    std::puts("gain cache native calls, failures, validation, reset and reopen passed");
}
