#include "Utility/CppMacros.h"
#define NOMINMAX
#include <windows.h>
#include "Common/AudioAffect.h"
#include "Common/AudioEventInfo.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioSettings.h"
#include "Common/AudioRequest.h"
#include "Common/FileSystem.h"
#include "Common/GameCommon.h"
#include "Common/GameMemory.h"
#include "OpenALAudioDevice/OpenALAudioManager.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include <direct.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

class FixtureManager : public OpenALAudioManager
{
public:
    explicit FixtureManager(Bool dummy = false) : OpenALAudioManager(dummy) {}
    unsigned deviceOpens = 0;
    void openDevice() override
    {
        ++deviceOpens;
        CHECK(m_device.openLoopback(48000));
    }
    void configure()
    {
        AudioSettings &s = *friend_getAudioSettings();
        s.m_audioRoot = "Data\\Audio";
        s.m_soundsFolder = "Sounds";
        s.m_musicFolder = "Music";
        s.m_streamingFolder = "Speech";
        s.m_soundsExtension = "wav";
        s.m_useDigital = true;
        s.m_useMidi = false;
        s.m_outputRate = 48000;
        s.m_outputBits = 16;
        s.m_outputChannels = 2;
        s.m_sampleCount2D = 2;
        s.m_sampleCount3D = 2;
        s.m_streamCount = 2;
        s.m_globalMinRange = 10;
        s.m_globalMaxRange = 100;
        s.m_drawableAmbientFrames = 0;
        s.m_fadeAudioFrames = 3;
        s.m_maxCacheSize = 1048576;
        s.m_minVolume = 0;
        s.m_relative2DVolume = 0;
        s.m_defaultSoundVolume = 1;
        s.m_default3DSoundVolume = 1;
        s.m_defaultSpeechVolume = 1;
        s.m_defaultMusicVolume = 1;
        s.m_defaultMoneyTransactionVolume = 1;
        s.m_preferredSoundVolume = 1;
        s.m_preferred3DSoundVolume = 1;
        s.m_preferredSpeechVolume = 1;
        s.m_preferredMusicVolume = 1;
        s.m_preferredMoneyTransactionVolume = 1;
        s.m_defaultSpeakerType2D = 0;
        s.m_defaultSpeakerType3D = 0;
        s.m_microphoneDesiredHeightAboveTerrain = 0;
        s.m_microphoneMaxPercentageBetweenGroundAndCamera = 0;
        s.m_zoomMinDistance = 0;
        s.m_zoomMaxDistance = 100;
        s.m_zoomSoundVolumePercentageAmount = 0;
        s.m_use3DSoundRangeVolumeFade = true;
        s.m_3DSoundRangeVolumeFadeExponent = 1;
        m_zoomVolume = 1;
    }
    void tick() { updatePlayback(); }
    std::vector<short> render(int frames)
    {
        std::vector<short> output(static_cast<size_t>(frames) * 2);
        CHECK(m_device.render(output.data(), frames));
        return output;
    }
    std::vector<short> run(int frames)
    {
        static_assert(48000 % LOGICFRAMES_PER_SECOND == 0, "Fixture playback must match logic ticks");
        std::vector<short> output;
        while (frames > 0) {
            const int block = std::min(frames, 48000 / LOGICFRAMES_PER_SECOND);
            tick();
            auto pcm = render(block);
            output.insert(output.end(), pcm.begin(), pcm.end());
            frames -= block;
        }
        return output;
    }
    unsigned pendingRequests() const { return static_cast<unsigned>(m_audioRequests.size()); }
    void deferPending(AudioHandle handle, Real milliseconds)
    {
        for (AudioRequest *request : m_audioRequests) {
            if (request->m_pendingEvent && request->m_pendingEvent->getPlayingHandle() == handle) {
                request->m_pendingEvent->decrementDelay(-milliseconds);
                return;
            }
        }
        CHECK(false);
    }
protected:
    Bool shouldPlayLocally(const AudioEventRTS *) override { return true; }
};

static double energy(const std::vector<short> &pcm, int channel = -1, size_t start = 0)
{
    double sum = 0;
    for (size_t i = start * 2; i < pcm.size(); ++i) {
        if (channel >= 0 && static_cast<int>(i % 2) != channel)
            continue;
        sum += static_cast<double>(pcm[i]) * pcm[i];
    }
    return sum;
}

