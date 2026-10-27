#include "VideoDevice/FFmpeg/FFmpegFile.h"
#include "VideoDevice/FFmpeg/FFmpegAudioFile.h"
#include "Common/File.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/avutil.h>
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

class TestFile : public File
{
public:
    static int live;
    bool failReads = false;
    std::vector<unsigned char> data;
    int cursor = 0;
    TestFile(std::vector<unsigned char> bytes) : data(std::move(bytes)) { ++live; }
    ~TestFile() { --live; }
    Int read(void *buffer, Int bytes) override
    {
        if (failReads)
            return -1;
        bytes = std::min(bytes, size() - cursor);
        std::memcpy(buffer, data.data() + cursor, bytes);
        cursor += bytes;
        return bytes;
    }
    Int seek(Int bytes, seekMode mode) override
    {
        long long target = bytes;
        if (mode == CURRENT)
            target += cursor;
        if (mode == END)
            target += size();
        if (target < 0 || target > size())
            return -1;
        cursor = static_cast<int>(target);
        return cursor;
    }
    Int size() override { return static_cast<Int>(data.size()); }
    Int position() override { return cursor; }
    void close() override { delete this; }
};
int TestFile::live = 0;

class EntryFile : public File
{
    std::FILE *handle;
    long long start;
    Int length;
    Int cursor = 0;
public:
    EntryFile(const char *path, long long offset, Int size) : handle(std::fopen(path, "rb")), start(offset), length(size)
    {
        CHECK(handle != nullptr);
        CHECK(_fseeki64(handle, start, SEEK_SET) == 0);
    }
    Int read(void *buffer, Int bytes) override
    {
        bytes = std::min(bytes, length - cursor);
        const Int count = static_cast<Int>(std::fread(buffer, 1, bytes, handle));
        if (std::ferror(handle))
            return -1;
        cursor += count;
        return count;
    }
    Int seek(Int offset, seekMode mode) override
    {
        long long target = offset;
        if (mode == CURRENT)
            target += cursor;
        if (mode == END)
            target += length;
        if (target < 0 || target > length || _fseeki64(handle, start + target, SEEK_SET) != 0)
            return -1;
        cursor = static_cast<Int>(target);
        return cursor;
    }
    Int size() override { return length; }
    Int position() override { return cursor; }
    void close() override { std::fclose(handle); delete this; }
};

static void put16(std::vector<unsigned char> &data, unsigned value)
{
    data.push_back(value & 255);
    data.push_back((value >> 8) & 255);
}
static void put32(std::vector<unsigned char> &data, unsigned value)
{
    put16(data, value & 65535);
    put16(data, value >> 16);
}
static std::vector<unsigned char> wave(int samples, int channels = 1)
{
    std::vector<unsigned char> data;
    const char *riff = "RIFF";
    data.insert(data.end(), riff, riff + 4);
    put32(data, samples * channels * 2 + 36);
    const char *format = "WAVEfmt ";
    data.insert(data.end(), format, format + 8);
    put32(data, 16); put16(data, 1); put16(data, channels);
    put32(data, 8000); put32(data, 16000 * channels); put16(data, 2 * channels); put16(data, 16);
    const char *chunk = "data";
    data.insert(data.end(), chunk, chunk + 4);
    put32(data, samples * channels * 2);
    for (int i = 0; i < samples; ++i) {
        put16(data, static_cast<unsigned short>(i % 2000 - 1000));
        if (channels == 2)
            put16(data, static_cast<unsigned short>(12000 - i % 3000));
    }
    return data;
}

