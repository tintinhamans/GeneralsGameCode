#include "Utility/CppMacros.h"
#define NOMINMAX
#include <windows.h>
#include "Common/AudioAffect.h"
#include "Common/AudioSettings.h"
#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "OpenALAudioDevice/OpenALAudioManager.h"
#include "OpenALAudioDevice/OpenALMovieVoice.h"
#include "VideoDevice/FFmpeg/FFmpegVideoPlayer.h"
#include "VideoDevice/FFmpeg/FFmpegFile.h"
#include "VideoDevice/FFmpeg/FFmpegAudioFile.h"
#include "VideoDevice/FFmpeg/FFmpegPcmConverter.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "Win32Device/Common/Win32LocalFile.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/buffer.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

class FixtureAudio : public OpenALAudioManager
{
public:
    void openDevice() override { CHECK(m_device.openLoopback(48000)); }
    void setup()
    {
        m_systemMusicVolume = m_systemSoundVolume = m_systemSound3DVolume = m_systemSpeechVolume = 1;
        AudioManager::reset();
        m_audioSettings->m_outputRate = 48000;
        m_audioSettings->m_useDigital = true;
        openDevice();
    }
    void tick() { updatePlayback(); }
    std::vector<short> render(unsigned frames)
    {
        std::vector<short> result(static_cast<size_t>(frames) * 2);
        CHECK(m_device.render(result.data(), frames));
        return result;
    }
};

class FixturePlayer : public FFmpegVideoPlayer
{
    FixtureAudio &m_audio;
protected:
    size_t videoQueueByteLimit() const override { return queueByteLimit; }
    std::shared_ptr<OpenALMovieVoice> createAudioVoice() override
    {
        auto voice = m_audio.createMovieVoice();
        voices.push_back(voice);
        return voice;
    }
public:
    explicit FixturePlayer(FixtureAudio &audio) : m_audio(audio) {}
    std::vector<std::weak_ptr<OpenALMovieVoice>> voices;
    size_t queueByteLimit = 64 * 1024 * 1024;
    VideoStreamInterface *openOwned(File *file) { return createStream(file); }
    VideoStreamInterface *openFile(const char *filename)
    {
        return createStream(TheFileSystem->openFile(filename));
    }
};

class MemoryVideo : public VideoBuffer
{
public:
    std::vector<unsigned char> bytes;
    MemoryVideo() : VideoBuffer(TYPE_R8G8B8) {}
    Bool allocate(UnsignedInt width, UnsignedInt height) override
    {
        m_width = width; m_height = height; m_pitch = width * 3;
        m_textureWidth = width; m_textureHeight = height;
        bytes.resize(static_cast<size_t>(m_pitch) * height);
        return true;
    }
    void free() override { bytes.clear(); }
    void *lock() override { return bytes.data(); }
    void unlock() override {}
    Bool valid() override { return !bytes.empty(); }
};

static uint64_t hash(const std::vector<unsigned char> &bytes)
{
    uint64_t value = 14695981039346656037ull;
    for (unsigned char byte : bytes)
        value = (value ^ byte) * 1099511628211ull;
    return value;
}

struct Reference
{
    std::vector<uint64_t> video;
    std::vector<short> audio;
    int width = 0;
    int height = 0;
    size_t largestFrameBytes = 0;
};

