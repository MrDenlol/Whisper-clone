#include "Autostart.h"

#include <sstream>

namespace whisperflow {
namespace {

std::string quoteWindowsPath(const std::filesystem::path& path) {
    // Built with explicit appends rather than `"..." + std::string&& + "..."`:
    // GCC 12 at -O3 raises a bogus -Werror=restrict on that expression (PR 105329).
    std::string quoted;
    const std::string native = path.string();
    quoted.reserve(native.size() + 2);
    quoted.push_back('"');
    quoted.append(native);
    quoted.push_back('"');
    return quoted;
}

}  // namespace

std::string autostartCommand(const std::filesystem::path& executablePath) {
    std::string command = quoteWindowsPath(executablePath);
    command.append(" --tray");
    return command;
}

#if defined(_WIN32)

namespace {
std::wstring autostartCommandWide(const std::filesystem::path& executablePath) {
    std::wstring command;
    command.reserve(executablePath.wstring().size() + 8);
    command += L"\"";
    command += executablePath.wstring();
    command += L"\" --tray";
    return command;
}
}  // namespace

#endif

#if defined(_WIN32)

#include <windows.h>

namespace {
const wchar_t* const kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const kValueName = L"WhisperFlowClone";

std::filesystem::path currentExecutablePath() {
    wchar_t buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer);
}
}  // namespace

bool isAutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LONG status = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool setAutostartEnabled(bool enabled, std::string& error) {
    HKEY key = nullptr;
    const std::filesystem::path executable = currentExecutablePath();
    if (executable.empty()) {
        error = "Could not determine the executable path for autostart.";
        return false;
    }

    const LONG openStatus =
        RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr);
    if (openStatus != ERROR_SUCCESS) {
        error = "RegCreateKeyExW failed (error " + std::to_string(openStatus) + ").";
        return false;
    }

    LONG status = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring command = autostartCommandWide(executable);
        status = RegSetValueExW(key, kValueName, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;  // already disabled, that's fine
        }
    }
    RegCloseKey(key);

    if (status != ERROR_SUCCESS) {
        error = "Could not update the Run key (error " + std::to_string(status) + ").";
        return false;
    }
    return true;
}

#endif  // _WIN32

}  // namespace whisperflow
