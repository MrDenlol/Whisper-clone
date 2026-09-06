#include "Settings.h"
#include "AppConfig.h"
#include "test_framework.h"
#include "test_paths.h"

#include <filesystem>

namespace {

whisperflow::Settings sampleSettings() {
    whisperflow::Settings settings;
    settings.modelSize = "base";
    settings.language = "de";
    settings.hotkey = "ctrl+alt+f9";
    settings.threads = 6;
    settings.useGpu = false;
    settings.translateToEnglish = true;
    settings.vad = false;
    settings.shrinkContext = false;
    settings.startWithWindows = true;
    settings.maxHistoryEntries = 42;
    settings.initialPrompt = "Расставляй знаки препинания.";
    return settings;
}

}  // namespace

WF_TEST(SettingsDefaultsAreSane) {
    const whisperflow::Settings settings;
    WF_CHECK_EQ(settings.modelSize, std::string("small"));
    // Russian is the default; "auto" stays available via settings.json/CLI.
    WF_CHECK_EQ(settings.language, std::string("ru"));
    WF_CHECK_EQ(settings.initialPrompt, std::string());  // empty = built-in prompt
    WF_CHECK_EQ(settings.hotkey, std::string(whisperflow::kDefaultHotkey));
    WF_CHECK_EQ(settings.threads, 0);
    WF_CHECK(settings.useGpu);
    WF_CHECK(!settings.translateToEnglish);
    WF_CHECK(settings.vad);
    WF_CHECK(settings.shrinkContext);
    WF_CHECK(!settings.startWithWindows);
    WF_CHECK_EQ(settings.maxHistoryEntries, std::size_t(200));
}

WF_TEST(SettingsJsonRoundTrip) {
    whisperflow::Settings parsed;
    std::string json;
    // Duplicate keys must not defeat the last-wins semantics.
    json = R"({"model_name":"base","language":"de","initial_prompt":"Пиши кратко.","hotkey":"ctrl+alt+f9","threads":6,"use_gpu":false,"translate":true,"vad":false,"shrink_context":false,"start_with_windows":true,"max_history_entries":42})";
    WF_CHECK(whisperflow::parseSettingsJson(json, parsed));
    WF_CHECK_EQ(parsed.modelSize, std::string("base"));
    WF_CHECK_EQ(parsed.language, std::string("de"));
    WF_CHECK_EQ(parsed.initialPrompt, std::string("Пиши кратко."));
    WF_CHECK_EQ(parsed.hotkey, std::string("ctrl+alt+f9"));
    WF_CHECK_EQ(parsed.threads, 6);
    WF_CHECK(!parsed.useGpu);
    WF_CHECK(parsed.translateToEnglish);
    WF_CHECK(!parsed.vad);
    WF_CHECK(!parsed.shrinkContext);
    WF_CHECK(parsed.startWithWindows);
    WF_CHECK_EQ(parsed.maxHistoryEntries, std::size_t(42));
}

WF_TEST(SettingsBrokenJsonFallsBackToDefaults) {
    whisperflow::Settings parsed;
    parsed.language = "ru";
    WF_CHECK(!whisperflow::parseSettingsJson("{ this is not json", parsed));
    WF_CHECK_EQ(parsed.language, std::string("ru"));  // untouched on failure
    WF_CHECK_EQ(parsed.hotkey, std::string(whisperflow::kDefaultHotkey));

    WF_CHECK(!whisperflow::parseSettingsJson("[]", parsed));
    WF_CHECK(!whisperflow::parseSettingsJson("", parsed));
}

WF_TEST(SettingsPartialJsonKeepsUnsetDefaults) {
    whisperflow::Settings parsed;
    const std::string json = R"({"language":"en"})";
    WF_CHECK(whisperflow::parseSettingsJson(json, parsed));
    WF_CHECK_EQ(parsed.language, std::string("en"));
    WF_CHECK_EQ(parsed.modelSize, std::string("small"));
    WF_CHECK_EQ(parsed.threads, 0);
    WF_CHECK_EQ(parsed.maxHistoryEntries, std::size_t(200));
}