static Reference reference(const char *filename)
{
    Reference result;
    FFmpegFile file;
    CHECK(file.open(TheFileSystem->openFile(filename)));
    result.width = file.getWidth(); result.height = file.getHeight();
    SwsContext *scale = nullptr;
    MemoryVideo buffer;
    CHECK(buffer.allocate(result.width, result.height));
    file.setFrameCallback([&](AVFrame *frame, int, int type, void *) {
        if (type != AVMEDIA_TYPE_VIDEO)
            return;
        size_t frameBytes = 0;
        for (const auto *buffer : frame->buf) {
            if (buffer != nullptr)
                frameBytes += buffer->size;
        }
        for (int i = 0; i < frame->nb_extended_buf; ++i)
            frameBytes += frame->extended_buf[i]->size;
        result.largestFrameBytes = std::max(result.largestFrameBytes, frameBytes);
        scale = sws_getCachedContext(scale, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format), result.width, result.height,
            AV_PIX_FMT_RGB24, SWS_BICUBIC, nullptr, nullptr, nullptr);
        CHECK(scale != nullptr);
        unsigned char *destination[] = {buffer.bytes.data()};
        int strides[] = {static_cast<int>(buffer.pitch())};
        CHECK(sws_scale(scale, frame->data, frame->linesize, 0, frame->height, destination, strides) > 0);
        result.video.push_back(hash(buffer.bytes));
    });
    while (file.decodePacket()) {}
    CHECK(!file.hasError() && !result.video.empty());
    sws_freeContext(scale);
    if (file.hasAudio()) {
        FFmpegAudioFile audio;
        CHECK(audio.open(TheFileSystem->openFile(filename), 48000, 2));
        audio.setPcmCallback([&](const short *pcm, int frames) {
            result.audio.insert(result.audio.end(), pcm, pcm + frames * 2);
        });
        while (audio.decodePacket()) {}
        CHECK(!audio.hasError() && !result.audio.empty());
    }
    return result;
}

