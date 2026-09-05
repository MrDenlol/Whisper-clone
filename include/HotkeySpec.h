#pragma once

#include <optional>
#include <string>

namespace whisperflow {

// Platform-independent description of a key combination, e.g. "ctrl+shift+space".
// Parsing lives here, away from Win32, so it can be unit tested on any platform.
struct HotkeySpec {
    bool ctrl{false};
    bool alt{false};
    bool shift{false};
    bool win{false};
    std::string key;  // lower-case key name: "space", "f9", "q", ...

    [[nodiscard]] std::string toString() const;
};

// Used when the user does not configure one. Combinations with the Win key are
// deliberately avoided: Windows reserves Win+Space for the keyboard layout switch
// and registers a number of other Win+X hotkeys itself, so RegisterHotKey answers
// ERROR_HOTKEY_ALREADY_REGISTERED (1409) for those. Verified on Windows 11 24H2.
inline constexpr const char* kDefaultHotkey = "ctrl+shift+space";

struct HotkeyParseResult {
    std::optional<HotkeySpec> spec;
    std::string error;  // empty on success
};

// Accepts "ctrl+shift+space", "Ctrl+Win+Space", "f9", "alt+q". Case-insensitive,
// surrounding spaces around a part are ignored. A bare key is only allowed for
// F1-F24, so a typo cannot swallow ordinary typing.
[[nodiscard]] HotkeyParseResult parseHotkey(const std::string& text);

// True when the key name is one this app knows how to map to a virtual key code.
[[nodiscard]] bool isKnownKeyName(const std::string& key);

// True for "f1".."f24".
[[nodiscard]] bool isFunctionKeyName(const std::string& key);

}  // namespace whisperflow