static void monoUpmixTests()
{
    FFmpegAudioFile audio;
    Int64 received = 0;
    audio.setPcmCallback([&](const short *pcm, Int count) {
        for (Int i = 0; i < count; ++i) {
            const short expected = static_cast<short>((received + i) % 2000 - 1000);
            CHECK(pcm[2 * i] == expected);
            CHECK(pcm[2 * i + 1] == expected);
        }
        received += count;
    });
    CHECK(audio.open(new TestFile(wave(8000)), 8000, 2));
    for (Int pass = 0; pass < 2; ++pass) {
        received = 0;
        while (audio.decodePacket()) {}
        CHECK(!audio.hasError() && received == 8000);
        CHECK(audio.rewind());
    }
    audio.close();
    CHECK(TestFile::live == 0);

    FFmpegPcmConverter converter;
    for (Int pass = 0; pass < 3; ++pass) {
        if (pass == 1)
            CHECK(converter.configure(8000, 2, FFmpegPcmConverter::MonoUpmix::Duplicate));
        else
            CHECK(converter.configure(8000, 2));
        received = 0;
        converter.setPcmCallback([&](const short *pcm, Int count) {
            for (Int i = 0; i < count; ++i) {
                double expected = static_cast<short>((received + i) % 2000 - 1000);
                if (pass != 1)
                    expected /= std::sqrt(2.0);
                CHECK(std::abs(pcm[2 * i] - expected) <= 1.0);
                CHECK(pcm[2 * i] == pcm[2 * i + 1]);
            }
            received += count;
        });
        FFmpegFile source;
        CHECK(source.open(new TestFile(wave(8000))));
        source.setFrameCallback([&](AVFrame *frame, int, int type, void *) {
            CHECK(type == AVMEDIA_TYPE_AUDIO);
            CHECK(converter.convert(frame));
        });
        while (source.decodePacket()) {}
        CHECK(converter.drain());
        CHECK(!source.hasError() && !converter.hasError() && received == 8000);
    }
    CHECK(TestFile::live == 0);
    CHECK(converter.configure(8000, 2, FFmpegPcmConverter::MonoUpmix::Duplicate));
    for (Int inputChannels : {1, 2, 1}) {
        received = 0;
        converter.setPcmCallback([&](const short *pcm, Int count) {
            for (Int i = 0; i < count; ++i) {
                const short left = static_cast<short>((received + i) % 2000 - 1000);
                short right = left;
                if (inputChannels == 2)
                    right = static_cast<short>(12000 - (received + i) % 3000);
                CHECK(pcm[2 * i] == left);
                CHECK(pcm[2 * i + 1] == right);
            }
            received += count;
        });
        FFmpegFile source;
        CHECK(source.open(new TestFile(wave(8000, inputChannels))));
        source.setFrameCallback([&](AVFrame *frame, int, int type, void *) {
            CHECK(type == AVMEDIA_TYPE_AUDIO);
            CHECK(converter.convert(frame));
        });
        while (source.decodePacket()) {}
        CHECK(converter.drain());
        CHECK(!source.hasError() && !converter.hasError() && received == 8000);
    }
    CHECK(TestFile::live == 0);
    std::puts("standalone mono duplication and default converter equal-power remix passed");
}

static std::vector<unsigned char> filterWave(int rate, bool step = false)
{
    auto data = wave(rate);
    for (Int offset : {24, 28}) {
        const unsigned value = rate * (offset == 28 ? 2 : 1);
        for (Int byte = 0; byte < 4; ++byte)
            data[offset + byte] = static_cast<unsigned char>(value >> (8 * byte));
    }
    for (Int sample = 0; sample < rate; ++sample) {
        double value = 0;
        if (step) {
            if (sample >= rate / 4)
                value = 32700;
        } else {
            for (Int frequency : {250, 1000, 4000, 8000})
                value += 3000 * std::sin(2.0 * 3.141592653589793 * frequency * sample / rate);
        }
        const short encoded = static_cast<short>(std::lround(value));
        data[44 + 2 * sample] = static_cast<unsigned char>(encoded);
        data[45 + 2 * sample] = static_cast<unsigned char>(encoded >> 8);
    }
    return data;
}

static double expectedLowPassGain(double cutoff, double frequency, double sourceRate)
{
    const double c = 1.0 / std::tan(3.141592653589793 * cutoff / 2.0);
    const double b0 = 1.0 / (1.0 + std::sqrt(2.0) * c + c * c);
    const double a1 = std::round(2.0 * (1.0 - c * c) * b0 * 4096.0) / 4096.0;
    const double a2 = std::round((1.0 - std::sqrt(2.0) * c + c * c) * b0 * 4096.0) / 4096.0;
    const double angle = 2.0 * 3.141592653589793 * frequency / sourceRate;
    const double realNumerator = b0 * (1.0 + 2.0 * std::cos(angle) + std::cos(2.0 * angle));
    const double imagNumerator = -b0 * (2.0 * std::sin(angle) + std::sin(2.0 * angle));
    const double realDenominator = 1.0 + a1 * std::cos(angle) + a2 * std::cos(2.0 * angle);
    const double imagDenominator = -a1 * std::sin(angle) - a2 * std::sin(2.0 * angle);
    return std::sqrt((realNumerator * realNumerator + imagNumerator * imagNumerator) /
        (realDenominator * realDenominator + imagDenominator * imagDenominator));
}

