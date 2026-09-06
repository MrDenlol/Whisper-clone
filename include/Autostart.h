#pragma once

#include <filesystem>
#include <string>

namespace whisperflow {

// Builds the registry command for starting the tray build with Windows.
// The executable path is quoted so spaces in the install folder do not break it.
[[nodiscard]] std::string autostartCommand(const std::filesystem::path& executablePath);

#if defined(_WIN32)
// Reads/writes HKCU\Software\Microsoft\Windows\CurrentVersion\Run. Errors are
// reported through outError; enabled=false removes the value when present.
[[nodiscard]] bool isAutostartEnabled();
[[nodiscard]] bool setAutostartEnabled(bool enabled, std::string& error);
#endif

}  // namespace whisperflow
