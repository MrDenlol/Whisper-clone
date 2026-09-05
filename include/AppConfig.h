#pragma once

#include <filesystem>
#include <string>

#include "ModelLocator.h"

namespace whisperflow {

// Runtime settings: config.ini (user config directory) first, command line last.
struct AppConfig {
    ModelSize modelSize{ModelSize::Small};
    std::filesystem::path modelPath;        // optional explicit ggml file
    std::string language{"auto"};           // "auto" = detect, or "ru", "en", ...
    int threads{0};                         // 0 = all logical cores
    bool useGpu{true};                      // only used if a GPU backend is compiled in
    bool translateToEnglish{false};
    std::filesystem::path wavInput;         // transcribe a file instead of the microphone
    std::filesystem::path saveRecording;    // dump the captured audio next to the transcript
    bool interactive{false};                // Enter-to-record console mode instead of the hotkey
    bool listModels{false};
    bool showHelp{false};
    bool valid{true};
    std::string error;
};

// Parses one "key = value" line. Returns false for blanks and comments.
// Exposed for unit tests.
bool parseIniLine(const std::string& line, std::string& outKey, std::string& outValue);

// Applies a config file (if present) and then the command line on top of it.
AppConfig loadConfig(int argc, char** argv);

[[nodiscard]] std::filesystem::path configFilePath();
[[nodiscard]] std::string usageText();

}  // namespace whisperflow