static double toneAmplitude(const std::vector<float> &pcm, int frequency)
{
    double real = 0;
    double imaginary = 0;
    const Int start = 9600;
    const Int end = 38400;
    for (Int sample = start; sample < end; ++sample) {
        const double angle = 2.0 * 3.141592653589793 * frequency * sample / 48000;
        real += pcm[sample] * std::cos(angle);
        imaginary += pcm[sample] * std::sin(angle);
    }
    return 2.0 * std::sqrt(real * real + imaginary * imaginary) / (end - start);
}

static std::vector<float> convertFilterFrames(bool changeFormat)
{
    FFmpegPcmConverter converter;
    CHECK(converter.configure(44100, 1, FFmpegPcmConverter::MonoUpmix::EqualPower, 0.26f));
    std::vector<float> output;
    CHECK(converter.setFloatPcmCallback([&](const float *pcm, Int count) {
        output.insert(output.end(), pcm, pcm + count);
    }));
    AVFrame *empty = av_frame_alloc();
    CHECK(empty != nullptr);
    empty->sample_rate = 44100;
    empty->format = AV_SAMPLE_FMT_S16;
    av_channel_layout_default(&empty->ch_layout, 1);
    CHECK(converter.convert(empty));
    av_frame_free(&empty);
    for (Int block = 0; block < 8; ++block) {
        AVFrame *frame = av_frame_alloc();
        CHECK(frame != nullptr);
        frame->sample_rate = 44100;
        frame->format = AV_SAMPLE_FMT_S16;
        if (changeFormat && block >= 4)
            frame->format = AV_SAMPLE_FMT_FLTP;
        frame->nb_samples = 128;
        av_channel_layout_default(&frame->ch_layout, 1);
        CHECK(av_frame_get_buffer(frame, 0) == 0);
        for (Int index = 0; index < 128; ++index) {
            const short sample = static_cast<short>(10000 * std::sin(
                2.0 * 3.141592653589793 * 1700 * (block * 128 + index) / 44100));
            if (frame->format == AV_SAMPLE_FMT_S16)
                reinterpret_cast<short *>(frame->data[0])[index] = sample;
            else
                reinterpret_cast<float *>(frame->data[0])[index] = sample / 32768.0f;
        }
        CHECK(converter.convert(frame));
        av_frame_free(&frame);
    }
    CHECK(converter.drain() && !converter.hasError());
    CHECK(output.size() == 1024);
    return output;
}

static void sourceFilterTests()
{
    const auto fixedFormat = convertFilterFrames(false);
    const auto changedFormat = convertFilterFrames(true);
    CHECK(fixedFormat.size() == changedFormat.size());
    for (size_t index = 0; index < fixedFormat.size(); ++index)
        CHECK(std::abs(fixedFormat[index] - changedFormat[index]) < 0.000001f);
    for (Int sourceRate : {22050, 44100}) {
        std::vector<float> output;
        FFmpegAudioFile audio;
        CHECK(audio.open(new TestFile(filterWave(sourceRate)), 48000, 1, 0.26f));
        CHECK(audio.setFloatPcmCallback([&](const float *pcm, Int count) {
            output.insert(output.end(), pcm, pcm + count);
        }));
        while (audio.decodePacket()) {}
        CHECK(!audio.hasError() && output.size() == 48000);
        for (Int frequency : {250, 1000, 4000, 8000}) {
            const double actual = toneAmplitude(output, frequency) / (3000.0 / 32768);
            const double expected = expectedLowPassGain(0.26f, frequency, sourceRate);
            CHECK(std::abs(actual - expected) < 0.003);
        }
        const auto first = output;
        CHECK(!audio.setFloatPcmCallback(nullptr));
        CHECK(audio.rewind());
        output.clear();
        while (audio.decodePacket()) {}
        CHECK(!audio.hasError() && output == first);
    }
    FFmpegAudioFile headroom;
    CHECK(headroom.open(new TestFile(filterWave(44100, true)), 48000, 1, 0.1f));
    float peak = 0;
    CHECK(headroom.setFloatPcmCallback([&](const float *pcm, Int count) {
        for (Int sample = 0; sample < count; ++sample)
            peak = std::max(peak, pcm[sample]);
    }));
    while (headroom.decodePacket()) {}
    CHECK(!headroom.hasError() && peak > 1.02f && peak < 1.1f);
    std::printf("source-rate lowpass and float headroom passed (peak %.6f)\n", peak);
    CHECK(!headroom.open(new TestFile(wave(8)), 8000, 1, NAN));
    headroom.close();
    CHECK(TestFile::live == 0);
}

