#pragma once

#include <filesystem>
#include <string>

#include "HotkeySpec.h"

namespace whisperflow {

// User-editable runtime settings. This is the source of truth for --tray mode.
// Purely local JSON (no third-party JSON library), kept small and dependency-free.
struct Settings {
    std::string modelSize{"small"};       // tiny | base | small | medium
    std::string language{"auto"};         // auto | ru | en | de | ...
    std::string hotkey{kDefaultHotkey};   // parsed by parseHotkey()
    int threads{0};                       // 0 = all logical cores
    bool useGpu{true};                    // used only if a GPU backend is compiled in
    bool translateToEnglish{false};
    bool vad{true};
    bool shrinkContext{true};
    bool startWithWindows{false};
    std::size_t maxHistoryEntries{200};
};

// Portable layout wins: <exe dir>/settings.json, otherwise
// %APPDATA%\WhisperFlowClone\settings.json (userConfigDirectory elsewhere).
[[nodiscard]] std::filesystem::path settingsFilePath(
    const std::filesystem::path& executableDirectory);

// The phrase history lives next to settings.json so the whole state stays in one
// portable folder when the application is run from a USB stick.
[[nodiscard]] std::filesystem::path phraseHistoryFilePath(
    const std::filesystem::path& executableDirectory);

// Parses a settings.json document. Unknown keys, duplicate keys (last wins),
// malformed input and missing values all degrade gracefully: the default for the
// missing field is kept and the function returns false only for truly broken JSON.
[[nodiscard]] bool parseSettingsJson(const std::string& json, Settings& out);

[[nodiscard]] Settings loadSettings(const std::filesystem::path& path);

// Writes a pretty JSON object. Returns false and fills error on I/O failure.
[[nodiscard]] bool saveSettings(const std::filesystem::path& path, const Settings& settings,
                                std::string& error);

struct AppConfig;

// Applies settings values onto an already loaded AppConfig. Used when an existing
// settings.json should act as the source of truth for the tray mode.
void applySettingsToConfig(Settings& settings, AppConfig& config);

}  // namespace whisperflow
