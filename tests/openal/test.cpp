#include "OpenALAudioDevice/OpenALDevice.h"
#include "OpenALAudioDevice/OpenALPcmStream.h"
#include "VideoDevice/FFmpeg/FFmpegAudioFile.h"
#include "Common/File.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <limits>
extern "C" {
#include <libavutil/log.h>
}

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
using Result = OpenALPcmStream::QueueResult;
using State = OpenALPcmStream::State;

static std::vector<short> sequence(int start, int count)
{
    std::vector<short> pcm(count * 2);
    for (int i = 0; i < count; ++i) {
        pcm[2 * i] = static_cast<short>(1000 + (start + i) % 3000);
        pcm[2 * i + 1] = static_cast<short>(-7000 + (start + i) % 1000);
    }
    return pcm;
}

static std::vector<short> render(OpenALDevice &device, int frames)
{
    std::vector<short> output(frames * 2);
    CHECK(device.render(output.data(), frames));
    return output;
}

static void silence(const std::vector<short> &pcm, size_t skip = 0)
{
    for (size_t i = skip; i < pcm.size(); ++i) {
        if (std::abs(static_cast<int>(pcm[i])) > 2)
            std::fprintf(stderr, "non-silent sample %zu (skip %zu): %d\n", i, skip, pcm[i]);
        CHECK(std::abs(static_cast<int>(pcm[i])) <= 2);
    }
}

static void queueAndWrap()
{
    OpenALDevice device;
    CHECK(device.openLoopback());
    OpenALPcmStream stream;
    CHECK(!stream.open(device, 0, 2, 4096));
    CHECK(!stream.open(device, 48000, 3, 4096));
    CHECK(!stream.open(device, 48000, 2, 0xffffffffu));
    CHECK(stream.open(device, 48000, 2, 4096));
    CHECK(stream.queue(nullptr, 1) == Result::Invalid);
    auto initial = sequence(0, 4096);
    CHECK(stream.queue(initial.data(), 4096) == Result::Accepted);
    CHECK(stream.availableFrames() == 0);
    CHECK(stream.queue(initial.data(), 1) == Result::Full);
    CHECK(stream.play());
    std::vector<short> output;
    int submitted = 4096;
    for (int iteration = 0; iteration < 48; ++iteration) {
        auto block = render(device, 256);
        output.insert(output.end(), block.begin(), block.end());
        auto input = sequence(submitted, 256);
        CHECK(stream.queue(input.data(), 256) == Result::Accepted);
        submitted += 256;
    }
    CHECK(stream.markEnd());
    CHECK(stream.queue(initial.data(), 1) == Result::Invalid);
    auto tail = render(device, 4608);
    output.insert(output.end(), tail.begin(), tail.end());
    CHECK(stream.state() == State::Finished);
    const auto expected = sequence(0, submitted);
    CHECK(output.size() >= expected.size());
    int maximumError = 0;
    for (size_t i = 0; i < expected.size(); ++i)
        maximumError = std::max(maximumError, std::abs(static_cast<int>(output[i]) - expected[i]));
    std::printf("wrap sample maximum error: %d\n", maximumError);
    CHECK(maximumError <= 2);
    auto settling = render(device, 512);
    output.insert(output.end(), settling.begin(), settling.end());
    int previousLeft = std::abs(static_cast<int>(expected[expected.size() - 2]));
    int previousRight = std::abs(static_cast<int>(expected.back()));
    for (size_t i = expected.size(); i < output.size(); i += 2) {
        const int left = std::abs(static_cast<int>(output[i]));
        const int right = std::abs(static_cast<int>(output[i + 1]));
        CHECK(left <= previousLeft + 2 && right <= previousRight + 2);
        previousLeft = left;
        previousRight = right;
    }
    silence(output, output.size() - 512);
    silence(render(device, 512));
    CHECK(!stream.play());
    CHECK(stream.stop());
    CHECK(stream.state() == State::Stopped && stream.availableFrames() == 4096);
}

static void controlsAndStarvation()
{
    OpenALDevice device;
    CHECK(device.openLoopback());
    OpenALPcmStream stream;
    CHECK(stream.open(device, 48000, 2, 1024));
    CHECK(stream.play());
    silence(render(device, 512));
    CHECK(stream.state() == State::Starved);
    auto input = sequence(0, 64);
    input[126] = 0;
    input[127] = 0;
    CHECK(stream.queue(input.data(), 64) == Result::Accepted);
    CHECK(stream.markEnd());
    auto output = render(device, 1024);
    CHECK(stream.state() == State::Finished);
    int audibleFrames = 0;
    for (size_t i = 0; i < output.size(); i += 2) {
        if (std::abs(static_cast<int>(output[i + 1])) > 100)
            ++audibleFrames;
    }
    CHECK(audibleFrames == 63);
    silence(render(device, 512));
    CHECK(stream.stop());
    CHECK(stream.markEnd());
    CHECK(stream.play());
    silence(render(device, 512));
    CHECK(stream.state() == State::Finished);
    CHECK(stream.stop());
    CHECK(stream.play());
    silence(render(device, 512));
    CHECK(stream.markEnd());
    silence(render(device, 512));
    CHECK(stream.state() == State::Finished);
    CHECK(stream.stop());
    auto longer = sequence(0, 1024);
    CHECK(stream.queue(longer.data(), 1024) == Result::Accepted);
    CHECK(stream.play());
    render(device, 128);
    CHECK(stream.pause());
    auto paused = render(device, 512);
    silence(paused, 256);
    const uint32_t freeWhilePaused = stream.availableFrames();
    silence(render(device, 512));
    CHECK(stream.availableFrames() == freeWhilePaused && stream.state() == State::Paused);
    CHECK(stream.play());
    auto resumed = render(device, 128);
    CHECK(std::any_of(resumed.begin(), resumed.end(), [](short value) { return value > 100; }));
    CHECK(stream.stop());
    silence(render(device, 512), 256);
    CHECK(stream.state() == State::Stopped);
    CHECK(stream.queue(input.data(), 64) == Result::Accepted);
    CHECK(stream.markEnd());
    CHECK(stream.play());
    render(device, 1024);
    CHECK(stream.state() == State::Finished);
}

