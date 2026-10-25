#include "OpenALAudioDevice/OpenALDevice.h"
#include "OpenALAudioDevice/OpenALPcmStream.h"
#include "VideoDevice/FFmpeg/FFmpegAudioFile.h"
#include "Common/File.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
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
        CHECK(stream.stop());
        CHECK(stream.availableFrames() == 1024);
        CHECK(stream.queue(input.data(), 127) == Result::Accepted);
        CHECK(stream.markEnd() && stream.play());
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        CHECK(device.close());
        CHECK(stream.close());
    }
    CHECK(consumed > 0);
    std::printf("null mixer concurrent progress observations: %d\n", consumed);
}

int main(int argc, char **argv)
{
    av_log_set_level(AV_LOG_ERROR);
    if (argc == 2 && std::strcmp(argv[1], "--stress") == 0) {
        concurrentNullStress();
        return 0;
    }
    CHECK(argc == 2);
    queueAndWrap();
    controlsAndStarvation();
    pauseContinuity();
    monoResampling();
    contextsAndLifetime();
    decoderBridge(argv[1]);
    std::puts("OpenAL loopback tests passed");
    return 0;
}
