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
    bool onScreen = true;
    mutable unsigned screenQueries = 0;
    AudioHandle nextPlaybackHandle() { return allocateNewHandle(); }
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
    Bool isOnScreen(const Coord3D *) const override { ++screenQueries; return onScreen; }
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
        const short expected = static_cast<short>(std::lround(source * std::pow(0.5, 0.3000000119)));
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


static void pauseSnapshotTests(FixtureManager &manager)
{
    manager.reset();
    manager.render(1600);
    const AudioHandle existing = add(manager, "Permanent");
    CHECK(energy(manager.run(1600)) > 1.0e9);
    const AudioHandle pending = add(manager, "Delayed");
    CHECK(manager.pendingRequests() == 1);
    manager.pauseAudio(static_cast<AudioAffect>(AudioAffect_All & ~AudioAffect_Music));
    CHECK(manager.pendingRequests() == 0 && !manager.isCurrentlyPlaying(pending));
    manager.run(1600);
    CHECK(energy(manager.run(1600)) < 10000);
    const AudioHandle ui = add(manager, "Ui");
    const auto menu = manager.run(6400);
    CHECK(toneScore(menu, 256, 2048, 400) > 0.95);
    CHECK(energy(menu, -1, 4800) < 10000);
    CHECK(manager.isCurrentlyPlaying(existing) && !manager.isCurrentlyPlaying(ui));
    manager.resumeAudio(AudioAffect_Sound);
    CHECK(energy(manager.run(3200)) > 1.0e9);
    CHECK(!manager.isCurrentlyPlaying(ui));
    manager.stopAudio(AudioAffect_All);
    manager.run(1600);
    CHECK(energy(manager.run(3200)) < 10000);

    std::vector<short> resumedWithoutUi;
    for (bool playUi : {false, true}) {
        manager.reset();
        manager.render(1600);
        const AudioHandle sound = add(manager, "Low");
        manager.run(800);
        manager.pauseAudio(AudioAffect_Sound);
        manager.run(1600);
        if (playUi)
            add(manager, "Ui");
        manager.run(6400);
        manager.resumeAudio(AudioAffect_Sound);
        const auto resumed = manager.run(48000);
        CHECK(!manager.isCurrentlyPlaying(sound));
        if (!playUi) {
            resumedWithoutUi = resumed;
        } else {
            CHECK(resumed.size() == resumedWithoutUi.size());
            int maximum = 0;
            for (size_t i = 0; i < resumed.size(); ++i)
                maximum = std::max(maximum, std::abs(resumed[i] - resumedWithoutUi[i]));
            std::printf("pause/resume PCM with and without intervening UI maximum error=%d\n", maximum);
            CHECK(maximum <= 2);
        }
    }

    manager.reset();
    manager.render(1600);
    add(manager, "Permanent");
    add(manager, "MusicOne");
    manager.run(1600);
    manager.pauseAudio(AudioAffect_Sound);
    manager.run(1600);
    manager.resumeAudio(AudioAffect_Speech);
    CHECK(toneScore(manager.run(3200), 128, 2560, 440) > 0.95);
    manager.stopAudio(AudioAffect_Music);
    manager.run(1600);
    CHECK(energy(manager.run(3200)) < 10000);
    manager.resumeAudio(AudioAffect_Sound);
    CHECK(energy(manager.run(3200)) > 1.0e9);

    for (bool categoryFirst : {false, true}) {
        manager.reset();
        manager.render(1600);
        add(manager, "Permanent");
        manager.run(1600);
        if (categoryFirst) {
            manager.pauseAudio(AudioAffect_Sound);
            manager.pauseAmbient(true);
            manager.resumeAudio(AudioAffect_Sound);
        } else {
            manager.pauseAmbient(true);
            manager.pauseAudio(AudioAffect_Sound);
            manager.pauseAmbient(false);
        }
        manager.run(1600);
        CHECK(energy(manager.run(3200)) < 10000);
        if (categoryFirst)
            manager.pauseAmbient(false);
        else
            manager.resumeAudio(AudioAffect_Sound);
        CHECK(energy(manager.run(3200)) > 1.0e9);
    }

    manager.reset();
    manager.render(1600);
    manager.pauseAmbient(true);
    const AudioHandle ambient = add(manager, "Permanent");
    CHECK(energy(manager.run(3200)) < 10000);
    CHECK(manager.isCurrentlyPlaying(ambient));
    const AudioHandle freshUi = add(manager, "Ui");
    CHECK(energy(manager.run(6400)) > 1.0e9);
    CHECK(!manager.isCurrentlyPlaying(freshUi));
    manager.pauseAudio(AudioAffect_Sound);
    manager.pauseAmbient(false);
    CHECK(energy(manager.run(3200)) < 10000);
    manager.resumeAudio(AudioAffect_Sound);
    CHECK(energy(manager.run(3200)) > 1.0e9);
    manager.reset();
    manager.render(1600);
    std::puts("pause snapshot, new UI playback, scoped resume and ambient overlap tests passed");
}