static void unitTests()
{
    sourceFilterTests();
    monoUpmixTests();
    FFmpegAudioFile audio;
    Int64 received = 0;
    audio.setPcmCallback([&](const short *pcm, Int samples) {
        for (Int i = 0; i < samples; ++i)
            CHECK(pcm[i] == static_cast<short>((received + i) % 2000 - 1000));
        received += samples;
    });
    CHECK(audio.open(new TestFile(wave(8000)), 8000, 1));
    while (audio.decodePacket()) {}
    CHECK(!audio.hasError() && received == 8000 && audio.getDecodedSampleCount() == 8000);
    CHECK(audio.getDecodedDurationMS() == 1000.0);
    CHECK(audio.rewind()); received = 0;
    while (audio.decodePacket()) {}
    CHECK(!audio.hasError() && received == 8000);
    audio.close(); audio.close(); CHECK(TestFile::live == 0);
    CHECK(!audio.open(new TestFile(wave(8000)), 8000, 3)); CHECK(TestFile::live == 0);
    received = 0;
    audio.setPcmCallback([&](const short *pcm, Int samples) {
        for (Int i = 0; i < samples; ++i) {
            CHECK(pcm[2 * i] == static_cast<short>((received + i) % 2000 - 1000));
            CHECK(pcm[2 * i + 1] == static_cast<short>(12000 - (received + i) % 3000));
        }
        received += samples;
    });
    CHECK(audio.open(new TestFile(wave(8000, 2)), 8000, 2));
    while (audio.decodePacket()) {}
    CHECK(!audio.hasError() && received == 8000);
    audio.close(); CHECK(TestFile::live == 0);
    FFmpegFile file;
    CHECK(!file.open(new TestFile({0, 1, 2, 3}))); CHECK(TestFile::live == 0);
    CHECK(file.open(new TestFile(wave(8000))));
    CHECK(file.open(new TestFile(wave(4000)))); CHECK(TestFile::live == 1);
    file.close(); CHECK(TestFile::live == 0);
    TestFile *failing = new TestFile(wave(1000000));
    CHECK(file.open(failing)); failing->failReads = true;
    while (file.decodePacket()) {}
    CHECK(file.hasError()); failing->failReads = false;
    CHECK(file.rewind()); CHECK(!file.hasError());
    while (file.decodePacket()) {}
    CHECK(!file.hasError()); file.close(); CHECK(TestFile::live == 0);
    FFmpegAudioFile first, second;
    CHECK(first.open(new TestFile(wave(8000)), 48000, 2));
    CHECK(second.open(new TestFile(wave(4000)), 16000, 1));
    while (first.decodePacket() | second.decodePacket()) {}
    CHECK(!first.hasError() && !second.hasError());
    CHECK(first.getDecodedSampleCount() == 48000 && second.getDecodedSampleCount() == 8000);
    first.close(); second.close(); CHECK(TestFile::live == 0);
    std::puts("unit tests passed");
}