static double energy(const std::vector<short> &samples, size_t first = 0)
{
    double sum = 0;
    for (size_t i = first; i < samples.size(); ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return sum;
}

static AVFrame *constantFrame(AVSampleFormat format, int rate, int channels, int samples)
{
    AVFrame *frame = av_frame_alloc();
    CHECK(frame != nullptr);
    frame->format = format; frame->sample_rate = rate; frame->nb_samples = samples;
    av_channel_layout_default(&frame->ch_layout, channels);
    CHECK(av_frame_get_buffer(frame, 0) == 0);
    for (int sample = 0; sample < samples; ++sample) {
        for (int channel = 0; channel < channels; ++channel) {
            int sign = 1;
            if (channel == 1)
                sign = -1;
            if (format == AV_SAMPLE_FMT_FLTP)
                reinterpret_cast<float *>(frame->extended_data[channel])[sample] = sign * 0.25f;
            else if (format == AV_SAMPLE_FMT_S16)
                reinterpret_cast<short *>(frame->data[0])[sample * channels + channel] = sign * 8192;
            else
                CHECK(false);
        }
    }
    return frame;
}

class LegacyStream : public VideoStream
{
public:
    int index = 0;
    Bool ready = false;
    Bool isFrameReady() override { return ready; }
    Int frameIndex() override { return index; }
    Int frameCount() override { return 3; }
};

static void legacyCompletionTest()
{
    LegacyStream stream;
    CHECK(!stream.isPlaybackComplete());
    stream.ready = true;
    CHECK(!stream.isPlaybackComplete());
    stream.index = 2;
    stream.ready = false;
    CHECK(!stream.isPlaybackComplete());
    stream.ready = true;
    CHECK(stream.isPlaybackComplete());
    std::puts("legacy completion requires both last index and ready frame passed");
}

static void converterTests()
{
    for (AVSampleFormat format : {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_S16}) {
        FFmpegPcmConverter converter;
        CHECK(converter.configure(48000, 2));
        std::vector<short> samples;
        converter.setPcmCallback([&](const short *pcm, int frames) {
            samples.insert(samples.end(), pcm, pcm + frames * 2);
        });
        AVFrame *frame = constantFrame(format, 44100, 2, 4410);
        CHECK(converter.convert(frame));
        CHECK(converter.drain());
        av_frame_free(&frame);
        CHECK(!converter.hasError() && converter.getDecodedSampleCount() == 4800);
        CHECK(samples.size() == 9600);
        for (size_t sample = 128; sample < samples.size() - 128; sample += 2) {
            CHECK(std::abs(samples[sample] - 8192) <= 1);
            CHECK(std::abs(samples[sample + 1] + 8192) <= 1);
        }
        const size_t drained = samples.size();
        CHECK(converter.drain() && samples.size() == drained);
        converter.reset();
        CHECK(converter.getDecodedSampleCount() == 0);
    }
    FFmpegPcmConverter malformed;
    CHECK(malformed.configure(48000, 2));
    AVFrame *frame = av_frame_alloc();
    CHECK(!malformed.convert(frame));
    av_frame_free(&frame);
    CHECK(malformed.hasError());
    std::puts("synthetic planar/packed resampling, drain/reset and malformed frame tests passed");
}

static void voiceOwnershipTests(FixtureAudio &audio)
{
    std::vector<short> pcm(32000, 4000);
    auto first = audio.createMovieVoice();
    auto second = audio.createMovieVoice();
    CHECK(first != nullptr && second != nullptr);
    CHECK(first->queue(pcm.data(), 16000) && second->queue(pcm.data(), 16000));
    CHECK(first->play() && second->play());
    CHECK(energy(audio.render(800)) > 1.0e8);
    const auto prior = second->readFrames();
    first->close();
    CHECK(energy(audio.render(800)) > 1.0e8 && second->readFrames() > prior);
    CHECK(second->end());
    audio.render(18000);
    CHECK(second->finished());
    CHECK(!second->play());
    CHECK(energy(audio.render(3200), 1024) < 10000);
    auto closing = audio.createMovieVoice();
    CHECK(closing != nullptr);
    CHECK(closing->queue(pcm.data(), 16000) && closing->play());
    audio.closeDevice();
    CHECK(!closing->queue(pcm.data(), 1));
    CHECK(!closing->play());
    std::puts("per-movie ownership, EOF no-replay and device shutdown tests passed");
}

static void checkPcm(const std::vector<short> &output, const std::vector<short> &expected)
{
    CHECK(output.size() >= expected.size());
    int maximum = 0;
    for (size_t i = 0; i < expected.size(); ++i)
        maximum = std::max(maximum, std::abs(output[i] - expected[i]));
    std::printf("movie PCM samples=%zu maximum error=%d\n", expected.size(), maximum);
    CHECK(maximum <= 2);
}

static std::vector<short> walkMovie(FixtureAudio &audio, VideoStreamInterface *stream,
    const Reference &expected, int first = 0)
{
    MemoryVideo buffer;
    CHECK(buffer.allocate(stream->width(), stream->height()));
    CHECK(stream->frameCount() == static_cast<int>(expected.video.size()));
    int displayed = first;
    size_t rendered = 0;
    std::vector<short> output;
    const auto start = std::chrono::steady_clock::now();
    bool finalImageBeforeCompletion = false;
    while (!stream->isPlaybackComplete()) {
        stream->update();
        if (stream->isFrameReady()) {
            CHECK(stream->frameIndex() == displayed);
            stream->frameDecompress();
            stream->frameRender(&buffer);
            CHECK(hash(buffer.bytes) == expected.video[displayed]);
            ++displayed;
            stream->frameNext();
            if (displayed == static_cast<int>(expected.video.size()) && !stream->isPlaybackComplete())
                finalImageBeforeCompletion = true;
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= std::chrono::seconds(20)) {
            std::fprintf(stderr, "movie watchdog displayed=%d index=%d count=%d rendered=%zu\n", displayed,
                stream->frameIndex(), stream->frameCount(), rendered);
            CHECK(false);
        }
        const size_t due = static_cast<size_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() * 48 / 1000);
        if (due > rendered) {
            const unsigned count = static_cast<unsigned>(std::min(size_t(512), due - rendered));
            auto pcm = audio.render(count);
            output.insert(output.end(), pcm.begin(), pcm.end());
            rendered += count;
        } else {
            Sleep(1);
        }
    }
    CHECK(displayed == static_cast<int>(expected.video.size()));
    if (!expected.audio.empty())
        CHECK(finalImageBeforeCompletion);
    CHECK(!stream->isFrameReady());
    auto tail = audio.render(3200);
    output.insert(output.end(), tail.begin(), tail.end());
    CHECK(energy(tail, 4096) < 10000);
    std::printf("presented %d ordered RGB frame hashes\n", displayed - first);
    return output;
}