static void pausedGainTests(FixtureManager &manager)
{
    for (bool ambient : {false, true}) {
        manager.reset();
        manager.render(1600);
        add(manager, "Permanent");
        CHECK(energy(manager.run(3200)) > 1.0e9);
        if (ambient)
            manager.pauseAmbient(true);
        else
            manager.pauseAudio(AudioAffect_Sound);
        manager.run(1600);
        manager.setVolume(0, AudioAffect_Sound);
        if (!ambient)
            manager.tick();
        if (ambient)
            manager.pauseAmbient(false);
        else
            manager.resumeAudio(AudioAffect_Sound);
        const auto muted = manager.render(6400);
        CHECK(energy(muted, -1, 1600) < 10000);
        manager.setVolume(1, AudioAffect_Sound);
        CHECK(energy(manager.run(3200)) > 1.0e9);
    }
    manager.reset();
    manager.render(1600);
    add(manager, "Permanent");
    manager.run(3200);
    manager.pauseAudio(AudioAffect_Sound);
    manager.run(1600);
    manager.muteAudio(AudioManager::MuteAudioReason_WindowFocus);
    manager.tick();
    manager.resumeAudio(AudioAffect_Sound);
    CHECK(energy(manager.render(6400), -1, 1600) < 10000);
    manager.unmuteAudio(AudioManager::MuteAudioReason_WindowFocus);
    CHECK(energy(manager.run(3200)) > 1.0e9);
    manager.reset();
    manager.render(1600);
    manager.friend_getAudioSettings()->m_fadeAudioFrames = 20;
    const AudioHandle fading = add(manager, "MusicOne");
    manager.run(1600);
    add(manager, "MusicTwo");
    manager.tick();
    manager.pauseAudio(AudioAffect_Music);
    manager.run(1600);
    manager.setVolume(0, AudioAffect_Music);
    for (int tick = 0; tick < 25; ++tick)
        manager.tick();
    CHECK(manager.isCurrentlyPlaying(fading));
    manager.resumeAudio(AudioAffect_Music);
    CHECK(energy(manager.render(6400), -1, 1600) < 10000);
    CHECK(manager.isCurrentlyPlaying(fading));
    manager.run(17600);
    CHECK(!manager.isCurrentlyPlaying(fading));
    manager.friend_getAudioSettings()->m_fadeAudioFrames = 3;
    manager.reset();
    manager.render(1600);
    std::puts("paused category/ambient gains, focus mute and frozen fades apply before direct resume passed");
}

static void forcedOverrideTests(FixtureManager &manager)
{
    manager.reset();
    manager.render(1600);
    AudioEventRTS briefing{AsciiString("Low")};
    manager.friend_forcePlayAudioEventRTS(&briefing);
    const double baseline = energy(manager.render(48000), -1, 1600);
    CHECK(baseline > 1.0e9);
    manager.reset();
    manager.render(1600);
    manager.setAudioEventVolumeOverride("Low", 0.25f);
    manager.setVolume(0.5f, AudioAffect_Speech);
    manager.friend_forcePlayAudioEventRTS(&briefing);
    const double quiet = energy(manager.render(48000), -1, 1600);
    const double expectedRatio = std::pow(0.125, 10.0 / 3.0);
    CHECK(quiet / baseline > expectedRatio * 0.95 && quiet / baseline < expectedRatio * 1.05);
    manager.reset();
    manager.render(1600);
    manager.setAudioEventEnabled("Low", false);
    manager.friend_forcePlayAudioEventRTS(&briefing);
    CHECK(energy(manager.render(48000), -1, 1600) < 10000);
    manager.reset();
    manager.render(1600);
    manager.setVolume(0.5f, AudioAffect_Speech);
    manager.friend_forcePlayAudioEventRTS(&briefing);
    manager.render(1600);
    manager.setAudioEventVolumeOverride("Low", 0.25f);
    const auto changed = manager.render(6400);
    const double expectedGain = std::pow(0.125, 5.0 / 3.0) * std::pow(0.5, 0.3000000119);
    for (size_t frame = 1600; frame < changed.size() / 2; ++frame) {
        const double phase = 6.283185307179586 * 440 * (1600 + frame) / 48000;
        const int expected = static_cast<int>(std::lround(8000 * std::sin(phase) * expectedGain));
        CHECK(std::abs(changed[frame * 2] - expected) <= 2);
        CHECK(std::abs(changed[frame * 2 + 1] - expected) <= 2);
    }
    manager.reset();
    manager.render(1600);
    std::puts("forced briefing initial/live event overrides, disable and speech slider passed");
}