static double toneScore(const std::vector<short> &pcm, int start, int frames, double frequency)
{
    CHECK(start >= 0 && frames > 0 && static_cast<size_t>((start + frames) * 2) <= pcm.size());
    double sine = 0, cosine = 0, sum = 0;
    for (int i = 0; i < frames; ++i) {
        const double value = pcm[(start + i) * 2];
        const double angle = 2.0 * 3.141592653589793 * frequency * i / 48000.0;
        sine += value * std::sin(angle);
        cosine += value * std::cos(angle);
        sum += value * value;
    }
    if (sum <= 1) std::fprintf(stderr, "zero tone window start=%d frames=%d frequency=%.0f\n", start, frames, frequency);
    CHECK(sum > 1);
    return 2.0 * (sine * sine + cosine * cosine) / (frames * sum);
}

static AudioHandle add(FixtureManager &manager, const char *name)
{
    AudioEventRTS event{AsciiString(name)};
    return manager.addAudioEvent(&event);
}

static void requestAndSequenceTests(FixtureManager &manager)
{
    CHECK(manager.findAudioEventInfo("Sequence") != nullptr);
    const AudioHandle handle = add(manager, "Sequence");
    CHECK(handle >= AHSV_FirstHandle && manager.pendingRequests() == 1);
    const auto pcm = manager.run(8000);
    CHECK(toneScore(pcm, 128, 1280, 200) > 0.95);
    CHECK(toneScore(pcm, 1792, 2560, 400) > 0.95);
    CHECK(toneScore(pcm, 4992, 1280, 800) > 0.95);
    CHECK(!manager.isCurrentlyPlaying(handle));
    CHECK(energy(pcm) > 1.0e10);

    std::puts("initial sequence passed"); std::fflush(stdout);
    const AudioHandle finite = add(manager, "FiniteLoop");
    const auto loop = manager.run(11200);
    CHECK(toneScore(loop, 6656, 2048, 400) > 0.95);
    CHECK(!manager.isCurrentlyPlaying(finite));

    std::puts("finite loop passed"); std::fflush(stdout);
    const AudioHandle all = add(manager, "AllParts");
    const auto parts = manager.run(8000);
    CHECK(toneScore(parts, 128, 1280, 200) > 0.95);
    // The shared AudioEventRTS helper currently ends AC_ALL after its first sound, as in Miles.
    CHECK(energy(parts, -1, 2048) < 10000);
    CHECK(!manager.isCurrentlyPlaying(all));
    std::puts("real INI/request/attack-main-decay/loop/all-part tests passed");
}

static void limitAndPriorityTests(FixtureManager &manager)
{
    manager.reset();
    const AudioHandle limited = add(manager, "Limited");
    manager.tick();
    CHECK(manager.isCurrentlyPlaying(limited));
    CHECK(manager.getNumAvailable2DSamples() == 1);
    const AudioHandle rejected = add(manager, "Limited");
    manager.tick();
    CHECK(!manager.isCurrentlyPlaying(rejected));
    manager.killAudioEventImmediately(limited);
    CHECK(!manager.isCurrentlyPlaying(limited));
    CHECK(manager.getNumAvailable2DSamples() == 2);
    const AudioHandle afterStop = add(manager, "Limited");
    manager.tick();
    CHECK(manager.isCurrentlyPlaying(afterStop));
    manager.stopAudio(AudioAffect_All);
    manager.tick();

    const AudioHandle low1 = add(manager, "Low");
    const AudioHandle low2 = add(manager, "Low");
    manager.tick();
    CHECK(manager.getNumAvailable2DSamples() == 0);
    CHECK(manager.isCurrentlyPlaying(low1) && manager.isCurrentlyPlaying(low2));
    const AudioHandle high = add(manager, "High");
    manager.tick();
    CHECK(manager.isCurrentlyPlaying(high));
    const unsigned lowPlaying = static_cast<unsigned>(manager.isCurrentlyPlaying(low1)) +
        static_cast<unsigned>(manager.isCurrentlyPlaying(low2));
    CHECK(lowPlaying == 1 && manager.getNumAvailable2DSamples() == 0);
    manager.stopAudio(AudioAffect_All);
    manager.tick();
    std::puts("limits/preemption/immediate-stopped-visibility tests passed");
}


