#include <atomic>
#include <thread>
#include <vector>

#include "SessionGuard.h"
#include "test_framework.h"

WF_TEST(SessionGuard_startsIdle) {
    whisperflow::SessionGuard guard;
    WF_CHECK(guard.isIdle());
    WF_CHECK(guard.state() == whisperflow::SessionGuard::State::Idle);
    WF_CHECK(guard.busyMessage().empty());
}

WF_TEST(SessionGuard_refusesSecondRecordingSession) {
    whisperflow::SessionGuard guard;

    WF_CHECK(guard.beginRecording());
    WF_CHECK(!guard.beginRecording());
    WF_CHECK(guard.busyMessage().find("recording") != std::string::npos);

    guard.finish();
    WF_CHECK(guard.isIdle());
    WF_CHECK(guard.beginRecording());
}

WF_TEST(SessionGuard_refusesRecordingWhileTranscribing) {
    whisperflow::SessionGuard guard;

    WF_CHECK(guard.beginRecording());
    WF_CHECK(guard.beginTranscribing());
    WF_CHECK(guard.state() == whisperflow::SessionGuard::State::Transcribing);
    WF_CHECK(!guard.beginRecording());
    WF_CHECK(guard.busyMessage().find("recognized") != std::string::npos);

    guard.finish();
    WF_CHECK(guard.isIdle());
}

WF_TEST(SessionGuard_transcribingRequiresRecordingFirst) {
    whisperflow::SessionGuard guard;
    WF_CHECK(!guard.beginTranscribing());
    WF_CHECK(guard.isIdle());
}

WF_TEST(SessionGuard_allowsOnlyOneWinnerUnderContention) {
    whisperflow::SessionGuard guard;
    std::atomic<int> winners{0};

    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&guard, &winners] {
            if (guard.beginRecording()) {
                ++winners;
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    WF_CHECK_EQ(winners.load(), 1);
}
