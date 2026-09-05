#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace whisperflow {

// ggml model sizes we expose in the UI/config. Small is the default:
// it is the best speed/quality balance for dictation on a CPU.
enum class ModelSize {
    Tiny,
    Base,
    Small,
    Medium,
};

// Outcome of a model lookup: where it was found (if anywhere) and every path
// that was probed, so the caller can print an actionable message.
struct ModelSearch {
    bool found{false};
    std::filesystem::path path;
    std::vector<std::filesystem::path> searchedPaths;
    std::string expectedFileName;
};

struct ModelQuery {
    ModelSize size{ModelSize::Small};
    std::filesystem::path explicitPath;        // --model or config.ini model_path
    std::filesystem::path executableDirectory; // portable layout: <exe dir>/models
    std::vector<std::filesystem::path> extraSearchDirectories;
};

[[nodiscard]] bool parseModelSize(const std::string& text, ModelSize& outSize);
[[nodiscard]] std::string toString(ModelSize size);
[[nodiscard]] std::string modelFileName(ModelSize size);
[[nodiscard]] std::vector<std::string> allModelSizeNames();

// %LOCALAPPDATA%\WhisperFlowClone (config) and ...\models (weights) on Windows.
// Falls back to $XDG_DATA_HOME / $HOME based directories elsewhere.
[[nodiscard]] std::filesystem::path userConfigDirectory();
[[nodiscard]] std::filesystem::path userModelsDirectory();

// Directories probed for a model file, highest priority first.
[[nodiscard]] std::vector<std::filesystem::path> defaultSearchDirectories(
    const std::filesystem::path& executableDirectory);

ModelSearch locateModel(const ModelQuery& query);

// Copy-pasteable instructions printed when no model file could be found.
[[nodiscard]] std::string describeMissingModel(const ModelSearch& search);

}  // namespace whisperflow