static void streamedPayloadTest(FixtureManager &manager)
{
    manager.reset();
    manager.render(1600);
    const AudioHandle speech = add(manager, "SpeechOne");
    const auto output = manager.run(51200);
    int maximumError = 0;
    for (int i = 0; i < 48000; ++i) {
        const short source = static_cast<short>(std::lround(8000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * i / 48000.0)));
        const short expected = static_cast<short>(std::lround(source / std::sqrt(2.0)));
        maximumError = std::max(maximumError, std::abs(static_cast<int>(output[2 * i]) - expected));
        maximumError = std::max(maximumError, std::abs(static_cast<int>(output[2 * i + 1]) - expected));
    }
    std::printf("streamed one-second payload maximum sample error: %d\n", maximumError);
    CHECK(maximumError <= 2);
    CHECK(!manager.isCurrentlyPlaying(speech));
    CHECK(energy(output, -1, 49152) < 10000);
}


static void retireFinishedTests(FixtureManager &manager)
{
    manager.reset();
    manager.render(1600);
    for (unsigned round = 0; round < 300; ++round) {
        const AudioHandle handle = add(manager, "Ui");
        CHECK(handle >= AHSV_FirstHandle);
        manager.tick();
        CHECK(manager.isCurrentlyPlaying(handle));
        manager.render(6400);
        CHECK(!manager.isCurrentlyPlaying(handle));
        CHECK(manager.getNumAvailable2DSamples() == manager.getNum2DSamples());
    }
    for (unsigned round = 0; round < 3; ++round) {
        AudioEventRTS speech{AsciiString("SpeechOne")};
        speech.setUninterruptible(true);
        const AudioHandle handle = manager.addAudioEvent(&speech);
        CHECK(handle >= AHSV_FirstHandle);
        manager.tick();
        CHECK(manager.getDisallowSpeech());
        manager.run(48000);
        manager.render(3200);
        CHECK(!manager.isCurrentlyPlaying(handle));
    }
    manager.tick();
    CHECK(!manager.getDisallowSpeech());
    std::puts("300 no-reset finished-source reuses and pre-add speech-gate retirement passed");
}

static void controlsAndStreamingTests(FixtureManager &manager)
{
    manager.reset();
    const AudioHandle permanent = add(manager, "Permanent");
    manager.run(11200);
    CHECK(manager.isCurrentlyPlaying(permanent));
    manager.pauseAudio(AudioAffect_Sound);
    manager.run(1600);
    const auto paused = manager.run(3200);
    CHECK(energy(paused) < 10000);
    manager.resumeAudio(AudioAffect_Sound);
    CHECK(energy(manager.run(3200)) > 1.0e9);
    manager.removeAudioEvent(permanent);
    manager.tick();
    manager.run(6400);
    CHECK(!manager.isCurrentlyPlaying(permanent));
    manager.run(1600);

    AudioEventRTS speech{AsciiString("SpeechOne")};
    speech.setUninterruptible(true);
    const AudioHandle speaking = manager.addAudioEvent(&speech);
    manager.tick();
    CHECK(manager.isCurrentlyPlaying(speaking) && manager.getDisallowSpeech());
    CHECK(add(manager, "SpeechOne") == AHSV_NoSound);
    const AudioHandle music = add(manager, "MusicOne");
    manager.tick();
    CHECK(manager.isCurrentlyPlaying(music) && manager.isMusicPlaying());
    CHECK(!manager.hasMusicTrackCompleted("MusicOne", 1));
    CHECK(energy(manager.run(3200)) > 1.0e9);
    manager.pauseAudio(AudioAffect_Music);
    CHECK(energy(manager.run(3200)) > 1.0e8);
    manager.pauseAudio(AudioAffect_Speech);
    manager.run(1600);
    CHECK(energy(manager.run(3200)) < 10000);
    manager.resumeAudio(AudioAffect_Music);
    manager.resumeAudio(AudioAffect_Speech);
    manager.run(51200);
    CHECK(!manager.isCurrentlyPlaying(speaking) && !manager.getDisallowSpeech());
    CHECK(manager.isMusicPlaying() && manager.hasMusicTrackCompleted("MusicOne", 1));
    manager.stopAudio(AudioAffect_Music);
    CHECK(!manager.isMusicPlaying());
    std::puts("category pause/stream backpressure/speech gate/music completion tests passed");
}


