#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "Transcriber.h"
#include "test_framework.h"
#include "test_paths.h"

namespace {

std::vector<float> tone(std::size_t samples, double frequency = 440.0) {
    std::vector<float> pcm(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / 16000.0;
        pcm[i] = static_cast<float>(0.3 * std::sin(2.0 * 3.14159265358979323846 * frequency * t));
    }
    return pcm;
}

const char* envOrNull(const char* name) {
#if defined(_MSC_VER)
    // Tests only need a best effort here; a leaked duplicate is acceptable.
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) == 0) {
        return value;
    }
    return nullptr;
#else
    return std::getenv(name);
#endif
}

}  // namespace

WF_TEST(Transcriber_resolvesDefaultParams) {
    whisperflow::TranscriptionOptions options;
    const whisperflow::ResolvedParams resolved = whisperflow::resolveParams(options);

    WF_CHECK(resolved.threads > 0);
    WF_CHECK(resolved.threads <= 16);
    WF_CHECK(resolved.detectLanguage);
    WF_CHECK(resolved.language.empty());
    WF_CHECK(!resolved.translate);
    WF_CHECK(!resolved.singleSegment);
    WF_CHECK(resolved.useGpu);
}

WF_TEST(Transcriber_resolvesExplicitParams) {
    whisperflow::TranscriptionOptions options;
    options.language = " RU ";
    options.threads = 3;
    options.translateToEnglish = true;
    options.singleSegment = true;
    options.useGpu = false;

    const whisperflow::ResolvedParams resolved = whisperflow::resolveParams(options);
    WF_CHECK_EQ(resolved.threads, 3);
    WF_CHECK(!resolved.detectLanguage);
    WF_CHECK_EQ(resolved.language, std::string("ru"));
    WF_CHECK(resolved.translate);
    WF_CHECK(resolved.singleSegment);
    WF_CHECK(!resolved.useGpu);
}

WF_TEST(Transcriber_reportsLibraryVersionAndBackends) {
    // These two calls run inside the linked whisper.cpp/ggml: they prove the
    // dependency is really linked and callable, not just declared.
    WF_CHECK(!whisperflow::Transcriber::version().empty());
    WF_CHECK(!whisperflow::Transcriber::backendInfo().empty());
    WF_CHECK(whisperflow::Transcriber::isKnownLanguage("auto"));
    WF_CHECK(whisperflow::Transcriber::isKnownLanguage("ru"));
    WF_CHECK(!whisperflow::Transcriber::isKnownLanguage("not-a-language"));
}

WF_TEST(Transcriber_failsCleanlyWithoutAModel) {
    whisperflow::TranscriptionOptions options;
    options.modelPath = std::filesystem::temp_directory_path() / "whisperflow-tests" / "nope.bin";

    whisperflow::Transcriber transcriber(options);
    WF_CHECK(!transcriber.isLoaded());

    bool called = false;
    whisperflow::TranscriptionResult captured;
    transcriber.transcribe(tone(16000), [&](const whisperflow::TranscriptionResult& result) {
        called = true;
        captured = result;
    });

    WF_CHECK(called);
    WF_CHECK(!captured.ok);
    WF_CHECK(captured.text.empty());
    WF_CHECK(captured.error.find("not found") != std::string::npos);
}

WF_TEST(Transcriber_rejectsANonModelFile) {
    const std::filesystem::path dir = wftest::scratchDirectory("badmodel");
    const std::filesystem::path fake = dir / "ggml-small.bin";
    WF_CHECK(wftest::writeFile(fake, "this is not a ggml model, whisper must reject it"));

    whisperflow::TranscriptionOptions options;
    options.modelPath = fake;

    whisperflow::Transcriber transcriber(options);
    // Runs the real whisper.cpp loader against a bogus file.
    WF_CHECK(!transcriber.ensureLoaded());
    WF_CHECK(transcriber.lastError().find("could not load") != std::string::npos);
    WF_CHECK(!transcriber.isLoaded());
}

WF_TEST(Transcriber_rejectsEmptyAudioBuffer) {
    whisperflow::TranscriptionOptions options;
    options.modelPath = std::filesystem::temp_directory_path() / "whisperflow-tests" / "nope.bin";

    whisperflow::Transcriber transcriber(options);
    whisperflow::TranscriptionResult captured;
    transcriber.transcribe({}, [&](const whisperflow::TranscriptionResult& result) {
        captured = result;
    });

    WF_CHECK(!captured.ok);
    WF_CHECK(captured.error.find("empty") != std::string::npos);
}

// Opt-in end-to-end check: set WHISPERFLOW_TEST_MODEL to a real ggml model to
// exercise the full decode path. Not required for a normal build.
WF_TEST(Transcriber_endToEndWhenModelProvided) {
    const char* modelPath = envOrNull("WHISPERFLOW_TEST_MODEL");
    if (modelPath == nullptr || std::string(modelPath).empty()) {
        std::cout << "  (skipped: set WHISPERFLOW_TEST_MODEL to run this case)\n";
        return;
    }

    whisperflow::TranscriptionOptions options;
    options.modelPath = modelPath;
    options.language = "en";

    whisperflow::Transcriber transcriber(options);
    WF_CHECK(transcriber.ensureLoaded());

    whisperflow::TranscriptionResult captured;
    transcriber.transcribe(tone(16000), [&](const whisperflow::TranscriptionResult& result) {
        captured = result;
    });

    WF_CHECK(captured.ok);
    WF_CHECK(captured.inferenceMs > 0.0);
    WF_CHECK(std::fabs(captured.audioSeconds - 1.0) < 0.01);
}