static void disabledCleanupTests(FixtureManager &manager)
{
    manager.reset();
    manager.render(1600);
    const AudioHandle keep = add(manager, "Permanent");
    AudioEventRTS protectedSpeech{AsciiString("SpeechOne")};
    protectedSpeech.setUninterruptible(true);
    const AudioHandle disabled = manager.addAudioEvent(&protectedSpeech);
    manager.tick();
    CHECK(manager.isCurrentlyPlaying(disabled) && manager.getDisallowSpeech());
    manager.setAudioEventEnabled("SpeechOne", false);
    manager.setAudioEventEnabled("SpeechOne", true);
    manager.removeDisabledEvents();
    CHECK(!manager.isCurrentlyPlaying(disabled) && !manager.getDisallowSpeech());
    CHECK(manager.isCurrentlyPlaying(keep));
    CHECK(add(manager, "SpeechOne") >= AHSV_FirstHandle);
    manager.tick();
    manager.setAudioEventVolumeOverride("Permanent", 0);
    manager.setAudioEventVolumeOverride(AsciiString::TheEmptyString, -1);
    manager.removeDisabledEvents();
    CHECK(!manager.isCurrentlyPlaying(keep));
    manager.reset();
    manager.render(1600);
    std::puts("disabled cleanup follows active event volume after overrides are cleared passed");
}