static void loopStopAndForcedTests(FixtureManager &manager)
{
    manager.reset();
    const AudioHandle oneShot = add(manager, "Ui");
    manager.tick();
    CHECK(manager.isCurrentlyPlaying(oneShot));
    manager.render(6400);
    CHECK(!manager.isCurrentlyPlaying(oneShot));
    CHECK(manager.getNumAvailable2DSamples() == manager.getNum2DSamples());
    manager.tick();

    const AudioHandle loop = add(manager, "StopLoop");
    manager.tick();
    auto prefix = manager.render(1200);
    manager.removeAudioEvent(loop);
    manager.tick();
    auto suffix = manager.run(5200);
    prefix.insert(prefix.end(), suffix.begin(), suffix.end());
    CHECK(toneScore(prefix, 128, 800, 400) > 0.95);
    CHECK(toneScore(prefix, 1728, 1280, 400) > 0.95);
    CHECK(toneScore(prefix, 3328, 1280, 800) > 0.95);
    int previousLeft = std::abs(static_cast<int>(prefix[4799 * 2]));
    int previousRight = std::abs(static_cast<int>(prefix[4799 * 2 + 1]));
    for (size_t i = 4800 * 2; i < prefix.size(); i += 2) {
        const int left = std::abs(static_cast<int>(prefix[i]));
        const int right = std::abs(static_cast<int>(prefix[i + 1]));
        CHECK(left <= previousLeft + 2 && right <= previousRight + 2);
        previousLeft = left;
        previousRight = right;
    }
    CHECK(energy(prefix, -1, 6000) < 10000);
    CHECK(!manager.isCurrentlyPlaying(loop));

    manager.reset();
    AudioEventRTS briefing{AsciiString("SpeechOne")};
    manager.friend_forcePlayAudioEventRTS(&briefing);
    const auto output = manager.render(51200);
    CHECK(toneScore(output, 256, 8000, 440) > 0.95);
    CHECK(toneScore(output, 38400, 8000, 440) > 0.95);
    CHECK(energy(output, -1, 49024) < 10000);
    manager.tick();
    std::puts("pre-pump stopped visibility/current-loop-then-decay/no-pump loading speech tests passed");
}


static void pendingCancellationAndMusicTests(FixtureManager &manager)
{
    for (const char *name : {"SpeechOne", "MusicOne"}) {
        manager.reset();
        manager.render(1600);
        const AudioHandle pending = add(manager, name);
        manager.deferPending(pending, 200);
        manager.removeAudioEvent(pending);
        manager.tick();
        CHECK(!manager.isCurrentlyPlaying(pending));
        CHECK(energy(manager.run(12800)) < 10000);
        CHECK(!manager.getDisallowSpeech());
    }
    manager.reset();
    manager.render(1600);
    const AudioHandle firstMusic = add(manager, "MusicOne");
    manager.tick();
    CHECK(manager.isCurrentlyPlaying(firstMusic));
    CHECK(manager.nextMusicTrack() == "MusicTwo");
    const auto compressed = manager.run(3200);
    CHECK(toneScore(compressed, 256, 2560, 440) > 0.95);
    CHECK(manager.prevMusicTrack() == "MusicOne");
    manager.run(3200);
    CHECK(manager.isMusicPlaying());
    manager.removeAudioEvent(AHSV_StopTheMusicFade);
    manager.tick();
    manager.run(6400);
    CHECK(!manager.isMusicPlaying());
    std::puts("pending speech/music cancellation, compressed music navigation and fade tests passed");
}

