#include "OpenALAudioDevice/OpenALDevice.h"
#include "OpenALAudioDevice/OpenALPcmStream.h"
#define RECOVERY_CALLS_IMPLEMENTATION
#include "recovery_calls.h"
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
using State = OpenALPcmStream::State;
using Result = OpenALPcmStream::QueueResult;
static void retryDelay() { std::this_thread::sleep_for(std::chrono::milliseconds(1050)); }
static void notificationRecovery()
{
    RecoveryTest::reset();
    RecoveryTest::enableSystemEvents(true);
    OpenALDevice device;
    CHECK(device.open());
    CHECK(RecoveryTest::eventRegistrations() == 1 && RecoveryTest::eventCallbackRegistered());
    RecoveryTest::disconnect();
    RecoveryTest::failReopen(true);
    RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT);
    CHECK(!device.update() && RecoveryTest::reopenCalls() == 1);
    CHECK(!device.update() && RecoveryTest::reopenCalls() == 1);
    RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_ADDED_SOFT, ALC_CAPTURE_DEVICE_SOFT);
    RecoveryTest::notify(ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT);
    CHECK(!device.update() && RecoveryTest::reopenCalls() == 1);
    std::thread arrival([] { RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_ADDED_SOFT); });
    arrival.join();
    CHECK(RecoveryTest::reopenCalls() == 1);
    CHECK(!device.update() && RecoveryTest::reopenCalls() == 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(310));
    CHECK(!device.update() && RecoveryTest::reopenCalls() == 3);
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(7300);
    while (std::chrono::steady_clock::now() < until) {
        // Notifications must not extend an active readiness window.
        RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_ADDED_SOFT);
        CHECK(!device.update());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const auto exhaustedCalls = RecoveryTest::reopenCalls();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    CHECK(!device.update() && RecoveryTest::reopenCalls() == exhaustedCalls);
    RecoveryTest::failReopen(false);
    CHECK(!device.update() && RecoveryTest::reopenCalls() == exhaustedCalls);
    RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_ADDED_SOFT);
    CHECK(device.update() && device.isConnected() && device.recoveryEpoch() == 1);
    CHECK(RecoveryTest::reopenCalls() == exhaustedCalls + 1);
    RecoveryTest::disconnect();
    RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT);
    CHECK(device.update() && device.recoveryEpoch() == 2);
    CHECK(device.close() && !RecoveryTest::eventCallbackRegistered());
    CHECK(RecoveryTest::eventUnregistrations() == 1);

    CHECK(device.open());
    OpenALDevice second;
    CHECK(second.open());
    CHECK(RecoveryTest::eventRegistrations() == 2);
    CHECK(device.close() && RecoveryTest::eventCallbackRegistered());
    RecoveryTest::disconnect();
    RecoveryTest::failReopen(true);
    RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT);
    CHECK(!second.update());
    const auto failed = RecoveryTest::reopenCalls();
    RecoveryTest::failReopen(false);
    RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_ADDED_SOFT);
    CHECK(second.update() && RecoveryTest::reopenCalls() == failed + 1);
    std::atomic<bool> running{true};
    std::thread notifier([&] {
        while (running.load())
            RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_ADDED_SOFT);
    });
    CHECK(second.close());
    running.store(false);
    notifier.join();
    CHECK(!RecoveryTest::eventCallbackRegistered() && RecoveryTest::eventUnregistrations() == 2);
    RecoveryTest::notify(ALC_EVENT_TYPE_DEVICE_ADDED_SOFT);

    for (bool failControl : {false, true}) {
        RecoveryTest::reset();
        RecoveryTest::enableSystemEvents(true);
        RecoveryTest::supportPlaybackAdded(failControl);
        RecoveryTest::failEventControl(failControl);
        CHECK(device.open() && !RecoveryTest::eventCallbackRegistered());
        CHECK(recoveryGetError(nullptr) == ALC_NO_ERROR);
        RecoveryTest::disconnect();
        RecoveryTest::failReopen(true);
        CHECK(!device.update() && RecoveryTest::reopenCalls() == 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(310));
        CHECK(!device.update() && RecoveryTest::reopenCalls() == 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        CHECK(!device.update() && RecoveryTest::reopenCalls() == 2);
        CHECK(device.close());
    }
}
int main()
{
    RecoveryTest::reset();
    OpenALDevice device;
    OpenALPcmStream playing, paused, finished, starved, replaced;
    CHECK(device.open() && device.isConnected());
    std::vector<short> pcm(16000, 1000);
    for (auto *stream : {&playing, &paused, &finished, &starved, &replaced}) {
        CHECK(stream->open(device, 48000, 1, 32768));
        CHECK(stream->queue(pcm.data(), 16000) == Result::Accepted);
    }
    CHECK(playing.markEnd() && playing.play());
    CHECK(paused.markEnd() && paused.play() && paused.pause());
    CHECK(finished.markEnd() && finished.play());
    CHECK(starved.play());
    CHECK(replaced.replaceQueuedAfter(200, pcm.data(), 100) && replaced.play());
    // Preserve unread finite PCM in the playing/paused streams while another reaches EOF.
    CHECK(playing.pause());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(finished.state() == State::Finished && starved.state() == State::Starved);
    CHECK(replaced.state() == State::Finished && replaced.getReadFrames() == 300 && !replaced.play());
    CHECK(playing.play());
    RecoveryTest::disconnect();
    CHECK(playing.state() != State::Finished);
    RecoveryTest::failReopen(true);
    CHECK(!device.update() && RecoveryTest::reopenCalls() == 1);
    CHECK(playing.state() == State::Disconnected && paused.state() == State::Disconnected);
    CHECK(finished.state() == State::Finished);
    CHECK(!finished.play());
    CHECK(!device.isConnected() && device.isOpen());
    const auto before = playing.getReadFrames();
    CHECK(playing.pause() && playing.play());
    CHECK(playing.getReadFrames() == before);
    CHECK(!device.update() && RecoveryTest::reopenCalls() == 1);
    CHECK(device.recoveryEpoch() == 0);
    CHECK(!RecoveryTest::reopenName().empty());
    bool wrong = true;
    std::thread other([&] { wrong = device.update(); });
    other.join();
    CHECK(!wrong && RecoveryTest::reopenCalls() == 1);
    RecoveryTest::failReopen(false);
    RecoveryTest::staleSuccess(true);
    retryDelay();
    CHECK(!device.update() && RecoveryTest::reopenCalls() == 2);
    CHECK(device.recoveryEpoch() == 0 && !device.isConnected());
    RecoveryTest::staleSuccess(false);
    retryDelay();
    CHECK(device.update() && RecoveryTest::reopenCalls() == 3);
    CHECK(device.recoveryEpoch() == 1 && device.isConnected());
    const unsigned calls = RecoveryTest::sourcePlayCalls();
    CHECK(!finished.play());
    finished.update();
    replaced.update();
    CHECK(!replaced.play());
    CHECK(RecoveryTest::sourcePlayCalls() == calls && finished.state() == State::Finished);
    paused.update();
    CHECK(paused.state() == State::Paused && RecoveryTest::sourcePlayCalls() == calls);
    playing.update();
    CHECK(RecoveryTest::sourcePlayCalls() == calls + 1);
    playing.update();
    CHECK(RecoveryTest::sourcePlayCalls() == calls + 1);
    CHECK(paused.play());
    CHECK(paused.state() == State::Playing);
    starved.update();
    CHECK(starved.queue(pcm.data(), 16000) == Result::Accepted);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(playing.getReadFrames() > before && paused.getReadFrames() > 0);
    CHECK(starved.getReadFrames() > 16000);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    RecoveryTest::disconnect();
    CHECK(device.update());
    CHECK(device.isConnected());
    CHECK(paused.pause());
    CHECK(device.update());
    playing.update();
    paused.update();
    CHECK(paused.state() == State::Paused);
    const auto queries = RecoveryTest::connectionQueries();
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(device.update() && device.isConnected());
        playing.state();
        playing.setGain(0.5f);
    }
    CHECK(RecoveryTest::connectionQueries() == queries);
    CHECK(playing.close() && paused.close() && finished.close() && starved.close() && replaced.close() && device.close());
    RecoveryTest::reset();
    RecoveryTest::forceStereo(true);
    CHECK(device.open(48000, nullptr, OpenALDevice::OutputMode::Headphones));
    CHECK(device.getOutputMode() == OpenALDevice::OutputMode::Stereo);
    RecoveryTest::disconnect();
    CHECK(device.update());
    CHECK(RecoveryTest::reopenOutputMode() == ALC_STEREO_BASIC_SOFT);
    OpenALPcmStream broken;
    CHECK(broken.open(device, 48000, 1, 1024));
    RecoveryTest::failPlay(true);
    CHECK(!broken.play() && broken.state() == State::Error);
    RecoveryTest::failPlay(false);
    const auto failedPlays = RecoveryTest::sourcePlayCalls();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    RecoveryTest::disconnect();
    CHECK(device.update());
    broken.update();
    CHECK(broken.state() == State::Error && RecoveryTest::sourcePlayCalls() == failedPlays);
    CHECK(broken.close() && device.close());
    RecoveryTest::reset();
    CHECK(device.openLoopback() && device.update());
    CHECK(RecoveryTest::connectionQueries() == 0 && device.close());
    RecoveryTest::suppressDisconnectExtension(true);
    CHECK(device.open() && device.update() && device.isConnected());
    CHECK(RecoveryTest::connectionQueries() == 0 && device.close());
    notificationRecovery();
    std::puts("notifications, bounded readiness, shared registration, device loss, pinned endpoint, throttled/stale retry, epochs, EOF, pause and starvation recovery passed");
}
