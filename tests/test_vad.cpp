#include <cmath>
#include <vector>

#include "Vad.h"
#include "test_framework.h"

namespace {

constexpr int kRate = 16000;

// A block of silence (tiny amplitude, below the floor).
void appendSilence(std::vector<float>& out, double seconds) {
    const auto n = static_cast<std::size_t>(seconds * kRate);
    out.insert(out.end(), n, 0.0f);
}

// A block of a loud tone (clearly speech-level).
void appendTone(std::vector<float>& out, double seconds, float amplitude = 0.4f,
                double frequency = 200.0) {
    const auto n = static_cast<std::size_t>(seconds * kRate);
    const std::size_t base = out.size();
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(base + i) / kRate;
        out.push_back(amplitude *
                      static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * frequency * t)));
    }
}

}  // namespace

WF_TEST(Vad_trimsLeadingAndTrailingSilence) {
    std::vector<float> pcm;
    appendSilence(pcm, 1.0);  // 1 s of silence at the front
    appendTone(pcm, 1.0);     // 1 s of speech
    appendSilence(pcm, 1.0);  // 1 s of silence at the end

    const whisperflow::VadResult vad = whisperflow::detectSpeech(pcm, kRate);

    WF_CHECK(vad.hasSpeech);
    // The speech span (with padding) should be far smaller than the 3 s original.
    WF_CHECK(vad.keptSeconds < 1.6);
    WF_CHECK(vad.keptSeconds > 0.8);
    WF_CHECK(vad.trimmedLeadingMs > 700.0);   // most of the leading second is cut
    WF_CHECK(vad.trimmedTrailingMs > 700.0);  // most of the trailing second is cut
    WF_CHECK(std::fabs(vad.originalSeconds - 3.0) < 0.05);
}

WF_TEST(Vad_reportsNoSpeechForSilence) {
    std::vector<float> pcm;
    appendSilence(pcm, 2.0);

    const whisperflow::VadResult vad = whisperflow::detectSpeech(pcm, kRate);

    WF_CHECK(!vad.hasSpeech);
    // With no speech, the range covers the whole buffer (caller's gate decides).
    WF_CHECK_EQ(vad.startSample, static_cast<std::size_t>(0));
    WF_CHECK_EQ(vad.endSample, pcm.size());
}

WF_TEST(Vad_keepsAllSpeechWhenNoSilence) {
    std::vector<float> pcm;
    appendTone(pcm, 1.0);

    const whisperflow::VadResult vad = whisperflow::detectSpeech(pcm, kRate);

    WF_CHECK(vad.hasSpeech);
    // No silence to cut (only padding-limited); should keep essentially everything.
    WF_CHECK(vad.keptSeconds > 0.95);
}

WF_TEST(Vad_trimToSpeechShrinksBuffer) {
    std::vector<float> pcm;
    appendSilence(pcm, 0.8);
    appendTone(pcm, 0.6);
    appendSilence(pcm, 0.8);

    const std::vector<float> trimmed = whisperflow::trimToSpeech(pcm, kRate);
    WF_CHECK(trimmed.size() < pcm.size());
    WF_CHECK(!trimmed.empty());
}

WF_TEST(Vad_trimToSpeechReturnsInputWhenSilent) {
    std::vector<float> pcm;
    appendSilence(pcm, 1.0);

    const std::vector<float> trimmed = whisperflow::trimToSpeech(pcm, kRate);
    // No speech: return the input unchanged so the caller's own gate can reject it.
    WF_CHECK_EQ(trimmed.size(), pcm.size());
}

WF_TEST(Vad_handlesEmptyBuffer) {
    const whisperflow::VadResult vad = whisperflow::detectSpeech({}, kRate);
    WF_CHECK(!vad.hasSpeech);
    WF_CHECK_EQ(vad.startSample, static_cast<std::size_t>(0));
    WF_CHECK_EQ(vad.endSample, static_cast<std::size_t>(0));
}

WF_TEST(Vad_ignoresIsolatedBlip) {
    // A single 20 ms click surrounded by silence is below minSpeechMs (60 ms).
    std::vector<float> pcm;
    appendSilence(pcm, 0.5);
    appendTone(pcm, 0.02);  // 20 ms
    appendSilence(pcm, 0.5);

    const whisperflow::VadResult vad = whisperflow::detectSpeech(pcm, kRate);
    WF_CHECK(!vad.hasSpeech);
}