static void pauseContinuity()
{
    constexpr int frames = 16384;
    const int pausePoints[] = {2048, 5376, 9216, 13824};
    std::vector<short> input(frames * 2);
    uint32_t value = 0x6a09e667;
    for (short &sample : input) {
        value = value * 1664525u + 1013904223u;
        sample = static_cast<short>(static_cast<int>((value >> 16) & 16383u) - 8192);
    }
    // Silence guards isolate OpenAL's pause gain ramps, not decoder payload.
    for (int point : pausePoints)
        std::fill(input.begin() + (point - 256) * 2, input.begin() + (point + 512) * 2, 0);
    std::fill(input.end() - 512, input.end(), 0);

    auto playback = [&](bool paused) {
        OpenALDevice device;
        OpenALPcmStream stream;
        CHECK(device.openLoopback() && stream.open(device, 48000, 2, frames));
        CHECK(stream.queue(input.data(), frames) == Result::Accepted);
        CHECK(stream.markEnd() && stream.play());
        std::vector<short> output;
        int position = 0;
        for (int point : pausePoints) {
            auto block = render(device, point - position);
            output.insert(output.end(), block.begin(), block.end());
            position = point;
            if (paused) {
                CHECK(stream.pause());
                silence(render(device, 512));
                const uint32_t available = stream.availableFrames();
                silence(render(device, 256));
                CHECK(stream.availableFrames() == available && stream.state() == State::Paused);
                CHECK(stream.play());
            }
        }
        auto final = render(device, frames - position + 1024);
        output.insert(output.end(), final.begin(), final.end());
        CHECK(stream.state() == State::Finished);
        return output;
    };
    const auto reference = playback(false);
    const auto paused = playback(true);
    CHECK(reference.size() == paused.size());
    auto expected = input;
    expected.resize(reference.size(), 0);
    int maximumError = 0;
    for (size_t i = 0; i < reference.size(); ++i) {
        CHECK(std::abs(static_cast<int>(reference[i]) - expected[i]) <= 2);
        maximumError = std::max(maximumError, std::abs(static_cast<int>(paused[i]) - reference[i]));
    }
    std::printf("four-pause continuity maximum error: %d\n", maximumError);
    CHECK(maximumError <= 2);
    auto matches = [&](const std::vector<short> &output) {
        if (output.size() != reference.size())
            return false;
        return std::equal(output.begin(), output.end(), reference.begin(), [](short a, short b) {
            return std::abs(static_cast<int>(a) - b) <= 2;
        });
    };
    auto skipped = paused;
    skipped.erase(skipped.begin() + 6200 * 2, skipped.begin() + 6201 * 2);
    skipped.resize(reference.size(), 0);
    CHECK(!matches(skipped));
    auto repeated = paused;
    repeated.insert(repeated.begin() + 6201 * 2, paused.begin() + 6200 * 2, paused.begin() + 6201 * 2);
    repeated.resize(reference.size());
    CHECK(!matches(repeated));
}

static void parametersAndReplacement()
{
    OpenALDevice device;
    OpenALPcmStream stream;
    CHECK(device.openLoopback() && stream.open(device, 48000, 2, 4096));
    CHECK(!stream.setGain(-1) && !stream.setPitch(0));
    CHECK(!stream.setPosition(0, 0, 0, true));
    CHECK(stream.setGain(0.5f) && stream.setPitch(2.0f));
    auto input = sequence(0, 4096);
    CHECK(stream.queue(input.data(), 4096) == Result::Accepted && stream.play());
    const auto faster = render(device, 128);
    for (int frame = 0; frame < 128; ++frame) {
        CHECK(std::abs(faster[2 * frame] - input[4 * frame] / 2) <= 2);
        CHECK(std::abs(faster[2 * frame + 1] - input[4 * frame + 1] / 2) <= 2);
    }
    CHECK(stream.getReadFrames() >= 256);
    CHECK(stream.stop() && stream.getReadFrames() == 0);
    render(device, 1024);
    CHECK(stream.setGain(1) && stream.setPitch(1));
    std::fill(input.begin(), input.begin() + 512 * 2, 0);
    CHECK(stream.queue(input.data(), 4096) == Result::Accepted && stream.play());
    auto output = render(device, 128);
    auto decay = sequence(8000, 512);
    std::fill(decay.end() - 256, decay.end(), 0);
    CHECK(!stream.replaceQueuedAfter(4097, decay.data(), 512));
    CHECK(stream.replaceQueuedAfter(1024, decay.data(), 512));
    CHECK(stream.queue(decay.data(), 1) == Result::Invalid);
    CHECK(!stream.replaceQueuedAfter(1024, nullptr, 0));
    auto rest = render(device, 2048);
    output.insert(output.end(), rest.begin(), rest.end());
    auto expected = std::vector<short>(input.begin(), input.begin() + 1024 * 2);
    expected.insert(expected.end(), decay.begin(), decay.end());
    expected.resize(output.size(), 0);
    CHECK(output.size() == expected.size());
    for (size_t i = 0; i < output.size(); ++i) {
        if (std::abs(static_cast<int>(output[i]) - expected[i]) > 2)
            std::fprintf(stderr, "replacement sample %zu actual %d expected %d\n", i, output[i], expected[i]);
        CHECK(std::abs(static_cast<int>(output[i]) - expected[i]) <= 2);
    }
    CHECK(stream.getReadFrames() == 1536 && stream.state() == State::Finished);
    CHECK(stream.stop() && stream.getReadFrames() == 0);
    std::puts("gain/pitch/read-counter/immutable replacement content tests passed");
}