static void gaplessTests(const char *path)
{
    std::FILE *input = std::fopen(path, "rb");
    CHECK(input != nullptr);
    CHECK(std::fseek(input, 0, SEEK_END) == 0);
    const Int size = static_cast<Int>(std::ftell(input));
    std::fclose(input);
    FFmpegFile file;
    Int64 samples = 0;
    file.setFrameCallback([&](AVFrame *frame, int, int type, void *) {
        CHECK(type == AVMEDIA_TYPE_AUDIO);
        samples += frame->nb_samples;
    });
    CHECK(file.open(new EntryFile(path, 0, size)));
    for (Int pass = 0; pass < 2; ++pass) {
        samples = 0;
        while (file.decodePacket()) {}
        CHECK(!file.hasError() && samples == 44100);
        CHECK(file.rewind());
    }
    FFmpegAudioFile audio;
    CHECK(audio.open(new EntryFile(path, 0, size), 48000, 2));
    Int64 converted = 0;
    double errorSquared = 0;
    double referenceSquared = 0;
    Int compared = 0;
    audio.setPcmCallback([&](const short *pcm, Int count) {
        for (Int i = 0; i < count; ++i) {
            const Int64 index = converted + i;
            CHECK(pcm[2 * i] == pcm[2 * i + 1]);
            // Exclude codec/resampler edge transients, not encoder delay (tested by sample counts).
            if (index >= 4800 && index < 43200) {
                const double reference = 10000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * index / 48000.0);
                const double error = pcm[2 * i] - reference;
                errorSquared += error * error;
                referenceSquared += reference * reference;
                ++compared;
            }
        }
        converted += count;
    });
    for (Int pass = 0; pass < 2; ++pass) {
        converted = 0; errorSquared = 0; referenceSquared = 0; compared = 0;
        while (audio.decodePacket()) {}
        CHECK(compared == 38400 && converted == 48000);
        const double relativeRmsError = std::sqrt(errorSquared / referenceSquared);
        CHECK(relativeRmsError < 0.05);
        std::printf("compressed content relative RMS error: %.6f\n", relativeRmsError);
        CHECK(!audio.hasError() && audio.getDecodedSampleCount() == 48000);
        CHECK(audio.getDecodedDurationMS() == 1000.0);
        CHECK(audio.rewind());
    }
    std::puts("gapless trimming and rewind passed");
}

int main(int argc, char **argv)
{
    av_log_set_level(AV_LOG_ERROR);
    if (argc == 1) { unitTests(); return 0; }
    if (argc == 3 && std::strcmp(argv[1], "--gapless") == 0) { gaplessTests(argv[2]); return 0; }
    CHECK(argc == 4);
    const long long offset = _strtoi64(argv[2], nullptr, 10);
    const Int size = std::atoi(argv[3]);
    FFmpegFile file;
    CHECK(file.open(new EntryFile(argv[1], offset, size)));
    Int64 videoFrames = 0, audioSamples = 0;
    file.setFrameCallback([&](AVFrame *frame, int, int type, void *) {
        if (type == AVMEDIA_TYPE_VIDEO)
            ++videoFrames;
        if (type == AVMEDIA_TYPE_AUDIO)
            audioSamples += frame->nb_samples;
    });
    while (file.decodePacket()) {}
    CHECK(!file.hasError());
    const Bool hasAudio = file.hasAudio();
    if (videoFrames > 1) {
        Int64 timestamp = -1;
        file.setFrameCallback([&](AVFrame *frame, int, int type, void *) {
            if (type == AVMEDIA_TYPE_VIDEO)
                timestamp = frame->best_effort_timestamp;
        });
        file.seekFrame(1);
        while (timestamp < 0 && file.decodePacket()) {}
        CHECK(!file.hasError() && timestamp >= 0 && timestamp <= 1);
    }
    CHECK(file.rewind());
    file.setFrameCallback(nullptr);
    CHECK(file.decodePacket());
    file.close();
    Int64 pcmSamples = 0;
    double duration = 0;
    if (hasAudio) {
        FFmpegAudioFile audio;
        audio.setPcmCallback([&](const short *, Int samples) { pcmSamples += samples; });
        CHECK(audio.open(new EntryFile(argv[1], offset, size), 48000, 2));
        while (audio.decodePacket()) {}
        CHECK(!audio.hasError() && pcmSamples > 0 && pcmSamples == audio.getDecodedSampleCount());
        duration = audio.getDecodedDurationMS();
    }
    std::printf("{\"video_frames\":%lld,\"audio_samples\":%lld,\"pcm_samples\":%lld,\"duration_ms\":%.9f}\n",
        videoFrames, audioSamples, pcmSamples, duration);
    return 0;
}