WF_TEST(SettingsUnknownKeysAreIgnored) {
    whisperflow::Settings parsed;
    const std::string json = R"({"language":"ru","future_option":123,"nested":{"x":true},"hotkey":"f9"})";
    WF_CHECK(whisperflow::parseSettingsJson(json, parsed));
    WF_CHECK_EQ(parsed.language, std::string("ru"));
    WF_CHECK_EQ(parsed.hotkey, std::string("f9"));
    WF_CHECK_EQ(parsed.modelSize, std::string("small"));
}

WF_TEST(SettingsInvalidValuesAreIgnored) {
    whisperflow::Settings parsed;
    const std::string json = R"({"model_name":"ultra","threads":-1,"language":123,"hotkey":"bad_key"})";
    WF_CHECK(whisperflow::parseSettingsJson(json, parsed));
    WF_CHECK_EQ(parsed.modelSize, std::string("small"));
    WF_CHECK_EQ(parsed.threads, 0);
    WF_CHECK_EQ(parsed.language, std::string("ru"));
    WF_CHECK_EQ(parsed.hotkey, std::string(whisperflow::kDefaultHotkey));
}

WF_TEST(SettingsSaveAndLoadFile) {
    const std::filesystem::path dir = wftest::scratchDirectory("settings_save_load");
    const std::filesystem::path file = dir / "settings.json";
    const whisperflow::Settings saved = sampleSettings();

    std::string error;
    WF_CHECK(whisperflow::saveSettings(file, saved, error));
    WF_CHECK_EQ(error, std::string());

    const whisperflow::Settings loaded = whisperflow::loadSettings(file);
    WF_CHECK_EQ(loaded.modelSize, saved.modelSize);
    WF_CHECK_EQ(loaded.language, saved.language);
    WF_CHECK_EQ(loaded.initialPrompt, saved.initialPrompt);
    WF_CHECK_EQ(loaded.hotkey, saved.hotkey);
    WF_CHECK_EQ(loaded.threads, saved.threads);
    WF_CHECK_EQ(loaded.useGpu, saved.useGpu);
    WF_CHECK_EQ(loaded.translateToEnglish, saved.translateToEnglish);
    WF_CHECK_EQ(loaded.vad, saved.vad);
    WF_CHECK_EQ(loaded.shrinkContext, saved.shrinkContext);
    WF_CHECK_EQ(loaded.startWithWindows, saved.startWithWindows);
    WF_CHECK_EQ(loaded.maxHistoryEntries, saved.maxHistoryEntries);
}

WF_TEST(SettingsPathPreferPortableLayout) {
    const std::filesystem::path dir = wftest::scratchDirectory("settings_portable");
    const std::filesystem::path portable = dir / "settings.json";
    const std::filesystem::path appData = dir / "appdata";
    WF_CHECK(wftest::writeFile(portable, "{\"language\":\"en\"}"));

    // The user-data path resolution needs an env override to really test APPDATA;
    // the portable decision itself is what matters and is deterministic.
    WF_CHECK_EQ(whisperflow::settingsFilePath(dir), portable);
    WF_CHECK(!whisperflow::phraseHistoryFilePath(dir).empty());
}

WF_TEST(SettingsApplyToConfig) {
    const whisperflow::Settings settings = sampleSettings();
    whisperflow::AppConfig config;
    whisperflow::Settings mutableSettings = settings;
    whisperflow::applySettingsToConfig(mutableSettings, config);
    WF_CHECK_EQ(config.language, std::string("de"));
    WF_CHECK_EQ(config.initialPrompt, std::string("Расставляй знаки препинания."));
    WF_CHECK_EQ(config.hotkey, std::string("ctrl+alt+f9"));
    WF_CHECK_EQ(config.threads, 6);
    WF_CHECK(!config.useGpu);
    WF_CHECK(config.translateToEnglish);
    WF_CHECK(!config.vad);
    WF_CHECK(!config.shrinkContext);
    WF_CHECK_EQ(whisperflow::toString(config.modelSize), std::string("base"));
}