static void replacementEdges()
{
    OpenALDevice device;
    OpenALPcmStream stream;
    CHECK(device.openLoopback() && stream.open(device, 48000, 2, 1024));
    auto input = sequence(0, 1024);
    CHECK(stream.queue(input.data(), 1024) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    CHECK(stream.replaceQueuedAfter(512, nullptr, 0));
    render(device, 2048);
    CHECK(stream.state() == State::Finished && stream.getReadFrames() == 512);
    CHECK(!stream.replaceQueuedAfter(512, nullptr, 0));
    silence(render(device, 512));
    CHECK(stream.stop());
    render(device, 1024);

    CHECK(stream.queue(input.data(), 1024) == Result::Accepted && stream.play());
    render(device, 512);
    const uint32_t alreadyRead = stream.getReadFrames();
    CHECK(alreadyRead > 128);
    std::vector<short> tail(128);
    for (int i = 0; i < 63; ++i) {
        tail[2 * i] = 10000;
        tail[2 * i + 1] = -10000;
    }
    CHECK(stream.replaceQueuedAfter(128, tail.data(), 64));
    const auto late = render(device, 2048);
    int tailFrames = 0;
    for (size_t i = 0; i < late.size(); i += 2) {
        if (late[i] > 9000 && late[i + 1] < -9000)
            ++tailFrames;
    }
    CHECK(tailFrames == 63);
    CHECK(stream.state() == State::Finished && stream.getReadFrames() == alreadyRead + 64);
    CHECK(stream.stop());
    render(device, 1024);

    std::fill(input.begin(), input.begin() + 512 * 2, 0);
    CHECK(stream.queue(input.data(), 1024) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    render(device, 128);
    CHECK(stream.pause());
    silence(render(device, 512));
    const uint32_t pausedRead = stream.getReadFrames();
    CHECK(stream.replaceQueuedAfter(512, tail.data(), 64));
    silence(render(device, 512));
    CHECK(stream.getReadFrames() == pausedRead && stream.state() == State::Paused);
    CHECK(stream.play());
    render(device, 2048);
    std::printf("paused replacement read before %u after %u state %d\n", pausedRead, stream.getReadFrames(), static_cast<int>(stream.state()));
    const uint32_t cut = std::max<uint32_t>(512, pausedRead);
    CHECK(stream.state() == State::Finished && stream.getReadFrames() == cut + 64);
    silence(render(device, 512));
    std::puts("empty/late/paused/after-EOF immutable replacement edge tests passed");
}

static void monoResampling()
{
    OpenALDevice device;
    OpenALPcmStream stream;
    CHECK(device.openLoopback(48000));
    CHECK(stream.open(device, 16000, 1, 16384));
    std::vector<short> input(16000);
    for (int i = 0; i < 16000; ++i)
        input[i] = static_cast<short>(std::lround(10000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * i / 16000.0)));
    CHECK(stream.queue(input.data(), 16000) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    const auto output = render(device, 50176);
    CHECK(stream.state() == State::Finished && stream.availableFrames() == 16384);
    double sineProjection = 0, cosineProjection = 0, energy = 0;
    int risingCrossings = 0;
    const int count = 38400;
    for (int i = 4800; i < 43200; ++i) {
        CHECK(std::abs(output[2 * i] - output[2 * i + 1]) <= 2);
        const double angle = 2.0 * 3.141592653589793 * 440.0 * i / 48000.0;
        const double value = output[2 * i];
        sineProjection += value * std::sin(angle);
        cosineProjection += value * std::cos(angle);
        energy += value * value;
        if (output[2 * (i - 1)] <= 0 && value > 0)
            ++risingCrossings;
    }
    const double projection = sineProjection * sineProjection + cosineProjection * cosineProjection;
    const double amplitude = 2.0 * std::sqrt(projection) / count;
    const double phase = std::atan2(cosineProjection, sineProjection);
    double residualEnergy = energy - 2.0 * projection / count;
    if (residualEnergy < 0)
        residualEnergy = 0;
    const double residual = std::sqrt(residualEnergy / energy);
    std::printf("mono16k to stereo48k: amplitude %.3f phase %.6f residual %.6f crossings %d\n",
        amplitude, phase, residual, risingCrossings);
    CHECK(amplitude > 4000 && amplitude < 10000);
    CHECK(std::abs(phase) < 0.2 && residual < 0.06);
    CHECK(risingCrossings >= 351 && risingCrossings <= 353);
    double earlyEnergy = 0, lateEnergy = 0;
    for (int i = 0; i < 400; ++i) {
        earlyEnergy += static_cast<double>(output[2 * i]) * output[2 * i];
        lateEnergy += static_cast<double>(output[2 * (47600 + i)]) * output[2 * (47600 + i)];
    }
    CHECK(earlyEnergy > 1.0e8 && lateEnergy > 1.0e8);
    silence(output, 49024 * 2);
    silence(render(device, 512));
}

static void contextsAndLifetime()
{
    auto getCurrent = reinterpret_cast<PFNALCGETTHREADCONTEXTPROC>(alcGetProcAddress(nullptr, "alcGetThreadContext"));
    CHECK(getCurrent != nullptr);
    ALCcontext *previous = getCurrent();
    auto setCurrent = reinterpret_cast<PFNALCSETTHREADCONTEXTPROC>(alcGetProcAddress(nullptr, "alcSetThreadContext"));
    auto openLoopback = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
    CHECK(setCurrent != nullptr && openLoopback != nullptr);
    ALCdevice *externalDevice = openLoopback(nullptr);
    CHECK(externalDevice != nullptr);
    const ALCint attributes[] = { ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
        ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT, ALC_FREQUENCY, 48000, 0 };
    ALCcontext *externalContext = alcCreateContext(externalDevice, attributes);
    CHECK(externalContext != nullptr && setCurrent(externalContext));
    OpenALDevice first, second;
    CHECK(first.openLoopback() && second.openLoopback());
    OpenALPcmStream a, b;
    CHECK(a.open(first, 48000, 2, 512));
    CHECK(b.open(second, 48000, 2, 512));
    auto left = sequence(0, 512);
    auto right = sequence(1000, 512);
    CHECK(a.queue(left.data(), 512) == Result::Accepted);
    CHECK(b.queue(right.data(), 512) == Result::Accepted);
    CHECK(a.play() && b.play());
    auto firstOutput = render(first, 128);
    auto secondOutput = render(second, 128);
    CHECK(std::abs(firstOutput[100] - left[100]) <= 2);
    CHECK(std::abs(secondOutput[100] - right[100]) <= 2);
    CHECK(getCurrent() == externalContext);
    bool foreignRejected = false;
    std::thread other([&] { foreignRejected = !a.play() && a.queue(left.data(), 1) == Result::Error && !first.close(); });
    other.join();
    CHECK(foreignRejected);
    CHECK(first.close());
    CHECK(a.pause() && a.stop() && a.close());
    CHECK(b.stop() && b.close());
    CHECK(second.close());
    CHECK(getCurrent() == externalContext);
    CHECK(setCurrent(previous));
    alcDestroyContext(externalContext);
    CHECK(alcCloseDevice(externalDevice));
}

class MemoryFile : public File
{
    std::vector<unsigned char> data;
    int position_ = 0;
public:
    explicit MemoryFile(const char *path)
    {
        std::FILE *file = std::fopen(path, "rb");
        CHECK(file != nullptr);
        CHECK(std::fseek(file, 0, SEEK_END) == 0);
        data.resize(std::ftell(file));
        std::rewind(file);
        CHECK(std::fread(data.data(), 1, data.size(), file) == data.size());
        std::fclose(file);
    }
    Int read(void *destination, Int bytes) override
    {
        bytes = std::min(bytes, size() - position_);
        std::memcpy(destination, data.data() + position_, bytes);
        position_ += bytes;
        return bytes;
    }
    Int seek(Int offset, seekMode mode) override
    {
        long long target = offset;
        if (mode == CURRENT)
            target += position_;
        if (mode == END)
            target += size();
        if (target < 0 || target > size())
            return -1;
        position_ = static_cast<Int>(target);
        return position_;
    }
    Int position() override { return position_; }
    Int size() override { return static_cast<Int>(data.size()); }
    void close() override { delete this; }
};

static void decoderBridge(const char *path)
{
    OpenALDevice device;
    OpenALPcmStream stream;
    CHECK(device.openLoopback() && stream.open(device, 48000, 2, 512));
    CHECK(stream.play());
    FFmpegAudioFile decoder;
    CHECK(decoder.open(new MemoryFile(path), 48000, 2));
    double expectedEnergy = 0, renderedEnergy = 0;
    uint32_t accepted = 0;
    auto pump = [&] {
        for (short value : render(device, 128))
            renderedEnergy += static_cast<double>(value) * value;
    };
    decoder.setPcmCallback([&](const short *pcm, int frames) {
        std::vector<short> pending(pcm, pcm + frames * 2);
        for (short value : pending)
            expectedEnergy += static_cast<double>(value) * value;
        int offset = 0;
        while (offset < frames) {
            const int count = std::min(127, frames - offset);
            const Result result = stream.queue(pending.data() + offset * 2, count);
            if (result == Result::Full) {
                pump();
                continue;
            }
            CHECK(result == Result::Accepted);
            offset += count;
            accepted += count;
        }
    });
    while (decoder.decodePacket()) {}
    CHECK(!decoder.hasError() && accepted == 48000);
    CHECK(stream.markEnd());
    for (int i = 0; i < 100 && stream.state() != State::Finished; ++i)
        pump();
    CHECK(stream.state() == State::Finished);
    const double error = std::abs(renderedEnergy - expectedEnergy) / expectedEnergy;
    std::printf("decoder bridge energy relative error: %.9f\n", error);
    CHECK(error < 0.01);
}

static void concurrentMixedControls()
{
    OpenALDevice device;
    CHECK(device.open());
    OpenALPcmStream stream;
    CHECK(stream.open(device, 48000, 1, 4096, true));
    CHECK(stream.schedulePortionBoundary(0));
    std::vector<float> input(512, 0.25f);
    CHECK(stream.queueFloat(input.data(), 512) == Result::Accepted);
    CHECK(stream.play());
    uint32_t submitted = 512;
    for (unsigned iteration = 0; iteration < 80; ++iteration) {
        const float left = static_cast<float>(iteration % 17) / 16.0f;
        CHECK(stream.setStereoMix(left, 1 - left, 0.5f, iteration % 2 != 0));
        CHECK(stream.setGain(0.5f));
        if (stream.availableFrames() >= input.size()) {
            CHECK(stream.queueFloat(input.data(), static_cast<uint32_t>(input.size())) == Result::Accepted);
            submitted += static_cast<uint32_t>(input.size());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(stream.markEnd());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (stream.state() != State::Finished) {
        CHECK(stream.state() != State::Error && std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(stream.getReadFrames() == submitted);
    std::puts("concurrent Null mixer stereo controls and float feed passed");
}

static void concurrentNullStress()
{
    const char *driver = std::getenv("ALSOFT_DRIVERS");
    CHECK(driver != nullptr && std::strcmp(driver, "null") == 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    int consumed = 0;
    for (int round = 0; round < 20; ++round) {
        OpenALDevice device;
        CHECK(device.open());
        OpenALPcmStream stream;
        CHECK(stream.open(device, 48000, 2, 1024));
        const auto input = sequence(round, 127);
        CHECK(stream.play());
        for (int step = 0; step < 80; ++step) {
            CHECK(std::chrono::steady_clock::now() < deadline);
            const uint32_t before = stream.availableFrames();
            const Result result = stream.queue(input.data(), 127);
            CHECK(result == Result::Accepted || result == Result::Full);
            if (step % 13 == 0) {
                CHECK(stream.pause());
                CHECK(stream.play());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (stream.availableFrames() > before)
                ++consumed;
            CHECK(stream.state() != State::Error);
        }
        CHECK(stream.replaceQueuedAfter(stream.getReadFrames(), input.data(), 127));
        CHECK(stream.queue(input.data(), 1) == Result::Invalid);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        CHECK(stream.stop());
        CHECK(stream.getReadFrames() == 0);
        CHECK(stream.availableFrames() == 1024);
        CHECK(stream.queue(input.data(), 127) == Result::Accepted);
        CHECK(stream.markEnd() && stream.play());
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        CHECK(device.close());
        CHECK(stream.close());
    }
    CHECK(consumed > 0);
    std::printf("null mixer concurrent progress observations: %d\n", consumed);
    OpenALDevice device;
    CHECK(device.open());
    const auto shortInput = sequence(0, 32);
    const auto tail = sequence(8000, 64);
    unsigned accepted = 0;
    for (unsigned round = 0; round < 100; ++round) {
        OpenALPcmStream stream;
        CHECK(stream.open(device, 48000, 2, 256));
        CHECK(stream.queue(shortInput.data(), 32) == Result::Accepted);
        CHECK(stream.markEnd() && stream.play());
        if (round % 2 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool replaced = stream.replaceQueuedAfter(32, tail.data(), 64);
        while (stream.state() != State::Finished) {
            CHECK(stream.state() != State::Error && std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        uint32_t expectedRead = 32;
        if (replaced) {
            ++accepted;
            expectedRead += 64;
        }
        CHECK(stream.getReadFrames() == expectedRead);
    }
    std::printf("concurrent EOF/publication rounds: 100, replacement accepted: %u\n", accepted);
}

static double mixedTonePower(const std::vector<short> &pcm, double frequency, int channel = 0)
{
    double sine = 0, cosine = 0;
    for (int i = 0; i < 4410; ++i) {
        const double angle = 2.0 * 3.141592653589793 * frequency * i / 44100.0;
        sine += pcm[(i + 1200) * 2 + channel] * std::sin(angle);
        cosine += pcm[(i + 1200) * 2 + channel] * std::cos(angle);
    }
    return sine * sine + cosine * cosine;
}

static std::vector<short> mixedTone(float left, float right, float frontBack, bool surround,
    float gain = 1)
{
    OpenALDevice device;
    CHECK(device.openLoopback(44100));
    OpenALPcmStream stream;
    CHECK(stream.open(device, 44100, 1, 8192, true));
    CHECK(stream.setStereoMix(left, right, frontBack, surround));
    CHECK(stream.setGain(gain));
    CHECK(!stream.setStereoMix(-0.1f, right, frontBack, surround));
    CHECK(stream.schedulePortionBoundary(0));
    std::vector<short> input(8192);
    for (size_t i = 0; i < input.size(); ++i) {
        const double phase = 2.0 * 3.141592653589793 * i / 44100.0;
        input[i] = static_cast<short>(4000 * (std::sin(250 * phase) + std::sin(1000 * phase) +
            std::sin(4000 * phase) + std::sin(8000 * phase)));
    }
    CHECK(stream.queue(input.data(), static_cast<uint32_t>(input.size())) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    return render(device, 7200);
}

static void legacyMixTests()
{
    const auto full = mixedTone(1, 0, 0, false);
    for (const auto gains : {std::pair<float,float>{0.5f,0.5f}, {0.25f,0.75f}, {0.0f,1.0f}}) {
        const auto pcm = mixedTone(gains.first, gains.second, 0, false);
        CHECK(std::abs(mixedTonePower(pcm, 1000, 0) / mixedTonePower(full, 1000) - gains.first * gains.first) < 0.001);
        CHECK(std::abs(mixedTonePower(pcm, 1000, 1) / mixedTonePower(full, 1000) - gains.second * gains.second) < 0.001);
    }
    const auto quiet = mixedTone(0.25f, 0.75f, 0, false, 0.5f);
    CHECK(std::abs(mixedTonePower(quiet, 1000, 1) / mixedTonePower(full, 1000) - 0.75 * 0.75 * 0.25) < 0.001);
    const auto rear = mixedTone(0.5f, 0.5f, 1, true);
    CHECK(std::abs(mixedTonePower(rear, 1000, 0) / mixedTonePower(full, 1000) - 0.25) < 0.001);
    for (int frame = 1200; frame < 6000; ++frame)
        CHECK(std::abs(static_cast<int>(rear[frame * 2]) + rear[frame * 2 + 1]) <= 1);
    std::puts("linear stereo mix and Dolby phase passed");
}

static std::vector<short> mixedWrap(bool wrapped)
{
    OpenALDevice device;
    CHECK(device.openLoopback());
    OpenALPcmStream stream;
    uint32_t capacity = 16384;
    if (wrapped)
        capacity = 4096;
    CHECK(stream.open(device, 48000, 1, capacity, true));
    CHECK(stream.setStereoMix(0.25f, 0.75f, 0.4f, true));
    CHECK(stream.schedulePortionBoundary(0));
    std::vector<short> input(12000);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<short>(7000 * std::sin(0.071 * i));
    size_t submitted = 0;
    bool ended = false;
    std::vector<short> output;
    while (output.size() < 14000 * 2) {
        const size_t count = std::min<size_t>(stream.availableFrames(), input.size() - submitted);
        if (count != 0) {
            CHECK(stream.queue(input.data() + submitted, static_cast<uint32_t>(count)) == Result::Accepted);
            submitted += count;
        }
        if (submitted == input.size() && !ended) {
            CHECK(stream.markEnd());
            ended = true;
        }
        if (output.empty())
            CHECK(stream.play());
        const auto block = render(device, 256);
        output.insert(output.end(), block.begin(), block.end());
    }
    CHECK(stream.getReadFrames() == input.size());
    CHECK(stream.state() == State::Finished);
    return output;
}

static void mixedBoundaryTests()
{
    CHECK(mixedWrap(false) == mixedWrap(true));
    OpenALDevice device;
    CHECK(device.openLoopback());
    OpenALPcmStream stream;
    CHECK(stream.open(device, 48000, 1, 16384, true));
    CHECK(stream.setStereoMix(0.5f, 0.5f, 1, true));
    CHECK(stream.schedulePortionBoundary(0));
    CHECK(stream.schedulePortionBoundary(5000));
    std::vector<short> input(12000, 8000);
    CHECK(stream.queue(input.data(), static_cast<uint32_t>(input.size())) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    auto output = render(device, 12020);
    CHECK(std::abs(output[5000 * 2]) <= 1);
    CHECK(std::abs(output[5000 * 2 + 1]) <= 1);
    CHECK(std::abs(static_cast<int>(output[5001 * 2]) - 4000) <= 1);
    CHECK(std::abs(static_cast<int>(output[5001 * 2 + 1]) + 4000) <= 1);
    CHECK(stream.getReadFrames() == input.size());
    CHECK(stream.stop());
    CHECK(stream.setStereoMix(0.5f, 0.5f, 0, false));
    CHECK(stream.schedulePortionBoundary(0));
    CHECK(stream.queue(input.data(), 12000) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    render(device, 13000);
    CHECK(stream.getReadFrames() == 12000);
    CHECK(stream.stop());
    CHECK(stream.setStereoMix(0.5f, 0.5f, 1, true));
    CHECK(stream.schedulePortionBoundary(0));
    CHECK(stream.schedulePortionBoundary(6000));
    CHECK(!stream.schedulePortionBoundary(5000));
    CHECK(stream.queue(input.data(), 8192) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    output = render(device, 512);
    CHECK(stream.getReadFrames() < 4096);
    std::vector<short> decay(2048, -6000);
    CHECK(stream.replaceQueuedAfter(4096, decay.data(), 2048));
    const auto replacement = render(device, 7000);
    output.insert(output.end(), replacement.begin(), replacement.end());
    CHECK(stream.getReadFrames() == 4096 + decay.size());
    CHECK(std::abs(output[4096 * 2]) <= 1);
    CHECK(std::abs(static_cast<int>(output[4097 * 2]) + 3000) <= 1);
    CHECK(std::abs(static_cast<int>(output[6000 * 2]) + 3000) <= 1);
    std::puts("mixed ring-wrap continuity, portion/reset/replacement-tail boundaries passed");
}


struct ScalarMixReference
{
    ALCdevice *device = nullptr;
    ALCcontext *context = nullptr;
    LPALCRENDERSAMPLESSOFT renderSamples = nullptr;
    PFNALCSETTHREADCONTEXTPROC setContext = nullptr;
    PFNALCGETTHREADCONTEXTPROC getContext = nullptr;
    ALuint source = 0, buffer = 0;
    std::vector<float> input, tail;
    std::vector<uint32_t> boundaries;
    uint32_t read = 0, boundary = 0, replaceAt = 0;
    bool replacing = false, replacementStarted = false, surround = false;
    float leftGain = 0.5f, rightGain = 0.5f, frontBack = 0;
    double previousRear = 0;

    static ALsizei AL_APIENTRY callback(void *opaque, void *output, ALsizei bytes) noexcept
    {
        auto &data = *static_cast<ScalarMixReference *>(opaque);
        auto stereo = static_cast<float *>(output);
        const uint32_t requested = static_cast<uint32_t>(bytes) / (2 * sizeof(float));
        uint32_t end = static_cast<uint32_t>(data.input.size());
        if (data.replacing)
            end = data.replaceAt + static_cast<uint32_t>(data.tail.size());
        const uint32_t count = std::min(requested, end - data.read);
        for (uint32_t frame = 0; frame < count; ++frame) {
            const uint32_t position = data.read + frame;
            bool replacement = false;
            if (data.replacing && position >= data.replaceAt) {
                replacement = true;
                if (!data.replacementStarted) {
                    data.previousRear = 0;
                    data.replacementStarted = true;
                }
            }
            while (!replacement && data.boundary < data.boundaries.size() &&
                position - data.boundaries[data.boundary] < 0x80000000u) {
                data.previousRear = 0;
                ++data.boundary;
            }
            double mono = 0;
            if (replacement)
                mono = data.tail[position - data.replaceAt];
            else
                mono = data.input[position];
            const double left = mono * data.leftGain;
            const double right = mono * data.rightGain;
            if (data.surround) {
                stereo[frame * 2] = static_cast<float>(((1 - data.frontBack) * left + data.frontBack * data.previousRear));
                stereo[frame * 2 + 1] = static_cast<float>(((1 - data.frontBack) * right - data.frontBack * data.previousRear));
            } else {
                stereo[frame * 2] = static_cast<float>(left);
                stereo[frame * 2 + 1] = static_cast<float>(right);
            }
            data.previousRear = (left + right) * 0.5;
        }
        data.read += count;
        return static_cast<ALsizei>(count * 2 * sizeof(float));
    }

    ScalarMixReference()
    {
        const auto open = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
        CHECK(open != nullptr);
        device = open(nullptr);
        CHECK(device != nullptr);
        const ALCint attributes[] = {ALC_FREQUENCY, 48000, ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
            ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT, ALC_OUTPUT_MODE_SOFT, ALC_STEREO_BASIC_SOFT,
            ALC_HRTF_SOFT, ALC_FALSE, 0};
        context = alcCreateContext(device, attributes);
        CHECK(context != nullptr);
        setContext = reinterpret_cast<PFNALCSETTHREADCONTEXTPROC>(alcGetProcAddress(device, "alcSetThreadContext"));
        getContext = reinterpret_cast<PFNALCGETTHREADCONTEXTPROC>(alcGetProcAddress(device, "alcGetThreadContext"));
        renderSamples = reinterpret_cast<LPALCRENDERSAMPLESSOFT>(alcGetProcAddress(device, "alcRenderSamplesSOFT"));
        CHECK(setContext != nullptr && getContext != nullptr && renderSamples != nullptr);
        auto previous = getContext();
        CHECK(setContext(context));
        const auto bufferCallback = reinterpret_cast<LPALBUFFERCALLBACKSOFT>(alGetProcAddress("alBufferCallbackSOFT"));
        CHECK(bufferCallback != nullptr);
        alGenSources(1, &source);
        alGenBuffers(1, &buffer);
        bufferCallback(buffer, AL_FORMAT_STEREO_FLOAT32, 48000, callback, this);
        alSourcei(source, AL_BUFFER, buffer);
        alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcef(source, AL_ROLLOFF_FACTOR, 0);
        alSourcei(source, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);
        alSourcef(source, AL_GAIN, 0.25f);
        CHECK(alGetError() == AL_NO_ERROR);
        CHECK(setContext(previous));
    }

    void play()
    {
        auto previous = getContext();
        CHECK(setContext(context));
        alSourcePlay(source);
        CHECK(alGetError() == AL_NO_ERROR);
        CHECK(setContext(previous));
    }

    std::vector<short> render(int frames)
    {
        std::vector<short> output(frames * 2);
        renderSamples(device, output.data(), frames);
        CHECK(alcGetError(device) == ALC_NO_ERROR);
        return output;
    }

    ~ScalarMixReference()
    {
        auto previous = getContext();
        CHECK(setContext(context));
        alSourceStop(source);
        alSourcei(source, AL_BUFFER, 0);
        alDeleteSources(1, &source);
        alDeleteBuffers(1, &buffer);
        CHECK(alGetError() == AL_NO_ERROR);
        CHECK(setContext(previous));
        alcDestroyContext(context);
        CHECK(alcCloseDevice(device));
    }
};

static void scalarMixEquivalence(bool replace, bool cachedSpans = false)
{
    OpenALDevice device;
    CHECK(device.openLoopback());
    OpenALPcmStream stream;
    auto mode = OpenALPcmStream::InputMode::Queued;
    if (cachedSpans)
        mode = OpenALPcmStream::InputMode::CachedSpans;
    CHECK(stream.open(device, 48000, 1, 4096, true, mode));
    CHECK(stream.setGain(0.25f));
    ScalarMixReference reference;
    reference.input.resize(16000);
    for (size_t frame = 0; frame < reference.input.size(); ++frame)
        reference.input[frame] = static_cast<float>(1.5 * std::sin(frame * 0.071) + 0.125 * std::cos(frame * 0.137));
    reference.boundaries = {0, 1, 1, 2, 7, 333, 334, 4096, 4097, 4098, 6001, 8193, 12001};
    for (uint32_t boundary : reference.boundaries)
        CHECK(stream.schedulePortionBoundary(boundary));
    uint32_t submitted = 4096;
    CHECK(stream.queueFloat(reference.input.data(), submitted) == Result::Accepted);
    CHECK(stream.play());
    reference.play();
    bool replaced = false;
    for (unsigned iteration = 0; iteration < 60; ++iteration) {
        reference.leftGain = static_cast<float>((iteration * 7) % 17) / 16.0f;
        reference.rightGain = 1 - reference.leftGain;
        reference.frontBack = static_cast<float>((iteration * 3) % 11) / 10.0f;
        reference.surround = iteration % 5 >= 2;
        CHECK(stream.setStereoMix(reference.leftGain, reference.rightGain, reference.frontBack, reference.surround));
        if (replace && iteration == 17) {
            CHECK(stream.getReadFrames() == reference.read);
            reference.replaceAt = reference.read + 173;
            reference.tail.assign(reference.input.begin() + 1000, reference.input.begin() + 3049);
            CHECK(stream.replaceQueuedAfterFloat(reference.replaceAt, reference.tail.data(),
                static_cast<uint32_t>(reference.tail.size())));
            reference.replacing = true;
            replaced = true;
        }
        const auto actual = render(device, 333);
        const auto expected = reference.render(333);
        CHECK(actual == expected);
        CHECK(stream.getReadFrames() == reference.read);
        if (!replaced && submitted < reference.input.size()) {
            const uint32_t count = std::min(stream.availableFrames(),
                static_cast<uint32_t>(reference.input.size()) - submitted);
            if (count != 0) {
                CHECK(stream.queueFloat(reference.input.data() + submitted, count) == Result::Accepted);
                submitted += count;
            }
            if (submitted == reference.input.size())
                CHECK(stream.markEnd());
        }
    }
    CHECK(stream.state() == State::Finished);
}

static void scalarMixEquivalenceTests()
{
    scalarMixEquivalence(false);
    scalarMixEquivalence(true);
    std::puts("scalar-reference exact output: live modes, portion chunks, wraps and replacement passed");
}


static void directCachedScalarEquivalence(bool floatInput, bool replace)
{
    OpenALDevice device;
    CHECK(device.openLoopback());
    OpenALPcmStream stream;
    CHECK(stream.open(device, 48000, 1, 4096, true, OpenALPcmStream::InputMode::CachedSpans));
    CHECK(stream.setGain(0.25f));
    ScalarMixReference reference;
    auto shorts = std::make_shared<std::vector<short>>(16000);
    auto floats = std::make_shared<std::vector<float>>(16000);
    reference.input.resize(16000);
    for (size_t frame = 0; frame < reference.input.size(); ++frame) {
        (*shorts)[frame] = static_cast<short>(30000 * std::sin(frame * 0.071));
        (*floats)[frame] = static_cast<float>(1.5 * std::sin(frame * 0.071) + 0.125 * std::cos(frame * 0.137));
        if (floatInput)
            reference.input[frame] = (*floats)[frame];
        else
            reference.input[frame] = (*shorts)[frame] / 32768.0f;
    }
    reference.boundaries = {0, 1, 1, 2, 7, 333, 334, 4096, 4097, 6000, 6001, 6500, 8193, 12001};
    for (uint32_t boundary : reference.boundaries)
        CHECK(stream.schedulePortionBoundary(boundary));
    if (floatInput) {
        CHECK(stream.queueCachedFloat(floats, 0, 6000) == Result::Accepted);
        CHECK(stream.queueFloat(floats->data() + 6000, 500) == Result::Accepted);
        CHECK(stream.queueCachedFloat(floats, 6500, 9500) == Result::Accepted);
    } else {
        CHECK(stream.queueCached(shorts, 0, 6000) == Result::Accepted);
        CHECK(stream.queue(shorts->data() + 6000, 500) == Result::Accepted);
        CHECK(stream.queueCached(shorts, 6500, 9500) == Result::Accepted);
    }
    CHECK(stream.markEnd() && stream.play());
    reference.play();
    for (unsigned iteration = 0; iteration < 60; ++iteration) {
        reference.leftGain = static_cast<float>((iteration * 7) % 17) / 16.0f;
        reference.rightGain = 1 - reference.leftGain;
        reference.frontBack = static_cast<float>((iteration * 3) % 11) / 10.0f;
        reference.surround = iteration % 5 >= 2;
        CHECK(stream.setStereoMix(reference.leftGain, reference.rightGain, reference.frontBack, reference.surround));
        if (replace && iteration == 17) {
            CHECK(stream.getReadFrames() == reference.read);
            reference.replaceAt = reference.read + 173;
            reference.tail.assign(reference.input.begin() + 1000, reference.input.begin() + 3049);
            CHECK(stream.replaceQueuedAfterFloat(reference.replaceAt, reference.tail.data(),
                static_cast<uint32_t>(reference.tail.size())));
            reference.replacing = true;
        }
        CHECK(render(device, 333) == reference.render(333));
        CHECK(stream.getReadFrames() == reference.read);
    }
    CHECK(stream.state() == State::Finished);
}

static void directCachedLifetimeTests()
{
    OpenALDevice device;
    CHECK(device.openLoopback());
    OpenALPcmStream stream;
    CHECK(stream.open(device, 48000, 1, 4096, true, OpenALPcmStream::InputMode::CachedSpans));
    const auto ownerThread = std::this_thread::get_id();
    std::atomic<bool> destroyedOnOwner{false};
    auto input = std::shared_ptr<const std::vector<short>>(new std::vector<short>(144000, 6000),
        [&](const std::vector<short> *pcm) {
            destroyedOnOwner.store(std::this_thread::get_id() == ownerThread);
            delete pcm;
        });
    std::weak_ptr<const std::vector<short>> weak = input;
    CHECK(stream.queueCached(input, 0, 144000) == Result::Accepted);
    input.reset();
    CHECK(stream.markEnd() && stream.play());
    const auto uninterrupted = render(device, 108000);
    CHECK(stream.state() == State::Playing && stream.getReadFrames() >= 108000);
    for (unsigned frame = 1024; frame < 107000; ++frame) {
        CHECK(std::abs(static_cast<int>(uninterrupted[frame * 2]) - 3000) <= 1);
        CHECK(std::abs(static_cast<int>(uninterrupted[frame * 2 + 1]) - 3000) <= 1);
    }
    CHECK(!weak.expired());
    CHECK(stream.pause());
    silence(render(device, 512), 256);
    const uint32_t pausedAt = stream.getReadFrames();
    silence(render(device, 256));
    CHECK(stream.getReadFrames() == pausedAt && !weak.expired());
    CHECK(stream.play());
    render(device, 40000);
    CHECK(!weak.expired());
    std::thread other([&] { stream.reclaimConsumed(); });
    other.join();
    CHECK(!weak.expired());
    stream.reclaimConsumed();
    CHECK(weak.expired() && destroyedOnOwner.load());
    CHECK(stream.state() == State::Finished);

    input = std::make_shared<const std::vector<short>>(144000, 6000);
    weak = input;
    CHECK(stream.stop());
    CHECK(stream.queueCached(input, 0, 144000) == Result::Accepted);
    input.reset();
    CHECK(stream.play());
    render(device, 256);
    const uint32_t cutoff = stream.getReadFrames() + 173;
    std::vector<short> tail(128, -4000);
    CHECK(stream.replaceQueuedAfter(cutoff, tail.data(), 128));
    render(device, 1024);
    CHECK(stream.getReadFrames() == cutoff + 128);
    CHECK(!weak.expired());
    stream.reclaimConsumed();
    CHECK(weak.expired());
    CHECK(stream.stop());
    input = std::make_shared<const std::vector<short>>(1000, 2000);
    weak = input;
    CHECK(stream.queueCached(input, 0, 1000) == Result::Accepted);
    input.reset();
    CHECK(!weak.expired());
    CHECK(stream.stop() && weak.expired());
    CHECK(stream.close());
    CHECK(stream.open(device, 48000, 1, 16));
    CHECK(stream.queueCached(std::make_shared<const std::vector<short>>(32, 1), 0, 32) == Result::Invalid);
}

static void directCachedQueueTests()
{
    OpenALDevice device;
    CHECK(device.openLoopback());
    OpenALPcmStream stream;
    CHECK(stream.open(device, 48000, 2, 4, false, OpenALPcmStream::InputMode::CachedSpans));
    auto input = std::make_shared<const std::vector<short>>(sequence(0, 64));
    CHECK(stream.queueCached(nullptr, 0, 1) == Result::Invalid);
    CHECK(stream.queueCached(input, 65, 1) == Result::Invalid);
    CHECK(stream.queueCached(input, 63, 2) == Result::Invalid);
    CHECK(stream.queueCached(input, 0, 0) == Result::Invalid);
    CHECK(stream.queueCachedFloat(std::make_shared<const std::vector<float>>(64, 1.0f), 0, 1) == Result::Invalid);
    for (unsigned round = 0; round < 4; ++round) {
        for (unsigned segment = 0; segment < 5; ++segment)
            CHECK(stream.queueCached(input, segment, 1) == Result::Accepted);
        CHECK(stream.queueCached(input, 0, 1) == Result::Full);
        if (round == 0)
            CHECK(stream.play());
        render(device, 128);
        CHECK(stream.getReadFrames() == (round + 1) * 5);
    }
    CHECK(stream.stop());
    CHECK(stream.queueCached(input, 0, 64) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    const auto output = render(device, 256);
    for (size_t sample = 0; sample < input->size(); ++sample)
        CHECK(std::abs(static_cast<int>(output[sample]) - (*input)[sample]) <= 2);
}

static void directCachedTests()
{
    scalarMixEquivalence(false, true);
    scalarMixEquivalence(true, true);
    directCachedScalarEquivalence(false, false);
    directCachedScalarEquivalence(false, true);
    directCachedScalarEquivalence(true, false);
    directCachedScalarEquivalence(true, true);
    directCachedLifetimeTests();
    directCachedQueueTests();
    std::puts("cached spans exact scalar output, mixed ring ordering and owner-only lifetime passed");
}


struct CachedPinCounts
{
    std::thread::id owner = std::this_thread::get_id();
    std::atomic<unsigned> created{0}, destroyed{0}, wrongThread{0};
};

template<typename Sample>
static std::shared_ptr<const std::vector<Sample>> trackedCachedSamples(
    unsigned frames, Sample value, CachedPinCounts &counts)
{
    ++counts.created;
    return std::shared_ptr<const std::vector<Sample>>(new std::vector<Sample>(frames, value),
        [&counts](const std::vector<Sample> *samples) {
            if (std::this_thread::get_id() != counts.owner)
                ++counts.wrongThread;
            ++counts.destroyed;
            delete samples;
        });
}

static void concurrentCachedSpanStress()
{
    const char *driver = std::getenv("ALSOFT_DRIVERS");
    CHECK(driver != nullptr && std::strcmp(driver, "null") == 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    CachedPinCounts counts;
    OpenALDevice device;
    CHECK(device.open());
    OpenALPcmStream stream;
    constexpr unsigned capacity = 8;
    constexpr unsigned rounds = 4;
    CHECK(stream.open(device, 48000, 1, capacity, true, OpenALPcmStream::InputMode::CachedSpans));
    unsigned acceptedSegments = 0, fullResults = 0, totalConsumed = 0;
    const std::vector<short> copied(capacity, 2000);
    auto waitForSpace = [&] {
        CHECK(std::chrono::steady_clock::now() < deadline);
        CHECK(stream.state() != State::Error);
        stream.reclaimConsumed();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    for (unsigned round = 0; round < rounds; ++round) {
        std::vector<std::weak_ptr<const void>> pins;
        unsigned expectedFrames = 0;
        CHECK(stream.queue(copied.data(), capacity) == Result::Accepted);
        expectedFrames += capacity;
        ++acceptedSegments;
        for (unsigned segment = 0; segment < capacity; ++segment) {
            auto input = trackedCachedSamples(29, static_cast<short>(4000), counts);
            pins.emplace_back(input);
            CHECK(stream.queueCached(input, 0, 29) == Result::Accepted);
            expectedFrames += 29;
            ++acceptedSegments;
        }
        auto rejected = trackedCachedSamples(1, static_cast<short>(4000), counts);
        CHECK(stream.queueCached(rejected, 0, 1) == Result::Full);
        ++fullResults;
        rejected.reset();
        CHECK(stream.queue(copied.data(), 1) == Result::Full);
        ++fullResults;
        CHECK(stream.play());
        for (unsigned segment = 0; segment < 192; ++segment) {
            Result result = Result::Full;
            if (segment % 2 == 0) {
                do {
                    result = stream.queue(copied.data(), 3);
                    CHECK(result == Result::Accepted || result == Result::Full);
                    if (result == Result::Full) {
                        ++fullResults;
                        waitForSpace();
                    }
                } while (result == Result::Full);
                expectedFrames += 3;
            } else if (segment % 4 == 1) {
                auto input = trackedCachedSamples(29, static_cast<short>(4000), counts);
                pins.emplace_back(input);
                do {
                    result = stream.queueCached(input, 0, 29);
                    CHECK(result == Result::Accepted || result == Result::Full);
                    if (result == Result::Full) {
                        ++fullResults;
                        waitForSpace();
                    }
                } while (result == Result::Full);
                expectedFrames += 29;
            } else {
                auto input = trackedCachedSamples(29, 0.125f, counts);
                pins.emplace_back(input);
                do {
                    result = stream.queueCachedFloat(input, 0, 29);
                    CHECK(result == Result::Accepted || result == Result::Full);
                    if (result == Result::Full) {
                        ++fullResults;
                        waitForSpace();
                    }
                } while (result == Result::Full);
                expectedFrames += 29;
            }
            ++acceptedSegments;
            if (segment % 7 == 0)
                stream.reclaimConsumed();
        }
        CHECK(stream.markEnd());
        while (stream.state() != State::Finished)
            waitForSpace();
        CHECK(stream.getReadFrames() == expectedFrames);
        totalConsumed += expectedFrames;
        stream.reclaimConsumed();
        for (const auto &pin : pins)
            CHECK(pin.expired());
        CHECK(counts.wrongThread.load() == 0);
        CHECK(counts.created.load() == counts.destroyed.load());
        CHECK(stream.stop() && stream.getReadFrames() == 0);
        auto stopped = trackedCachedSamples(64, static_cast<short>(1000), counts);
        std::weak_ptr<const void> stoppedPin = stopped;
        CHECK(stream.queueCached(stopped, 0, 64) == Result::Accepted);
        CHECK(stream.queue(copied.data(), capacity) == Result::Accepted);
        stopped.reset();
        CHECK(!stoppedPin.expired());
        CHECK(stream.stop() && stoppedPin.expired());
    }
    CHECK(acceptedSegments >= (capacity + 2) * 64);
    CHECK(fullResults >= rounds * 2);
    CHECK(totalConsumed == rounds * (capacity + capacity * 29 + 96 * 3 + 96 * 29));
    CHECK(counts.wrongThread.load() == 0 && counts.created.load() == counts.destroyed.load());
    CHECK(stream.close() && device.close());
    std::printf("async cached spans: %u segments, %u Full results, %u exact frames, %u owner-only releases\n",
        acceptedSegments, fullResults, totalConsumed, counts.destroyed.load());
}

static void directCachedNullStall()
{
    OpenALDevice device;
    CHECK(device.open());
    OpenALPcmStream stream;
    CHECK(stream.open(device, 48000, 1, 4096, true, OpenALPcmStream::InputMode::CachedSpans));
    auto input = std::make_shared<const std::vector<short>>(192000, 2000);
    CHECK(stream.queueCached(input, 0, 192000) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    std::this_thread::sleep_for(std::chrono::milliseconds(2250));
    CHECK(stream.getReadFrames() >= 96000);
    CHECK(stream.state() == State::Playing);
    CHECK(stream.stop());
    std::puts("cached current clip continues during a 2.25 second owner stall on Null backend");
}

static void floatMixHeadroomTests()
{
    OpenALDevice device;
    CHECK(device.openLoopback());
    OpenALPcmStream stream;
    CHECK(stream.open(device, 48000, 1, 8192, true));
    CHECK(stream.setStereoMix(1, 0, 0, false));
    CHECK(stream.setGain(0.125f));
    CHECK(stream.schedulePortionBoundary(0));
    std::vector<float> input(8192, 2.0f);
    CHECK(stream.queueFloat(input.data(), 8192) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    auto output = render(device, 512);
    CHECK(stream.getReadFrames() < 4096);
    std::vector<float> decay(2048, -1.5f);
    CHECK(stream.replaceQueuedAfterFloat(4096, decay.data(), 2048));
    const auto tail = render(device, 7000);
    output.insert(output.end(), tail.begin(), tail.end());
    for (unsigned frame = 1200; frame < 3800; ++frame)
        CHECK(std::abs(static_cast<int>(output[frame * 2]) - 8192) <= 1);
    for (unsigned frame = 4300; frame < 6000; ++frame)
        CHECK(std::abs(static_cast<int>(output[frame * 2]) + 6144) <= 1);
    CHECK(stream.getReadFrames() == 6144);
    CHECK(stream.stop());
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    CHECK(stream.queueFloat(&invalid, 1) == Result::Invalid);
    CHECK(stream.close());
    CHECK(stream.open(device, 48000, 2, 1024));
    CHECK(stream.queueFloat(input.data(), 128) == Result::Invalid);
    CHECK(!stream.setStereoMix(0.5f, 0.5f, 0, false));
    CHECK(!stream.replaceQueuedAfterFloat(0, decay.data(), 128));
    CHECK(stream.close());
    CHECK(stream.open(device, 48000, 1, 4096, true));
    CHECK(stream.setStereoMix(0.5f, 0.5f, 1, true));
    std::vector<float> initial(512, 0.25f);
    CHECK(stream.queueFloat(initial.data(), 512) == Result::Accepted);
    CHECK(stream.play());
    render(device, 2048);
    CHECK(stream.state() == State::Starved);
    std::vector<float> zero(128, 0);
    CHECK(stream.queueFloat(zero.data(), 128) == Result::Accepted);
    CHECK(stream.markEnd());
    silence(render(device, 2048));
    CHECK(stream.getReadFrames() == 640);
    std::puts("float headroom/replacement/typed input and starvation-history checks passed");
}

static void outputModeTests()
{
    using Mode = OpenALDevice::OutputMode;
    OpenALDevice device;
    CHECK(device.openLoopback());
    CHECK(device.getOutputMode() == Mode::Stereo);
    OpenALPcmStream stream;
    CHECK(stream.open(device, 48000, 2, 8192));
    const auto input = sequence(0, 8192);
    CHECK(stream.queue(input.data(), 8192) == Result::Accepted);
    CHECK(stream.markEnd() && stream.play());
    render(device, 512);
    const auto read = stream.getReadFrames();
    CHECK(read > 0);
    CHECK(device.setOutputMode(Mode::Headphones));
    CHECK(device.getOutputMode() == Mode::Headphones);
    CHECK(stream.getReadFrames() == read);
    CHECK(stream.state() == State::Playing);
    render(device, 512);
    CHECK(stream.getReadFrames() > read);
    const auto afterHeadphones = stream.getReadFrames();
    CHECK(device.setOutputMode(Mode::Stereo));
    CHECK(device.getOutputMode() == Mode::Stereo);
    CHECK(stream.getReadFrames() == afterHeadphones);
    CHECK(!device.setOutputMode(Mode::Unknown));
    CHECK(device.getOutputMode() == Mode::Stereo);
    while (stream.state() != State::Finished) {
        CHECK(stream.state() == State::Playing);
        render(device, 256);
    }
    CHECK(stream.getReadFrames() == 8192);
    CHECK(!device.setOutputMode(Mode::Quad));
    CHECK(device.getOutputMode() == Mode::Stereo);
    CHECK(device.setOutputMode(Mode::SurroundStereo));
    CHECK(device.getOutputMode() == Mode::SurroundStereo);
    CHECK(device.setOutputMode(Mode::Stereo));
    CHECK(device.getOutputMode() == Mode::Stereo);
    std::puts("explicit output modes and live voice preservation passed");
}

int main(int argc, char **argv)
{
    av_log_set_level(AV_LOG_ERROR);
    if (argc == 2 && std::strcmp(argv[1], "--output-modes") == 0) {
        outputModeTests();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--stress") == 0) {
        concurrentCachedSpanStress();
        directCachedNullStall();
        concurrentMixedControls();
        concurrentNullStress();
        return 0;
    }
    CHECK(argc == 2);
    directCachedTests();
    scalarMixEquivalenceTests();
    legacyMixTests();
    mixedBoundaryTests();
    floatMixHeadroomTests();
    outputModeTests();
    queueAndWrap();
    controlsAndStarvation();
    pauseContinuity();
    parametersAndReplacement();
    replacementEdges();
    monoResampling();
    contextsAndLifetime();
    decoderBridge(argv[1]);
    std::puts("OpenAL loopback tests passed");
    return 0;
}
