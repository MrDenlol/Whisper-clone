#include "AppConfig.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace whisperflow {
namespace {

bool parseBool(const std::string& text, bool& outValue) {
    std::string normalized;
    normalized.reserve(text.size());
    for (char c : text) {
        normalized.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    }
    if (normalized == "1" || normalized == "true" || normalized == "on" || normalized == "yes") {
        outValue = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "off" || normalized == "no") {
        outValue = false;
        return true;
    }
    return false;
}

bool parseInt(const std::string& text, int& outValue) {
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        outValue = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool readNextValue(int argc, char** argv, int& index, const std::string& flag,
                   std::string& outValue, std::string& outError) {
    if (index + 1 >= argc) {
        outError = "Option " + flag + " requires a value.";
        return false;
    }
    ++index;
    outValue = argv[index];
    return true;
}

void applyKeyValue(AppConfig& config, const std::string& key, const std::string& value,
                   std::string& outError) {
    if (key == "model_path") {
        config.modelPath = value;
    } else if (key == "model_name" || key == "model") {
        ModelSize size{};
        if (!parseModelSize(value, size)) {
            outError = "Unknown model name '" + value + "' (expected tiny, base, small or medium).";
            return;
        }
        config.modelSize = size;
    } else if (key == "language") {
        config.language = value;
    } else if (key == "hotkey") {
        const auto parsed = parseHotkey(value);
        if (!parsed.spec) {
            outError = parsed.error;
            return;
        }
        config.hotkey = value;
    } else if (key == "threads") {
        int threads = 0;
        if (!parseInt(value, threads) || threads < 0) {
            outError = "threads must be a non-negative integer, got '" + value + "'.";
            return;
        }
        config.threads = threads;
    } else if (key == "use_gpu") {
        bool useGpu = true;
        if (!parseBool(value, useGpu)) {
            outError = "use_gpu must be true or false, got '" + value + "'.";
            return;
        }
        config.useGpu = useGpu;
    } else if (key == "translate") {
        bool translate = false;
        if (!parseBool(value, translate)) {
            outError = "translate must be true or false, got '" + value + "'.";
            return;
        }
        config.translateToEnglish = translate;
    } else if (key == "vad") {
        bool vad = true;
        if (!parseBool(value, vad)) {
            outError = "vad must be true or false, got '" + value + "'.";
            return;
        }
        config.vad = vad;
    } else if (key == "shrink_context") {
        bool shrink = true;
        if (!parseBool(value, shrink)) {
            outError = "shrink_context must be true or false, got '" + value + "'.";
            return;
        }
        config.shrinkContext = shrink;
    }
    // Unknown keys are ignored on purpose: a newer config file must not break an older build.
}

void applyConfigFile(AppConfig& config, const std::filesystem::path& path, std::string& outError) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return;  // optional file
    }

    std::string line;
    while (std::getline(file, line)) {
        std::string key;
        std::string value;
        if (!parseIniLine(line, key, value)) {
            continue;
        }
        applyKeyValue(config, key, value, outError);
        if (!outError.empty()) {
            config.valid = false;
            return;
        }
    }
}

}  // namespace

bool parseIniLine(const std::string& line, std::string& outKey, std::string& outValue) {
    auto trim = [](const std::string& text) {
        const char* const kSpaces = " \t\r\n";
        const std::size_t first = text.find_first_not_of(kSpaces);
        if (first == std::string::npos) {
            return std::string();
        }
        const std::size_t last = text.find_last_not_of(kSpaces);
        return text.substr(first, last - first + 1);
    };

    const std::string stripped = trim(line);
    if (stripped.empty() || stripped[0] == '#' || stripped[0] == ';' || stripped[0] == '[') {
        return false;
    }

    const std::size_t separator = stripped.find('=');
    if (separator == std::string::npos) {
        return false;
    }

    outKey = trim(stripped.substr(0, separator));
    outValue = trim(stripped.substr(separator + 1));
    if (outValue.size() >= 2 && outValue.front() == '"' && outValue.back() == '"') {
        outValue = outValue.substr(1, outValue.size() - 2);
    }
    return !outKey.empty();
}

std::filesystem::path configFilePath() {
    return userConfigDirectory() / "config.ini";
}