static void movingRangeTests(FixtureManager &manager)
{
    Coord3D listener, forward, position;
    listener.set(0, 0, 0);
    forward.set(0, 1, 0);
    position.set(0, 5, 0);
    for (const Real exponent : {1.0f, 4.0f}) {
        manager.friend_getAudioSettings()->m_3DSoundRangeVolumeFadeExponent = exponent;
        const double expectedRatio = std::pow(1.0 - std::pow(0.5, exponent), 10.0 / 3.0) *
            std::pow(10.0 / 55.0, 2);
        for (const char *name : {"CriticalWorldLoop", "GlobalWorldLoop"}) {
            manager.reset();
            manager.render(1600);
            listener.set(0, 0, 0);
            manager.setListenerPosition(&listener, &forward);
            AudioEventRTS event(AsciiString(name), &position);
            const AudioHandle handle = manager.addAudioEvent(&event);
            const double nearEnergy = energy(manager.run(6400), -1, 1600);
            CHECK(nearEnergy > 1.0e9 && manager.isCurrentlyPlaying(handle));
            listener.set(0, -50, 0);
            manager.setListenerPosition(&listener, &forward);
            manager.run(1600);
            const double farEnergy = energy(manager.run(6400), -1, 1600);
            const double actualRatio = farEnergy / nearEnergy;
            std::printf("moving range event=%s exponent=%.0f energy ratio=%f expected=%f\n",
                name, exponent, actualRatio, expectedRatio);
            CHECK(actualRatio > expectedRatio * 0.95 && actualRatio < expectedRatio * 1.05);
            CHECK(manager.isCurrentlyPlaying(handle));
            listener.set(0, -110, 0);
            manager.setListenerPosition(&listener, &forward);
            manager.run(1600);
            CHECK(energy(manager.run(3200)) < 10000);
            CHECK(manager.isCurrentlyPlaying(handle));
            listener.set(0, 0, 0);
            manager.setListenerPosition(&listener, &forward);
            manager.run(1600);
            const double restored = energy(manager.run(6400), -1, 1600);
            CHECK(restored / nearEnergy > 0.95 && restored / nearEnergy < 1.05);
            CHECK(manager.isCurrentlyPlaying(handle));
        }
    }
    manager.friend_getAudioSettings()->m_3DSoundRangeVolumeFadeExponent = 1;
    manager.reset();
    manager.render(1600);
    listener.set(0, 0, 0);
    manager.setListenerPosition(&listener, &forward);
    std::puts("same-voice moving range gain and critical/global recovery passed");
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

// Exercise playback retirement without entering player/shroud admission code.
static AudioHandle enqueuePlayback(FixtureManager &manager, const AudioEventRTS &event)
{
    AudioRequest *request = manager.allocateAudioRequest();
    request->m_pendingEvent.Assign_No_Add_Ref(newInstance(DynamicAudioEventRTS)(event));
    auto &queued = *request->m_pendingEvent;
    manager.getInfoForAudioEvent(&queued);
    queued.setPlayingHandle(manager.nextPlaybackHandle());
    queued.generateFilename();
    queued.generatePlayInfo();
    manager.appendAudioRequest(request);
    return queued.getPlayingHandle();
}

static void minimumAudibleVolumeTests(FixtureManager &manager)
{
    Coord3D listener, forward, position;
    listener.set(0, 0, 0);
    forward.set(0, 1, 0);
    position.set(0, 5, 0);
    manager.setListenerPosition(&listener, &forward);
    manager.friend_getAudioSettings()->m_minVolume = 0.4f;
    for (float soundVolume : {1.0f, 0.25f, 0.0f}) {
        manager.reset();
        manager.setVolume(soundVolume, AudioAffect_Sound);
        manager.setVolume(0.2f, AudioAffect_Sound3D);
        AudioEventRTS event(AsciiString("OrdinaryWorldLoop"), &position);
        const AudioHandle handle = enqueuePlayback(manager, event);
        manager.tick();
        CHECK(manager.isCurrentlyPlaying(handle) == (soundVolume != 1.0f));
    }
    for (const char *name : {"GlobalWorldLoop", "CriticalWorldLoop"}) {
        manager.reset();
        manager.setVolume(1, AudioAffect_Sound);
        manager.setVolume(0.2f, AudioAffect_Sound3D);
        AudioEventRTS event(AsciiString(name), &position);
        const AudioHandle handle = enqueuePlayback(manager, event);
        manager.tick();
        CHECK(manager.isCurrentlyPlaying(handle));
    }
    manager.reset();
    manager.setVolume(0, AudioAffect_Sound3D);
    AudioEventRTS muted(AsciiString("OrdinaryWorldLoop"), &position);
    const AudioHandle mutedHandle = enqueuePlayback(manager, muted);
    manager.tick();
    CHECK(!manager.isCurrentlyPlaying(mutedHandle));
    manager.friend_getAudioSettings()->m_minVolume = 0;
    manager.setVolume(1, AudioAffect_Sound3D);
    manager.setVolume(1, AudioAffect_Sound);
    manager.reset();
    std::puts("legacy minimum-volume culling and global/critical exemptions passed");
}

static void speakerSelectionTests(FixtureManager &manager)
{
    manager.reset();
    CHECK(manager.getSpeakerType() == 0);
    const AudioHandle handle = add(manager, "Permanent");
    manager.run(1600);
    manager.setSpeakerType(1);
    CHECK(manager.getSpeakerType() == 1);
    CHECK(manager.isCurrentlyPlaying(handle));
    manager.run(1600);
    CHECK(manager.isCurrentlyPlaying(handle));
    manager.setSpeakerType(0);
    CHECK(manager.getSpeakerType() == 0);
    CHECK(manager.isCurrentlyPlaying(handle));
    manager.setSpeakerType(99);
    CHECK(manager.getSpeakerType() == 0);
    manager.setSpeakerType(3);
    CHECK(manager.getSpeakerType() == 0);
    manager.setSpeakerSurround(false);
    CHECK(!manager.getSpeakerSurround());
    manager.friend_getAudioSettings()->m_defaultSpeakerType3D = 3;
    manager.setSpeakerSurround(true);
    CHECK(manager.getSpeakerType() == 0);
    CHECK(!manager.getSpeakerSurround());
    manager.friend_getAudioSettings()->m_defaultSpeakerType3D = 2;
    manager.setSpeakerSurround(true);
    CHECK(manager.getSpeakerType() == 2);
    CHECK(manager.getSpeakerSurround());
    manager.setSpeakerSurround(false);
    CHECK(manager.getSpeakerType() == 0);
    CHECK(!manager.getSpeakerSurround());
    manager.friend_getAudioSettings()->m_defaultSpeakerType3D = 0;
    manager.setSpeakerType(2);
    CHECK(manager.getSpeakerType() == 2);
    manager.setSpeakerType(0);
    CHECK(manager.getSpeakerType() == 0);
    manager.reset();
    std::puts("speaker selection maps actual renderer without discarding voices");
}

static void mixedSpatialVolumeTests(FixtureManager &manager)
{
    manager.reset();
    manager.setVolume(1, AudioAffect_Sound3D);
    manager.setVolume(1, AudioAffect_Sound);
    Coord3D listener, forward, left, right;
    listener.set(0, 0, 0);
    forward.set(0, 1, 0);
    left.set(-5, 0, 0);
    right.set(5, 0, 0);
    manager.setListenerPosition(&listener, &forward);
    AudioEventRTS leftEvent(AsciiString("World"), &left);
    AudioEventRTS rightEvent(AsciiString("World"), &right);
    manager.addAudioEvent(&leftEvent);
    manager.addAudioEvent(&rightEvent);
    add(manager, "MixUI");
    const auto loud = manager.run(4800);
    const double balance = energy(loud, 0, 1200) / energy(loud, 1, 1200);
    CHECK(balance > 0.95 && balance < 1.05);
    auto tonePower = [](const std::vector<short> &pcm, double frequency) {
        double sine = 0, cosine = 0;
        for (int i = 0; i < 2400; ++i) {
            const double angle = 2.0 * 3.141592653589793 * frequency * i / 48000.0;
            sine += pcm[(i + 1200) * 2] * std::sin(angle);
            cosine += pcm[(i + 1200) * 2] * std::cos(angle);
        }
        return sine * sine + cosine * cosine;
    };
    manager.setVolume(0.25f, AudioAffect_Sound3D);
    const auto quietWorld = manager.run(4800);
    const double worldRatio = tonePower(quietWorld, 440) / tonePower(loud, 440);
    const double unchangedUI = tonePower(quietWorld, 400) / tonePower(loud, 400);
    std::printf("mixed world power ratio=%.6f unchanged UI=%.6f\n", worldRatio, unchangedUI);
    const double expectedWorldRatio = std::pow(0.25, 10.0 / 3.0);
    CHECK(worldRatio > expectedWorldRatio * 0.95 && worldRatio < expectedWorldRatio * 1.05);
    CHECK(unchangedUI > 0.98 && unchangedUI < 1.02);
    manager.setVolume(0.5f, AudioAffect_Sound);
    const auto quietBoth = manager.run(4800);
    const double uiRatio = tonePower(quietBoth, 400) / tonePower(quietWorld, 400);
    const double unchangedWorld = tonePower(quietBoth, 440) / tonePower(quietWorld, 440);
    std::printf("mixed UI power ratio=%.6f unchanged world=%.6f\n", uiRatio, unchangedWorld);
    const double expectedUIRatio = std::pow(0.5, 10.0 / 3.0);
    CHECK(uiRatio > expectedUIRatio * 0.95 && uiRatio < expectedUIRatio * 1.05);
    CHECK(unchangedWorld > 0.98 && unchangedWorld < 1.02);
    manager.setVolume(1, AudioAffect_Sound3D);
    manager.setVolume(1, AudioAffect_Sound);
    manager.reset();
    std::puts("simultaneous left/right world and centered UI category mix passed");
}

static void measuredSpatialMixTests(FixtureManager &manager)
{
    manager.friend_getAudioSettings()->m_use3DSoundRangeVolumeFade = false;
    Coord3D listener, forward;
    listener.set(0, 0, 0);
    forward.set(0, 1, 0);
    auto capture = [&](float x, float y, float z, float volume = 1, unsigned speaker = 0) {
        manager.reset();
        manager.render(1600);
        manager.setSpeakerType(speaker);
        manager.setVolume(volume, AudioAffect_Sound3D);
        manager.setListenerPosition(&listener, &forward);
        Coord3D position;
        position.set(x, y, z);
        AudioEventRTS event(AsciiString("World"), &position);
        manager.addAudioEvent(&event);
        return manager.run(6400);
    };
    const auto front = capture(0, 5, 0);
    const double baseline = energy(front, 0, 1600);
    struct Position { float x, y, z; double left, right; };
    for (const Position position : {
        Position{5, 0, 0, 0, 2},
        Position{-5, 0, 0, 2, 0},
        Position{3.5355339f, 3.5355339f, 0, 0.5, 1.5},
        Position{2.5f, 0, 4.3301270f, 2.0 / 3.0, 4.0 / 3.0},
        Position{0, -5, 0, 0.75, 0.75},
        Position{0, 0, 5, 1, 1}}) {
        const auto pcm = capture(position.x, position.y, position.z);
        const double left = std::sqrt(energy(pcm, 0, 1600) / baseline);
        const double right = std::sqrt(energy(pcm, 1, 1600) / baseline);
        CHECK(std::abs(left - position.left) < 0.025);
        CHECK(std::abs(right - position.right) < 0.025);
    }
    const auto half = capture(0, 5, 0, 0.5f);
    const double volumeRatio = energy(half, 0, 1600) / baseline;
    CHECK(std::abs(volumeRatio - std::pow(0.5, 10.0 / 3.0)) < 0.003);
    const auto atMinimum = capture(0, 10, 0);
    const double minimumEnergy = energy(atMinimum, 0, 1600);
    for (const float distance : {20.0f, 40.0f, 80.0f}) {
        const auto pcm = capture(0, distance, 0);
        const double ratio = std::sqrt(energy(pcm, 0, 1600) / minimumEnergy);
        CHECK(std::abs(ratio - 10.0 / distance) < 0.01);
    }
    forward.set(1, 0, 0);
    const auto rotated = capture(0, 5, 0);
    CHECK(energy(rotated, 0, 1600) > 1000 * energy(rotated, 1, 1600));
    forward.set(0, 1, 0);
    for (unsigned speaker : {1u, 2u}) {
        const auto dolbyFront = capture(0, 5, 0, 1, speaker);
        const auto dolbyRear = capture(0, -5, 0, 1, speaker);
        CHECK(std::abs(energy(dolbyRear, 0, 1600) / energy(dolbyFront, 0, 1600) - 1) < 0.03);
        double sum = 0, difference = 0;
        for (size_t frame = 1600; frame < dolbyRear.size() / 2; ++frame) {
            const double left = dolbyRear[frame * 2];
            const double right = dolbyRear[frame * 2 + 1];
            sum += (left + right) * (left + right);
            difference += (left - right) * (left - right);
        }
        CHECK(sum < difference * 0.0001);
    }
    manager.reset();
    manager.render(1600);
    manager.setSpeakerType(0);
    Coord3D rear;
    rear.set(0, -5, 0);
    AudioEventRTS continuing(AsciiString("World"), &rear);
    const AudioHandle handle = manager.addAudioEvent(&continuing);
    manager.run(1600);
    unsigned sourceOffset = 1600;
    for (unsigned speaker : {1u, 2u, 0u}) {
        manager.setSpeakerType(speaker);
        const auto pcm = manager.run(3200);
        CHECK(manager.isCurrentlyPlaying(handle));
        for (size_t frame = 1600; frame < pcm.size() / 2; ++frame) {
            double phase = 6.283185307179586 * 440 * (sourceOffset + frame) / 48000;
            double gain = 0.375;
            double rightSign = 1;
            if (speaker != 0) {
                phase = 6.283185307179586 * 440 * (sourceOffset + frame - 1) / 48000;
                gain = 0.5;
                rightSign = -1;
            }
            const double expected = 8000 * std::sin(phase) * gain;
            CHECK(std::abs(pcm[frame * 2] - expected) < 3);
            CHECK(std::abs(pcm[frame * 2 + 1] - expected * rightSign) < 3);
        }
        sourceOffset += 3200;
    }
    manager.reset();
    manager.setSpeakerType(0);
    manager.setVolume(1, AudioAffect_Sound3D);
    manager.friend_getAudioSettings()->m_use3DSoundRangeVolumeFade = true;
    std::puts("measured Miles volume, distance, angular panning and rear stereo behavior passed");
}

static void offScreenFilterTests(FixtureManager &manager)
{
    manager.reset();
    manager.render(1600);
    manager.setSpeakerType(0);
    manager.onScreen = true;
    Coord3D listener, forward, position;
    listener.set(0, 0, 0);
    forward.set(0, 1, 0);
    position.set(0, 5, 0);
    manager.setListenerPosition(&listener, &forward);
    AudioEventRTS event(AsciiString("FilteredWorldLoop"), &position);
    manager.screenQueries = 0;
    const AudioHandle handle = manager.addAudioEvent(&event);
    const auto initial = manager.run(1600);
    auto tonePower = [](const std::vector<short> &pcm, size_t start, size_t frames, double frequency) {
        CHECK((start + frames) * 2 <= pcm.size());
        double sine = 0, cosine = 0;
        for (size_t i = 0; i < frames; ++i) {
            const double angle = 6.283185307179586 * frequency * i / 48000;
            sine += pcm[(start + i) * 2] * std::sin(angle);
            cosine += pcm[(start + i) * 2] * std::cos(angle);
        }
        return (sine * sine + cosine * cosine) / (frames * frames);
    };
    auto lowPassPower = [](double sampleRate, double frequency) {
        const double c = 1.0 / std::tan(3.141592653589793 * 0.26 / 2.0);
        const double b = 1.0 / (1.0 + std::sqrt(2.0) * c + c * c);
        const double a1 = 2.0 * (1.0 - c * c) * b;
        const double a2 = (1.0 - std::sqrt(2.0) * c + c * c) * b;
        const double w = 6.283185307179586 * frequency / sampleRate;
        const double numeratorReal = b * (1.0 + 2.0 * std::cos(w) + std::cos(2.0 * w));
        const double numeratorImag = -b * (2.0 * std::sin(w) + std::sin(2.0 * w));
        const double denominatorReal = 1.0 + a1 * std::cos(w) + a2 * std::cos(2.0 * w);
        const double denominatorImag = -a1 * std::sin(w) - a2 * std::sin(2.0 * w);
        return (numeratorReal * numeratorReal + numeratorImag * numeratorImag) /
            (denominatorReal * denominatorReal + denominatorImag * denominatorImag);
    };
    const double bright = tonePower(initial, 400, 1200, 8000);
    const unsigned preparedPortions = manager.screenQueries;
    CHECK(preparedPortions >= 1 && preparedPortions <= 3);
    const unsigned preparedFrames = preparedPortions * 4800;
    manager.onScreen = false;
    const auto changed = manager.run(preparedFrames + 4800 - 1600);
    CHECK(tonePower(changed, 400, 1200, 8000) > bright * 0.95);
    const double dark = tonePower(changed, changed.size() / 2 - 2400, 2400, 8000);
    std::printf("offscreen8k power ratio=%f\n", dark / bright);
    const double expectedPower = lowPassPower(48000, 8000);
    CHECK(dark > bright * expectedPower * 0.95 && dark < bright * expectedPower * 1.05);
    CHECK(manager.isCurrentlyPlaying(handle));
    const unsigned preparedBeforeRestore = manager.screenQueries;
    manager.onScreen = true;
    const unsigned renderedFrames = 1600 + static_cast<unsigned>(changed.size() / 2);
    const auto restored = manager.run(preparedBeforeRestore * 4800 + 4800 - renderedFrames);
    const double restoredBright = tonePower(restored, restored.size() / 2 - 2400, 2400, 8000);
    CHECK(restoredBright > bright * 0.95 && restoredBright < bright * 1.05);
    manager.onScreen = false;
    manager.run(1600);
    manager.removeAudioEvent(handle);
    const auto decay = manager.run(12000);
    CHECK(!manager.isCurrentlyPlaying(handle));
    CHECK(energy(decay) > 1.0e7);
    CHECK(energy(decay, -1, 11000) < 10000);
    manager.reset();
    manager.render(1600);
    manager.onScreen = true;
    AudioEventRTS lowRate(AsciiString("FilteredLowRate"), &position);
    manager.addAudioEvent(&lowRate);
    const auto lowRateBright = manager.run(6400);
    manager.reset();
    manager.render(1600);
    manager.onScreen = false;
    manager.addAudioEvent(&lowRate);
    const auto lowRateDark = manager.run(6400);
    const double nativeRatio = tonePower(lowRateDark, 1600, 4800, 4000) /
        tonePower(lowRateBright, 1600, 4800, 4000);
    const double nativeExpected = lowPassPower(22050, 4000);
    std::printf("22050Hz source lowpass4k power ratio=%f expected=%f\n", nativeRatio, nativeExpected);
    CHECK(nativeRatio > nativeExpected * 0.95 && nativeRatio < nativeExpected * 1.05);
    manager.reset();
    manager.render(1600);
    manager.setVolume(0.1f, AudioAffect_Sound3D);
    AudioEventRTS headroom(AsciiString("FilteredHeadroom"), &position);
    manager.addAudioEvent(&headroom);
    const auto quietHeadroom = manager.run(6400);
    int peak = 0;
    for (size_t frame = 1600; frame < quietHeadroom.size() / 2; ++frame)
        peak = std::max(peak, std::abs(static_cast<int>(quietHeadroom[frame * 2])));
    const double prematureClipLimit = 32767 * std::pow(0.1, 5.0 / 3.0) * 0.5;
    CHECK(peak > prematureClipLimit + 10);
    CHECK(peak < 450);
    manager.setVolume(1, AudioAffect_Sound3D);
    manager.onScreen = true;
    manager.reset();
    std::puts("source-rate offscreen filtering, immutable variants and quiet-gain headroom passed");
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
    const double expectedRatio = std::pow(0.5, 10.0 / 3.0) * std::pow(10.0 / 55.0, 2);
    CHECK(ratio > expectedRatio * 0.95 && ratio < expectedRatio * 1.05);
    manager.stopAudio(AudioAffect_All);
    manager.run(1600);
    const AudioHandle looping = add(manager, "Permanent");
    const double loud = energy(manager.run(3200), -1, 256);
    manager.setVolume(0.5f, AudioAffect_Sound);
    manager.run(1600);
    const double quiet = energy(manager.run(3200), -1, 256);
    const double expectedCategoryRatio = std::pow(0.5, 10.0 / 3.0);
    CHECK(quiet / loud > expectedCategoryRatio * 0.95 && quiet / loud < expectedCategoryRatio * 1.05);
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

static void checkLongPcm(const std::vector<short> &pcm, unsigned offset, int voices = 1,
    double gain = std::pow(0.5, 0.3000000119))
{
    for (size_t frame = 128; frame < pcm.size() / 2; ++frame) {
        const double phase = 6.283185307179586 * 440 * (offset + frame) / 48000;
        const int source = static_cast<int>(std::lround(8000 * std::sin(phase)));
        const int expected = voices * static_cast<int>(std::lround(source * gain));
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
        checkLongPcm(clip->samples, 0, 1, 1.0);
        std::printf("cache budget=%zu logical=%zu capacity=%zu bytes=%zu\n", budget,
            clip->samples.size(), clip->samples.capacity(), cache.usedBytes());
    }
    OpenALAudioCache cache(200000);
    auto pinned = cache.load("Data\\Audio\\Sounds\\attack.wav", 48000, 2);
    CHECK(pinned != nullptr);
    auto large = cache.load(longFile, 48000, 2);
    CHECK(large != nullptr && cache.usedBytes() <= 240000);
    CHECK(cache.load("Data\\Audio\\Sounds\\attack.wav", 48000, 2) == pinned);
    checkLongPcm(large->samples, 0, 1, 1.0);
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

static void filteredCacheVariantTests()
{
    OpenALAudioCache cache(512000);
    const AsciiString filename("Data\\Audio\\Sounds\\filtered-low-rate.wav");
    auto plain = cache.load(filename, 48000, 1);
    auto filtered = cache.load(filename, 48000, 1, 0.26f);
    CHECK(plain != nullptr && filtered != nullptr && plain != filtered);
    CHECK(!plain->samples.empty() && plain->filteredSamples.empty());
    CHECK(filtered->samples.empty() && !filtered->filteredSamples.empty());
    CHECK(plain->frameCount() == filtered->frameCount());
    CHECK(cache.load(filename, 48000, 1, 1.01f) == plain);
    CHECK(cache.load(filename, 48000, 1, 0.26f) == filtered);
    CHECK(cache.usedBytes() == plain->byteSize() + filtered->byteSize());
    const size_t pinnedBytes = cache.usedBytes();
    cache.setBudget(pinnedBytes);
    CHECK(cache.load(filename, 48000, 1, 0.51f) == nullptr);
    CHECK(cache.usedBytes() == pinnedBytes);
    filtered.reset();
    auto otherFilter = cache.load(filename, 48000, 1, 0.51f);
    CHECK(otherFilter != nullptr && cache.usedBytes() <= pinnedBytes);
    CHECK(cache.load(filename, 48000, 1) == plain);
    std::puts("filtered/unfiltered cache variants, pins, float byte budgets and eviction passed");
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 || argc == 3);
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
        if (argc == 3) {
            const std::string selected = argv[2];
            if (selected == "paused-gain")
                pausedGainTests(manager);
            else if (selected == "forced-overrides")
                forcedOverrideTests(manager);
            else if (selected == "disabled-cleanup")
                disabledCleanupTests(manager);
            else if (selected == "moving-range")
                movingRangeTests(manager);
            else if (selected == "spatial-reference")
                measuredSpatialMixTests(manager);
            else if (selected == "offscreen-filter")
                offScreenFilterTests(manager);
            else
                CHECK(false);
        } else {
            requestAndSequenceTests(manager);
            limitAndPriorityTests(manager);
            streamedPayloadTest(manager);
            retireFinishedTests(manager);
            controlsAndStreamingTests(manager);
            pauseSnapshotTests(manager);
            pausedGainTests(manager);
            forcedOverrideTests(manager);
            disabledCleanupTests(manager);
            movingRangeTests(manager);
            loopStopAndForcedTests(manager);
            pendingCancellationAndMusicTests(manager);
            minimumAudibleVolumeTests(manager);
            speakerSelectionTests(manager);
            mixedSpatialVolumeTests(manager);
            measuredSpatialMixTests(manager);
            offScreenFilterTests(manager);
            positionAndVolumeTests(manager);
            delayAndReloadTests(manager);
            durationAndCacheTests(manager);
            replacementAndPoolTests(manager);
            cacheGrowthTests();
            filteredCacheVariantTests();
        }
        manager.reset();
        TheAudio = nullptr;
    }
    TheArchiveFileSystem = nullptr;
    TheLocalFileSystem = nullptr;
    TheFileSystem = nullptr;
    std::puts("engine-linked audio manager tests passed; no GameEngine or network startup invoked");
    return 0;
}