static void controlTests(FixtureAudio &audio, FixturePlayer &player, const char *filename,
    const Reference &expected)
{
    FFmpegFile timing;
    CHECK(timing.open(TheFileSystem->openFile(filename)));
    int startFrame = 0;
    double bestMinimum = 0;
    size_t startSample = 0;
    for (int frame = 0; frame < static_cast<int>(expected.video.size()); ++frame) {
        const size_t offset = static_cast<size_t>(timing.getVideoFrameTimeUS(frame) * 48 / 1000) * 2;
        if (offset + 32000 > expected.audio.size())
            break;
        double minimum = 1.0e30;
        for (size_t block = 0; block < 16; ++block) {
            double value = 0;
            for (size_t i = 0; i < 1600; ++i) {
                const double sample = expected.audio[offset + block * 1600 + i];
                value += sample * sample;
            }
            minimum = std::min(minimum, value);
        }
        if (minimum > bestMinimum) {
            bestMinimum = minimum; startFrame = frame; startSample = offset;
        }
    }
    CHECK(bestMinimum > 100000);
    std::printf("control fixture frame=%d minimum reference window energy=%g\n", startFrame, bestMinimum);
    player.setVolume(1.25f);
    auto *baselineStream = player.openFile(filename);
    CHECK(baselineStream != nullptr);
    baselineStream->frameGoto(startFrame);
    for (int tick = 0; tick < 40; ++tick) baselineStream->update();
    const auto baseline = audio.render(4000);
    checkPcm(baseline, std::vector<short>(expected.audio.begin() + startSample,
        expected.audio.begin() + startSample + baseline.size()));
    baselineStream->close(); audio.render(1600);
    player.setVolume(0.625f);
    auto *stream = player.openFile(filename);
    CHECK(stream != nullptr);
    stream->frameGoto(startFrame);
    for (int tick = 0; tick < 40; ++tick) stream->update();
    const auto half = audio.render(800);
    for (size_t i = 256; i < half.size(); ++i)
        CHECK(std::abs(half[i] - std::lround(baseline[i] * 0.5)) <= 2);
    player.setVolume(0);
    audio.render(800);
    CHECK(energy(audio.render(800)) < 10000);
    player.setVolume(1.25f);
    stream->frameNext(); stream->update();
    CHECK(energy(audio.render(800)) > 10000);
    audio.muteAudio(AudioManager::MuteAudioReason_WindowFocus);
    audio.tick();
    audio.render(800);
    CHECK(energy(audio.render(800)) < 10000);
    audio.unmuteAudio(AudioManager::MuteAudioReason_WindowFocus);
    audio.tick(); stream->update();
    CHECK(energy(audio.render(800)) > 10000);
    player.loseFocus();
    audio.render(800);
    CHECK(energy(audio.render(800)) < 10000);
    stream->frameNext();
    CHECK(!stream->isFrameReady());
    player.regainFocus(); stream->update();
    CHECK(energy(audio.render(800)) > 10000);
    stream->close(); audio.render(1600);

    player.setVolume(1.25f);
    auto *first = player.openFile(filename);
    auto *second = player.openFile(filename);
    CHECK(first != nullptr && second != nullptr);
    first->frameGoto(startFrame); second->frameGoto(startFrame);
    for (int tick = 0; tick < 40; ++tick) { first->update(); second->update(); }
    auto remainingVoice = player.voices.back().lock();
    CHECK(remainingVoice != nullptr);
    audio.render(800);
    const auto previousRead = remainingVoice->readFrames();
    first->close();
    const auto independent = audio.render(1600);
    std::printf("multimovie read before=%llu after=%llu energy=%g failed=%d finished=%d available=%u\n", previousRead, remainingVoice->readFrames(), energy(independent), remainingVoice->failed(), remainingVoice->finished(), remainingVoice->availableFrames());
    std::fflush(stdout);
    CHECK(remainingVoice->readFrames() > previousRead && energy(independent) > 10000);
    for (size_t i = 1024; i < independent.size(); ++i)
        CHECK(std::abs(independent[i] - baseline[1600 + i]) <= 2);
    remainingVoice.reset();
    second->close(); audio.render(1600);
    std::puts("movie gain/mute/focus and independent simultaneous streams passed");
}

