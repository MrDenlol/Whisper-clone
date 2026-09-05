#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace wftest {

// Per-test scratch directory under the system temp folder.
inline std::filesystem::path scratchDirectory(const std::string& name) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "whisperflow-tests" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

inline bool writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return file.good();
}

}  // namespace wftest