AppConfig loadConfig(int argc, char** argv) {
    AppConfig config;

    applyConfigFile(config, configFilePath(), config.error);
    if (!config.valid) {
        return config;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = (argv != nullptr && argv[i] != nullptr) ? argv[i] : std::string();
        std::string value;

        if (arg == "--help" || arg == "-h" || arg == "/?") {
            config.showHelp = true;
        } else if (arg == "--list-models") {
            config.listModels = true;
        } else if (arg == "--interactive") {
            config.interactive = true;
        } else if (arg == "--tray") {
            config.trayMode = true;
        } else if (arg == "--cpu") {
            config.useGpu = false;
        } else if (arg == "--translate") {
            config.translateToEnglish = true;
        } else if (arg == "--no-vad") {
            config.vad = false;
        } else if (arg == "--vad") {
            config.vad = true;
        } else if (arg == "--no-shrink-context") {
            config.shrinkContext = false;
        } else if (arg == "--model" || arg == "--model-path") {
            if (!readNextValue(argc, argv, i, arg, value, config.error)) {
                config.valid = false;
                return config;
            }
            config.modelPath = value;
        } else if (arg == "--model-name" || arg == "--size") {
            if (!readNextValue(argc, argv, i, arg, value, config.error)) {
                config.valid = false;
                return config;
            }
            ModelSize size{};
            if (!parseModelSize(value, size)) {
                config.valid = false;
                config.error = "Unknown model name '" + value + "' (expected tiny, base, small or medium).";
                return config;
            }
            config.modelSize = size;
        } else if (arg == "--language" || arg == "--lang") {
            if (!readNextValue(argc, argv, i, arg, value, config.error)) {
                config.valid = false;
                return config;
            }
            config.language = value;
        } else if (arg == "--hotkey") {
            if (!readNextValue(argc, argv, i, arg, value, config.error)) {
                config.valid = false;
                return config;
            }
            const auto parsed = parseHotkey(value);
            if (!parsed.spec) {
                config.valid = false;
                config.error = parsed.error;
                return config;
            }
            config.hotkey = value;
        } else if (arg == "--threads") {
            if (!readNextValue(argc, argv, i, arg, value, config.error)) {
                config.valid = false;
                return config;
            }
            int threads = 0;
            if (!parseInt(value, threads) || threads < 0) {
                config.valid = false;
                config.error = "--threads expects a non-negative integer, got '" + value + "'.";
                return config;
            }
            config.threads = threads;
        } else if (arg == "--wav" || arg == "--audio") {
            if (!readNextValue(argc, argv, i, arg, value, config.error)) {
                config.valid = false;
                return config;
            }
            config.wavInput = value;
        } else if (arg == "--save-wav") {
            if (!readNextValue(argc, argv, i, arg, value, config.error)) {
                config.valid = false;
                return config;
            }
            config.saveRecording = value;
        } else {
            config.valid = false;
            config.error = "Unknown option '" + arg + "'. Try --help.";
            return config;
        }
    }

    return config;
}

std::string usageText() {
    std::ostringstream out;
    out << "WhisperFlowClone - local, offline speech-to-text for Windows\n\n";
    out << "Usage: WhisperFlowClone [options]\n\n";
    out << "  (no options)        run in the background: hold the push-to-talk hotkey, speak,\n";
    out << "                      release - the text is pasted into the focused window\n";
    out << "  --tray              Windows tray build: no console, settings.json, menu, autostart\n";
    out << "  --interactive       console mode: Enter starts recording, Enter transcribes\n";
    out << "  --model <path>      use this ggml model file\n";
    out << "  --model-name <name> tiny | base | small (default) | medium\n";
    out << "  --language <code>   auto (default), ru, en, de, ...\n";
    out << "  --hotkey <combo>    push-to-talk keys, default " << kDefaultHotkey << '\n';
    out << "                      e.g. ctrl+shift+space, ctrl+alt+space, f9\n";
    out << "                      (Win-based combos usually fail: Windows reserves them)\n";
    out << "  --threads <n>       inference threads, 0 = all logical cores\n";
    out << "  --wav <file>        transcribe a 16-bit/float WAV file instead of the microphone\n";
    out << "  --save-wav <file>   also write the captured audio to this WAV file\n";
    out << "                      (microphone modes only)\n";
    out << "  --translate         translate the result to English\n";
    out << "  --no-vad            do not trim leading/trailing silence before recognition\n";
    out << "  --no-shrink-context keep whisper's full 30 s audio context (slower, short clips)\n";
    out << "  --cpu               do not use a GPU backend even if one is compiled in\n";
    out << "  --list-models       show where models are looked up and what is installed\n";
    out << "  --help              show this help\n\n";
    out << "Config file: " << configFilePath().string() << '\n';
    out << "  model_path, model_name, language, hotkey, threads, use_gpu, translate, vad,\n";
    out << "  shrink_context\n\n";
    out << "Models are not part of the repository. Default location:\n";
    out << "  " << userModelsDirectory().string() << "\\ggml-small.bin\n";
    out << "Download one with: scripts\\download-model.ps1 -Model small\n";
    return out.str();
}

}  // namespace whisperflow