struct FaultState { bool fail = false; unsigned failures = 0; };
class FaultFile : public Win32LocalFile
{
    MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE(FaultFile, "VideoAudioFaultFile", 1, 1)
    FaultState &m_state;
public:
    explicit FaultFile(FaultState &state) : m_state(state) {}
    static File *create(const char *filename, FaultState &state)
    {
        auto *file = newInstance(FaultFile)(state);
        CHECK(file->open(filename, File::READ | File::BINARY));
        return file;
    }
    Int read(void *data, Int bytes) override
    {
        if (m_state.fail) {
            ++m_state.failures;
            return -1;
        }
        return Win32LocalFile::read(data, bytes);
    }
};


FaultFile::~FaultFile() {}

static void sourceErrorTest(FixtureAudio &audio, FixturePlayer &player, const char *filename)
{
    FaultState state;
    auto *stream = player.openOwned(FaultFile::create(filename, state));
    CHECK(stream != nullptr);
    state.fail = true;
    for (int iteration = 0; iteration < stream->frameCount() + 32 && state.failures == 0; ++iteration) {
        stream->frameNext();
        stream->update();
        audio.render(800);
    }
    CHECK(state.failures > 0);
    CHECK(stream->isPlaybackComplete());
    stream->close();
    CHECK(energy(audio.render(6400), 2048) < 10000);
    std::puts("actual engine File read failure terminates movie without replay passed");
}

static void audioFailureClockTest(FixtureAudio &audio, FixturePlayer &player, const char *filename)
{
    auto *stream = player.openFile(filename);
    CHECK(stream != nullptr);
    stream->frameNext();
    audio.render(64);
    Sleep(80);
    CHECK(!stream->isFrameReady());
    auto voice = player.voices.back().lock();
    CHECK(voice != nullptr);
    voice->close();
    CHECK(!stream->isFrameReady());
    Sleep(40);
    CHECK(stream->isFrameReady());
    stream->close();
    audio.render(1600);
    std::puts("audio failure rebases silent clock without overdue-frame jump passed");
}

static void oneFrameBudgetTest(FixtureAudio &audio, FixturePlayer &player, const char *filename,
    const Reference &expected)
{
    CHECK(expected.largestFrameBytes > 0);
    player.queueByteLimit = expected.largestFrameBytes + expected.largestFrameBytes / 2;
    CHECK(expected.largestFrameBytes > player.queueByteLimit / 2);
    auto *stream = player.openFile(filename);
    CHECK(stream != nullptr);
    CHECK(energy(walkMovie(audio, stream, expected)) < 10000);
    CHECK(stream->isPlaybackComplete());
    stream->close();
    player.queueByteLimit = 64 * 1024 * 1024;
    std::puts("single-frame queue budget reaches EOF without exceeding the bound or deadlock passed");
}