static void positionAndVolumeTests(FixtureManager &manager)
{
    manager.reset();
    Coord3D listener, forward;
    listener.set(0, 0, 0);
    forward.set(0, 1, 0);
    manager.setListenerPosition(&listener, &forward);
    Coord3D right;
    right.set(5, 0, 0);
    AudioEventRTS event(AsciiString("World"), &right);
    const AudioHandle handle = manager.addAudioEvent(&event);
    CHECK(handle >= AHSV_FirstHandle);
    const auto positioned = manager.run(3200);
    CHECK(energy(positioned, 1, 256) > 4 * energy(positioned, 0, 256));
    manager.stopAudio(AudioAffect_All);
    manager.run(1600);
    forward.set(1, 0, 0);
    manager.setListenerPosition(&listener, &forward);
    Coord3D rotatedLeft;
    rotatedLeft.set(0, 5, 0);
    AudioEventRTS rotatedEvent(AsciiString("World"), &rotatedLeft);
    manager.addAudioEvent(&rotatedEvent);
    const auto rotated = manager.run(3200);
    CHECK(energy(rotated, 0, 256) > 4 * energy(rotated, 1, 256));
    manager.stopAudio(AudioAffect_All);
    manager.run(1600);
    forward.set(0, 1, 0);
    manager.setListenerPosition(&listener, &forward);

    Coord3D nearPosition, farPosition;
    nearPosition.set(0, 5, 0);
    farPosition.set(0, 55, 0);
    AudioEventRTS nearEvent(AsciiString("World"), &nearPosition);
    manager.addAudioEvent(&nearEvent);
    const auto nearPcm = manager.run(3200);
    manager.stopAudio(AudioAffect_All);
    manager.run(1600);
    AudioEventRTS farEvent(AsciiString("World"), &farPosition);
    manager.addAudioEvent(&farEvent);
    const auto farPcm = manager.run(3200);
    const double ratio = energy(farPcm, -1, 256) / energy(nearPcm, -1, 256);
    CHECK(ratio > 0.20 && ratio < 0.30);
    manager.stopAudio(AudioAffect_All);
    manager.run(1600);
    const AudioHandle looping = add(manager, "Permanent");
    const double loud = energy(manager.run(3200), -1, 256);
    manager.setVolume(0.5f, AudioAffect_Sound);
    manager.run(1600);
    const double quiet = energy(manager.run(3200), -1, 256);
    CHECK(quiet / loud > 0.20 && quiet / loud < 0.30);
    manager.killAudioEventImmediately(looping);
    manager.setVolume(1, AudioAffect_Sound);
    manager.tick();
    std::puts("Z-up 3D pan/range/category gain tests passed");
}


