#include "ModelLocator.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace whisperflow {
namespace {

std::string toLower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::string trimmed(const std::string& text) {
    const char* const kSpaces = " \t\r\n";
    const std::size_t first = text.find_first_not_of(kSpaces);
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(kSpaces);
    return text.substr(first, last - first + 1);
}

std::string envValue(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
        const std::string result(value);
        std::free(value);
        return result;
    }
    return {};
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

}  // namespace

bool parseModelSize(const std::string& text, ModelSize& outSize) {
    const std::string normalized = toLower(trimmed(text));
    if (normalized == "tiny") {
        outSize = ModelSize::Tiny;
    } else if (normalized == "base") {
        outSize = ModelSize::Base;
    } else if (normalized == "small") {
        outSize = ModelSize::Small;
    } else if (normalized == "medium") {
        outSize = ModelSize::Medium;
    } else {
        return false;
    }
    return true;
}

std::string toString(ModelSize size) {
    switch (size) {
        case ModelSize::Tiny:   return "tiny";
        case ModelSize::Base:   return "base";
        case ModelSize::Small:  return "small";
        case ModelSize::Medium: return "medium";
    }
    return "small";
}

std::string modelFileName(ModelSize size) {
    return "ggml-" + toString(size) + ".bin";
}

std::vector<std::string> allModelSizeNames() {
    return {"tiny", "base", "small", "medium"};
}

std::filesystem::path userConfigDirectory() {
    const std::string localAppData = envValue("LOCALAPPDATA");
    if (!localAppData.empty()) {
        return std::filesystem::path(localAppData) / "WhisperFlowClone";
    }

    const std::string appData = envValue("APPDATA");
    if (!appData.empty()) {
        return std::filesystem::path(appData) / "WhisperFlowClone";
    }

    const std::string xdgData = envValue("XDG_DATA_HOME");
    if (!xdgData.empty()) {
        return std::filesystem::path(xdgData) / "WhisperFlowClone";
    }

    const std::string home = envValue("HOME");
    if (!home.empty()) {
        return std::filesystem::path(home) / ".local" / "share" / "WhisperFlowClone";
    }

    return std::filesystem::path("WhisperFlowClone");
}

std::filesystem::path userModelsDirectory() {
    return userConfigDirectory() / "models";
}

std::vector<std::filesystem::path> defaultSearchDirectories(
    const std::filesystem::path& executableDirectory) {
    std::vector<std::filesystem::path> dirs;
    auto push = [&dirs](const std::filesystem::path& dir) {
        std::error_code ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(dir, ec);
        const std::filesystem::path key = ec ? dir : canonical;
        if (std::find(dirs.begin(), dirs.end(), key) == dirs.end()) {
            dirs.push_back(key);
        }
    };

    push(userModelsDirectory());
    if (!executableDirectory.empty()) {
        push(executableDirectory / "models");
        push(executableDirectory);
    }
    push(std::filesystem::current_path() / "models");
    push(std::filesystem::current_path());
    return dirs;
}

ModelSearch locateModel(const ModelQuery& query) {
    ModelSearch result;
    result.expectedFileName = modelFileName(query.size);

    std::error_code ec;
    if (!query.explicitPath.empty()) {
        result.searchedPaths.push_back(query.explicitPath);
        if (std::filesystem::is_regular_file(query.explicitPath, ec)) {
            result.found = true;
            result.path = query.explicitPath;
            return result;
        }
        return result;
    }

    std::vector<std::filesystem::path> dirs = query.extraSearchDirectories;
    if (dirs.empty()) {
        dirs = defaultSearchDirectories(query.executableDirectory);
    }

    for (const std::filesystem::path& dir : dirs) {
        const std::filesystem::path candidate = dir / result.expectedFileName;
        result.searchedPaths.push_back(candidate);
        if (std::filesystem::is_regular_file(candidate, ec)) {
            result.found = true;
            result.path = candidate;
            return result;
        }
    }

    return result;
}

std::string describeMissingModel(const ModelSearch& search) {
    std::ostringstream out;
    out << "No ggml model file found (looked for '" << search.expectedFileName << "').\n";
    out << "Models are NOT stored in git. Download one and put it in the models folder:\n\n";
    out << "  PowerShell (downloads ggml-small.bin into %LOCALAPPDATA%\\WhisperFlowClone\\models):\n";
    out << "    .\\scripts\\download-model.ps1 -Model small\n\n";
    out << "  Or manually, from https://huggingface.co/ggerganov/whisper.cpp/tree/main :\n";
    for (const std::filesystem::path& candidate : search.searchedPaths) {
        out << "    " << candidate.string() << '\n';
    }
    out << "\nThen run again, or point at a file directly:\n";
    out << "  WhisperFlowClone.exe --model D:\\models\\ggml-small.bin\n";
    return out.str();
}

}  // namespace whisperflow
