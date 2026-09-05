#include <cmath>
#include <vector>

#include "SpeechGate.h"
#include "test_framework.h"

namespace {

std::vector<float> tone(std::size_t samples, float amplitude, double frequency = 220.0) {
    std::vector<float> pcm(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / 16000.0;
        pcm[i] = amplitude * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * frequency * t));
    }
    return pcm;
}

}  // namespace

WF_TEST(SpeechGate_acceptsAudibleSpeech) {
    const std::vector<float> pcm = tone(16000, 0.30f);  // 1 second at a normal level
    const whisperflow::SpeechVerdict verdict = whisperflow::evaluateSpeech(pcm, 16000);

    WF_CHECK(verdict.acceptable);
    WF_CHECK(verdict.reason.empty());
    WF_CHECK(std::fabs(verdict.seconds - 1.0) < 0.001);
    WF_CHECK(verdict.peak > 0.2f);
    WF_CHECK(verdict.rms > 0.1f);
}

WF_TEST(SpeechGate_rejectsSilence) {
    const std::vector<float> pcm(16000, 0.0f);
    const whisperflow::SpeechVerdict verdict = whisperflow::evaluateSpeech(pcm, 16000);

    WF_CHECK(!verdict.acceptable);
    WF_CHECK(verdict.reason.find("silence") != std::string::npos);
}

WF_TEST(SpeechGate_rejectsRoomNoiseBelowThreshold) {
    // Very quiet hiss: peaks just under the configured floor.
    const whisperflow::SpeechGateSettings settings;
    std::vector<float> pcm = tone(16000, settings.minPeak * 0.5f);
    const whisperflow::SpeechVerdict verdict = whisperflow::evaluateSpeech(pcm, 16000, settings);

    WF_CHECK(!verdict.acceptable);
    WF_CHECK(verdict.reason.find("silence") != std::string::npos);
}

WF_TEST(SpeechGate_rejectsTooShortBuffer) {
    const std::vector<float> pcm = tone(1600, 0.5f);  // 100 ms
    const whisperflow::SpeechVerdict verdict = whisperflow::evaluateSpeech(pcm, 16000);

    WF_CHECK(!verdict.acceptable);
    WF_CHECK(verdict.reason.find("too short") != std::string::npos);
}

WF_TEST(SpeechGate_rejectsEmptyBuffer) {
    const whisperflow::SpeechVerdict verdict = whisperflow::evaluateSpeech({}, 16000);
    WF_CHECK(!verdict.acceptable);
    WF_CHECK(verdict.reason.find("empty") != std::string::npos);
}

WF_TEST(SpeechGate_thresholdsAreConfigurable) {
    whisperflow::SpeechGateSettings strict;
    strict.minSeconds = 2.0;

    const std::vector<float> pcm = tone(16000, 0.30f);  // exactly 1 second
    const whisperflow::SpeechVerdict verdict = whisperflow::evaluateSpeech(pcm, 16000, strict);

    WF_CHECK(!verdict.acceptable);
    WF_CHECK(verdict.reason.find("too short") != std::string::npos);
}

WF_TEST(SpeechGate_detectsMeaningfulText) {
    WF_CHECK(whisperflow::isMeaningfulText("Привет, мир"));
    WF_CHECK(whisperflow::isMeaningfulText("Hello world"));
    WF_CHECK(whisperflow::isMeaningfulText("42"));
    WF_CHECK(whisperflow::isMeaningfulText("Grüße"));
}

WF_TEST(SpeechGate_rejectsPunctuationOnlyText) {
    // Whisper answers silence and music with exactly these.
    WF_CHECK(!whisperflow::isMeaningfulText(""));
    WF_CHECK(!whisperflow::isMeaningfulText(" "));
    WF_CHECK(!whisperflow::isMeaningfulText("."));
    WF_CHECK(!whisperflow::isMeaningfulText("..."));
    WF_CHECK(!whisperflow::isMeaningfulText("[Blues] [Music]"));
    WF_CHECK(!whisperflow::isMeaningfulText("(applause)"));
    WF_CHECK(!whisperflow::isMeaningfulText("\xe2\x99\xaa\xe2\x99\xaa"));  // music notes
    WF_CHECK(whisperflow::isMeaningfulText("привет (пауза)"));  // real words win
    WF_CHECK(!whisperflow::isMeaningfulText("-"));
}