static std::vector<unsigned char> readFixture(const char *filename)
{
    FILE *file = std::fopen(filename, "rb");
    CHECK(file != nullptr);
    CHECK(std::fseek(file, 0, SEEK_END) == 0);
    const long size = std::ftell(file);
    CHECK(size > 0 && size < 1048576);
    std::rewind(file);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    CHECK(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    CHECK(std::fclose(file) == 0);
    return bytes;
}

static void writeFixture(const char *filename, const std::vector<unsigned char> &bytes)
{
    FILE *file = std::fopen(filename, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    CHECK(std::fclose(file) == 0);
}

static void delayAndReloadTests(FixtureManager &manager)
{
    manager.reset();
    manager.render(1600);
    const AudioHandle delayed = add(manager, "Delayed");
    CHECK(manager.pendingRequests() == 1);
    CHECK(energy(manager.run(4800)) < 10000);
    CHECK(manager.pendingRequests() == 1);
    CHECK(energy(manager.run(4000)) > 1.0e9);
    CHECK(!manager.isCurrentlyPlaying(delayed));

    const auto main = readFixture("Data/Audio/Sounds/main.wav");
    const auto attack = readFixture("Data/Audio/Sounds/attack.wav");
    CHECK(manager.getFileLengthMS("Data\\Audio\\Sounds\\main.wav") == 66);
    manager.reset();
    writeFixture("Data/Audio/Sounds/main.wav", attack);
    CHECK(manager.getFileLengthMS("Data\\Audio\\Sounds\\main.wav") == 33);
    add(manager, "Ui");
    const auto reloaded = manager.run(3200);
    CHECK(toneScore(reloaded, 128, 1280, 200) > 0.95);
    manager.reset();
    writeFixture("Data/Audio/Sounds/main.wav", main);
    CHECK(manager.getFileLengthMS("Data\\Audio\\Sounds\\main.wav") == 66);
    std::puts("logic-frame delay and real-file PCM/metadata reset-reload tests passed");
}

static void checkLongPcm(const std::vector<short> &pcm, unsigned offset, int voices = 1)
{
    for (size_t frame = 128; frame < pcm.size() / 2; ++frame) {
        const double phase = 6.283185307179586 * 440 * (offset + frame) / 48000;
        const int source = static_cast<int>(std::lround(8000 * std::sin(phase)));
        const int expected = voices * static_cast<int>(std::lround(source / std::sqrt(2.0)));
        CHECK(std::abs(pcm[frame * 2] - expected) <= voices * 2);
        CHECK(std::abs(pcm[frame * 2 + 1] - expected) <= voices * 2);
    }
}

static void replacementAndPoolTests(FixtureManager &manager)
{
    for (const char *suffix : {"Missing", "Corrupt"}) {
        manager.reset(); manager.render(1600);
        const AudioHandle first = add(manager, "Low");
        const AudioHandle second = add(manager, "Low");
        manager.run(800);
        const std::string bad = std::string("Bad") + suffix;
        const AudioHandle rejected = add(manager, bad.c_str());
        manager.tick();
        CHECK(manager.isCurrentlyPlaying(first) && manager.isCurrentlyPlaying(second));
        CHECK(!manager.isCurrentlyPlaying(rejected));
        checkLongPcm(manager.render(1600), 800, 2);

        manager.reset(); manager.render(1600);
        const AudioHandle original = add(manager, "Interrupt");
        manager.run(800);
        AudioEventInfo *info = manager.findAudioEventInfo("Interrupt");
        const auto savedSounds = info->m_sounds;
        info->m_sounds[0] = "missing";
        if (std::string(suffix) == "Corrupt")
            info->m_sounds[0] = "corrupt";
        add(manager, "Interrupt"); manager.tick();
        info->m_sounds = savedSounds;
        CHECK(manager.isCurrentlyPlaying(original));
        checkLongPcm(manager.render(1600), 800);

        manager.reset(); manager.render(1600);
        const AudioHandle music = add(manager, "MusicOne");
        manager.run(57600);
        CHECK(manager.hasMusicTrackCompleted("MusicOne", 1));
        const std::string badMusic = std::string("BadMusic") + suffix;
        add(manager, badMusic.c_str()); manager.tick();
        CHECK(manager.isCurrentlyPlaying(music));
        CHECK(manager.hasMusicTrackCompleted("MusicOne", 1));
        checkLongPcm(manager.run(3200), 57600);

        manager.reset(); manager.render(1600);
        const AudioHandle speech = add(manager, "SpeechOne");
        manager.run(800);
        const std::string badSpeech = std::string("BadSpeech") + suffix;
        AudioEventRTS candidate{AsciiString(badSpeech.c_str())};
        candidate.setUninterruptible(true);
        manager.addAudioEvent(&candidate); manager.tick();
        CHECK(manager.isCurrentlyPlaying(speech) && !manager.getDisallowSpeech());
        checkLongPcm(manager.render(1600), 800);

        manager.reset(); manager.render(1600);
        AudioEventRTS forced{AsciiString("SpeechOne")};
        forced.setUninterruptible(true);
        manager.friend_forcePlayAudioEventRTS(&forced);
        manager.render(800);
        manager.friend_forcePlayAudioEventRTS(&candidate);
        CHECK(manager.getDisallowSpeech());
        checkLongPcm(manager.render(46400), 800);
        manager.render(3200); manager.tick();
        CHECK(!manager.getDisallowSpeech());
    }
    manager.friend_getAudioSettings()->m_streamCount = 1;
    struct Case { const char *oldName; const char *newName; bool preempts; bool protectedSpeech; };
    for (const Case &test : {Case{"MusicLOW", "SpeechHIGH", true, false},
         Case{"SpeechLOW", "MusicHIGH", true, false},
         Case{"MusicHIGH", "SpeechLOW", false, false},
         Case{"SpeechHIGH", "MusicLOW", false, false},
         Case{"SpeechLOW", "MusicHIGH", false, true}}) {
        manager.reset(); manager.render(1600);
        AudioEventRTS previous{AsciiString(test.oldName)};
        previous.setUninterruptible(test.protectedSpeech);
        const AudioHandle old = manager.addAudioEvent(&previous);
        manager.run(800);
        const AudioHandle next = add(manager, test.newName);
        manager.tick();
        CHECK(manager.isCurrentlyPlaying(old) == !test.preempts);
        CHECK(manager.isCurrentlyPlaying(next) == test.preempts);
        if (!test.preempts)
            checkLongPcm(manager.render(1600), 800);
    }
    manager.reset(); manager.render(1600);
    add(manager, "MusicOne"); manager.run(800);
    CHECK(manager.nextMusicTrack() == "MusicTwo"); manager.run(1600);
    CHECK(manager.isMusicPlaying());
    CHECK(manager.prevMusicTrack() == "MusicOne"); manager.run(1600);
    CHECK(manager.isMusicPlaying());
    for (const char *filename : {"missing.wav", "corrupt.wav"}) {
        manager.reset(); manager.render(1600);
        const AudioHandle oldMusic = add(manager, "MusicOne");
        manager.run(57600);
        AudioEventInfo *info = manager.findAudioEventInfo("MusicTwo");
        const AsciiString saved = info->m_filename;
        info->m_filename = filename;
        CHECK(manager.nextMusicTrack() == "MusicTwo");
        manager.tick();
        info->m_filename = saved;
        CHECK(manager.isCurrentlyPlaying(oldMusic));
        CHECK(manager.hasMusicTrackCompleted("MusicOne", 1));
        checkLongPcm(manager.run(3200), 57600);
    }
    for (const char *filename : {"missing.wav", "corrupt.wav"}) {
        manager.reset(); manager.render(1600);
        AudioEventInfo *secondInfo = manager.findAudioEventInfo("MusicTwo");
        const AsciiString secondFilename = secondInfo->m_filename;
        secondInfo->m_filename = "long.wav";
        const AudioHandle oldMusic = add(manager, "MusicTwo");
        manager.run(57600);
        AudioEventInfo *firstInfo = manager.findAudioEventInfo("MusicOne");
        const AsciiString firstFilename = firstInfo->m_filename;
        firstInfo->m_filename = filename;
        CHECK(manager.prevMusicTrack() == "MusicOne");
        manager.tick();
        firstInfo->m_filename = firstFilename;
        secondInfo->m_filename = secondFilename;
        CHECK(manager.isCurrentlyPlaying(oldMusic));
        CHECK(manager.hasMusicTrackCompleted("MusicTwo", 1));
        checkLongPcm(manager.run(3200), 57600);
    }
    manager.friend_getAudioSettings()->m_streamCount = 2;
    manager.reset(); manager.render(1600);
    for (int cycle = 0; cycle < 10; ++cycle) {
        AudioEventRTS forced{AsciiString("SpeechOne")};
        manager.friend_forcePlayAudioEventRTS(&forced);
        const auto pcm = manager.render(51200);
        CHECK(toneScore(pcm, 256, 8000, 440) > 0.95);
        CHECK(toneScore(pcm, 38400, 8000, 440) > 0.95);
    }
    manager.tick();
    std::puts("transactional missing/corrupt replacement, shared stream priorities and forced reuse passed");
}

static void cacheGrowthTests()
{
    const char *longFile = "Data\\Audio\\Sounds\\long.wav";
    for (size_t budget : {size_t(1048576), size_t(192000), size_t(191999)}) {
        OpenALAudioCache cache(budget);
        auto clip = cache.load(longFile, 48000, 2);
        if (budget < 192000) {
            CHECK(clip == nullptr && cache.usedBytes() == 0);
            continue;
        }
        CHECK(clip != nullptr && clip->samples.size() == 96000);
        CHECK(cache.usedBytes() == clip->samples.capacity() * sizeof(short));
        CHECK(cache.usedBytes() <= budget);
        if (budget > 192000)
            CHECK(clip->samples.capacity() > clip->samples.size());
        else
            CHECK(clip->samples.capacity() == clip->samples.size());
        checkLongPcm(clip->samples, 0);
        std::printf("cache budget=%zu logical=%zu capacity=%zu bytes=%zu\n", budget,
            clip->samples.size(), clip->samples.capacity(), cache.usedBytes());
    }
    OpenALAudioCache cache(200000);
    auto pinned = cache.load("Data\\Audio\\Sounds\\attack.wav", 48000, 2);
    CHECK(pinned != nullptr);
    auto large = cache.load(longFile, 48000, 2);
    CHECK(large != nullptr && cache.usedBytes() <= 240000);
    CHECK(cache.load("Data\\Audio\\Sounds\\attack.wav", 48000, 2) == pinned);
    checkLongPcm(large->samples, 0);
    std::printf("pinned cache capacity bytes=%zu\n", cache.usedBytes());
}

static void durationAndCacheTests(FixtureManager &manager)
{
    manager.reset();
    const Real normal = manager.getFileLengthMS("Data\\Audio\\Sounds\\odd.wav");
    CHECK(normal == 296);
    AudioEventRTS normalSequence{AsciiString("Sequence")};
    const Real normalEventLength = manager.getAudioLengthMS(&normalSequence);
    CHECK(normalEventLength == 132);
    {
        FixtureManager dummy(true);
        dummy.configure();
        TheAudio = &dummy;
        dummy.init();
        CHECK(dummy.deviceOpens == 0 && dummy.getDevice() == nullptr);
        dummy.update();
        CHECK(dummy.deviceOpens == 0 && dummy.getDevice() == nullptr);
        const Real headless = dummy.getFileLengthMS("Data\\Audio\\Sounds\\odd.wav");
        CHECK(headless == normal);
        AudioEventRTS headlessSequence{AsciiString("Sequence")};
        CHECK(dummy.getAudioLengthMS(&headlessSequence) == normalEventLength);
        CHECK(static_cast<unsigned>(headless / MSEC_PER_LOGICFRAME_REAL) ==
              static_cast<unsigned>(normal / MSEC_PER_LOGICFRAME_REAL));
        dummy.reset();
    }
    TheAudio = &manager;
    CHECK(TheFileSystem->doesFileExist("Data\\Audio\\Sounds\\archive.wav"));
    CHECK(manager.getFileLengthMS("Data\\Audio\\Sounds\\archive.wav") == 66);
    OpenALAudioCache cache(24000);
    auto main = cache.load("Data\\Audio\\Sounds\\main.wav", 48000, 2);
    auto attack = cache.load("Data\\Audio\\Sounds\\attack.wav", 48000, 2);
    CHECK(main != nullptr && attack != nullptr && cache.usedBytes() == (main->samples.capacity() + attack->samples.capacity()) * sizeof(short));
    CHECK(cache.load("Data\\Audio\\Sounds\\decay.wav", 48000, 2) == nullptr);
    attack.reset();
    auto decay = cache.load("Data\\Audio\\Sounds\\decay.wav", 48000, 2);
    CHECK(decay != nullptr && cache.usedBytes() <= 24000);
    CHECK(cache.load("Data\\Audio\\Sounds\\main.wav", 48000, 2) == main);
    main.reset(); decay.reset(); cache.clear();
    CHECK(cache.usedBytes() == 0);
    std::puts("normal/headless/source-duration/script-frame/cache-pin/actual-BIG tests passed");
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    initMemoryManager();
    CHECK(_chdir(argv[1]) == 0);
    const auto marker = readFixture("audio-manager-fixture.marker");
    const char expectedMarker[] = "generals-openal-audio-manager-test-fixture-v1\n";
    CHECK(marker.size() == sizeof(expectedMarker) - 1 &&
        std::equal(marker.begin(), marker.end(), expectedMarker));
    FileSystem fileSystem;
    Win32LocalFileSystem local;
    Win32BIGFileSystem archive;
    TheFileSystem = &fileSystem;
    TheLocalFileSystem = &local;
    TheArchiveFileSystem = &archive;
    local.init();
    archive.loadBigFilesFromDirectory("", "fixture.big");
    {
        FixtureManager manager;
        manager.configure();
        TheAudio = &manager;
        manager.init();
        CHECK(manager.deviceOpens == 1 && manager.getDevice() != nullptr);
        requestAndSequenceTests(manager);
        limitAndPriorityTests(manager);
        streamedPayloadTest(manager);
        retireFinishedTests(manager);
        controlsAndStreamingTests(manager);
        loopStopAndForcedTests(manager);
        pendingCancellationAndMusicTests(manager);
        positionAndVolumeTests(manager);
        delayAndReloadTests(manager);
        durationAndCacheTests(manager);
        replacementAndPoolTests(manager);
        cacheGrowthTests();
        manager.reset();
        TheAudio = nullptr;
    }
    TheArchiveFileSystem = nullptr;
    TheLocalFileSystem = nullptr;
    TheFileSystem = nullptr;
    std::puts("engine-linked audio manager tests passed; no GameEngine or network startup invoked");
    return 0;
}
