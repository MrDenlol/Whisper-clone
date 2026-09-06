#include <string>
#include <vector>

#include "AppConfig.h"
#include "ModelLocator.h"
#include "test_framework.h"

namespace {

whisperflow::AppConfig parse(std::vector<std::string> args) {
    args.insert(args.begin(), "WhisperFlowClone");
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    return whisperflow::loadConfig(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

WF_TEST(Config_parsesIniLines) {
    std::string key;
    std::string value;

    WF_CHECK(whisperflow::parseIniLine("model_name = small", key, value));
    WF_CHECK_EQ(key, std::string("model_name"));
    WF_CHECK_EQ(value, std::string("small"));

    WF_CHECK(whisperflow::parseIniLine("model_path=C:\\models\\ggml-base.bin", key, value));
    WF_CHECK_EQ(key, std::string("model_path"));
    WF_CHECK_EQ(value, std::string("C:\\models\\ggml-base.bin"));

    WF_CHECK(whisperflow::parseIniLine("language = \"ru\"", key, value));
    WF_CHECK_EQ(value, std::string("ru"));

    WF_CHECK(!whisperflow::parseIniLine("# comment", key, value));
    WF_CHECK(!whisperflow::parseIniLine("; comment", key, value));
    WF_CHECK(!whisperflow::parseIniLine("[whisper]", key, value));
    WF_CHECK(!whisperflow::parseIniLine("   ", key, value));
    WF_CHECK(!whisperflow::parseIniLine("no-separator", key, value));
}

WF_TEST(Config_appliesCommandLineOverrides) {
    const whisperflow::AppConfig config =
        parse({"--model-name", "base", "--language", "ru", "--threads", "3", "--cpu", "--translate"});

    WF_CHECK(config.valid);
    WF_CHECK(config.modelSize == whisperflow::ModelSize::Base);
    WF_CHECK_EQ(config.language, std::string("ru"));
    WF_CHECK_EQ(config.threads, 3);
    WF_CHECK(!config.useGpu);
    WF_CHECK(config.translateToEnglish);
}

WF_TEST(Config_defaultsToSmallAndRussianLanguage) {
    const whisperflow::AppConfig config = parse({});
    WF_CHECK(config.valid);
    WF_CHECK(config.modelSize == whisperflow::ModelSize::Small);
    WF_CHECK_EQ(config.language, std::string("ru"));
    WF_CHECK(config.initialPrompt.empty());  // built-in per-language prompt
    WF_CHECK_EQ(config.threads, 0);
    WF_CHECK(!config.listModels);
    WF_CHECK(!config.showHelp);
}

WF_TEST(Config_parsesInitialPrompt) {
    const whisperflow::AppConfig config =
        parse({"--initial-prompt", "Расставляй запятые."});
    WF_CHECK(config.valid);
    WF_CHECK_EQ(config.initialPrompt, std::string("Расставляй запятые."));

    // "auto" stays available for language detection.
    const whisperflow::AppConfig autoLang = parse({"--language", "auto"});
    WF_CHECK(autoLang.valid);
    WF_CHECK_EQ(autoLang.language, std::string("auto"));
}

WF_TEST(Config_rejectsUnknownAndIncompleteOptions) {
    const whisperflow::AppConfig unknown = parse({"--nope"});
    WF_CHECK(!unknown.valid);
    WF_CHECK(unknown.error.find("--nope") != std::string::npos);

    const whisperflow::AppConfig missingValue = parse({"--threads"});
    WF_CHECK(!missingValue.valid);

    const whisperflow::AppConfig badNumber = parse({"--threads", "abc"});
    WF_CHECK(!badNumber.valid);

    const whisperflow::AppConfig badModel = parse({"--model-name", "large"});
    WF_CHECK(!badModel.valid);
}

WF_TEST(Config_recognisesHelpAndFileInputs) {
    const whisperflow::AppConfig help = parse({"--help"});
    WF_CHECK(help.valid);
    WF_CHECK(help.showHelp);

    const whisperflow::AppConfig wav = parse({"--wav", "clip.wav", "--save-wav", "out.wav"});
    WF_CHECK(wav.valid);
    WF_CHECK_EQ(wav.wavInput.string(), std::string("clip.wav"));
    WF_CHECK_EQ(wav.saveRecording.string(), std::string("out.wav"));

    const whisperflow::AppConfig tray = parse({"--tray"});
    WF_CHECK(tray.valid);
    WF_CHECK(tray.trayMode);
}

WF_TEST(Config_usageTextDocumentsModelLocation) {
    const std::string usage = whisperflow::usageText();
    WF_CHECK(usage.find("ggml-small.bin") != std::string::npos);
    WF_CHECK(usage.find("download-model.ps1") != std::string::npos);
    WF_CHECK(!whisperflow::configFilePath().empty());
}
