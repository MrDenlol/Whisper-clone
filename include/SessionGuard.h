#pragma once

#include <atomic>
#include <string>

namespace whisperflow {

// Prevents a second dictation session from starting while one is running.
// Header-only and lock-free so that it can be unit tested on any platform.
class SessionGuard {
public:
    enum class State {
        Idle,
        Recording,
        Transcribing,
    };

    // Idle -> Recording. Returns false (and changes nothing) when busy.
    bool beginRecording() noexcept {
        State expected = State::Idle;
        return state_.compare_exchange_strong(expected, State::Recording);
    }

    // Recording -> Transcribing. Returns false when the session is not recording.
    bool beginTranscribing() noexcept {
        State expected = State::Recording;
        return state_.compare_exchange_strong(expected, State::Transcribing);
    }

    void finish() noexcept {
        state_.store(State::Idle);
    }

    [[nodiscard]] State state() const noexcept {
        return state_.load();
    }

    [[nodiscard]] bool isIdle() const noexcept {
        return state_.load() == State::Idle;
    }

    // Why a new session was refused - shown in the log.
    [[nodiscard]] std::string busyMessage() const {
        switch (state_.load()) {
            case State::Recording:
                return "already recording - release the hotkey first";
            case State::Transcribing:
                return "previous utterance is still being recognized";
            case State::Idle:
                break;
        }
        return {};
    }

private:
    std::atomic<State> state_{State::Idle};
};

}  // namespace whisperflow