static void movieTests(FixtureAudio &audio, const char *withAudio, const char *silent)
{
    const auto expected = reference(withAudio);
    const auto silentExpected = reference(silent);
    CHECK(!expected.audio.empty() && silentExpected.audio.empty());
    std::printf("reference audio energy=%g first nonzero sample=%zu\n", energy(expected.audio),
        static_cast<size_t>(std::find_if(expected.audio.begin(), expected.audio.end(), [](short value) { return value != 0; }) - expected.audio.begin()));
    std::fflush(stdout);
    CHECK(energy(expected.audio) > 10000);
    FixturePlayer player(audio);
    TheVideoPlayer = &player;
    player.setVolume(1.25f);
    auto *stream = player.openFile(withAudio);
    CHECK(stream != nullptr && stream->frameIndex() == 0);
    MemoryVideo initial;
    CHECK(initial.allocate(stream->width(), stream->height()));
    stream->frameRender(&initial);
    CHECK(hash(initial.bytes) == expected.video[0]);
    for (int tick = 0; tick < 40; ++tick)
        stream->update();
    stream->frameRender(&initial);
    CHECK(stream->frameIndex() == 0 && hash(initial.bytes) == expected.video[0]);
    stream->frameNext();
    audio.render(64);
    Sleep(80);
    stream->update();
    CHECK(!stream->isFrameReady());
    stream->close();
    audio.render(1600);
    stream = player.openFile(withAudio);
    CHECK(stream != nullptr);
    const auto pcm = walkMovie(audio, stream, expected);
    checkPcm(pcm, expected.audio);
    const auto ended = audio.render(6400);
    CHECK(energy(ended, 1024) < 10000);
    for (int tick = 0; tick < 10; ++tick)
        stream->update();
    CHECK(energy(audio.render(6400), 1024) < 10000);
    stream->frameGoto(0);
    CHECK(stream->frameIndex() == 0);
    stream->frameRender(&initial);
    CHECK(hash(initial.bytes) == expected.video[0]);
    stream->close();
    CHECK(player.firstStream() == nullptr);
    stream = player.openFile(withAudio);
    CHECK(stream != nullptr);
    const int middle = stream->frameCount() / 2;
    stream->frameGoto(middle);
    CHECK(stream->frameIndex() == middle);
    for (int tick = 0; tick < 40; ++tick) stream->update();
    const auto seekPcm = walkMovie(audio, stream, expected, middle);
    FFmpegFile timing;
    CHECK(timing.open(TheFileSystem->openFile(withAudio)));
    const size_t seekSample = static_cast<size_t>(timing.getVideoFrameTimeUS(middle) * 48 / 1000) * 2;
    checkPcm(seekPcm, std::vector<short>(expected.audio.begin() + seekSample, expected.audio.end()));
    stream->close();
    controlTests(audio, player, withAudio, expected);
    std::fflush(stdout);
    sourceErrorTest(audio, player, withAudio);
    audioFailureClockTest(audio, player, withAudio);
    oneFrameBudgetTest(audio, player, silent, silentExpected);
    auto *silentStream = player.openFile(silent);
    CHECK(silentStream != nullptr);
    CHECK(energy(walkMovie(audio, silentStream, silentExpected)) < 10000);
    silentStream->close();
    TheVideoPlayer = nullptr;
    std::puts("actual Bink ordered presentation/audio/backpressure/clock/restart/EOF/silent movie tests passed");
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    initMemoryManager();
    CHECK(argc >= 2);
    FileSystem filesystem;
    Win32LocalFileSystem local;
    TheFileSystem = &filesystem; TheLocalFileSystem = &local;
    local.init();
    if (std::strcmp(argv[1], "--synthetic") == 0) {
        legacyCompletionTest();
        converterTests();
        FixtureAudio audio;
        TheAudio = &audio;
        audio.setup();
        voiceOwnershipTests(audio);
        TheAudio = nullptr;
    } else {
        CHECK(argc == 4 && std::strcmp(argv[1], "--movies") == 0);
        FixtureAudio audio;
        TheAudio = &audio;
        audio.setup();
        movieTests(audio, argv[2], argv[3]);
        TheAudio = nullptr;
    }
    TheFileSystem = nullptr; TheLocalFileSystem = nullptr;
    std::puts("isolated FFmpeg/OpenAL video audio tests passed; no game or network startup");
    return 0;
}
