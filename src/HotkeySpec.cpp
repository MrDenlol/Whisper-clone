#include "HotkeySpec.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace whisperflow {
namespace {

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t");
    return text.substr(begin, end - begin + 1);
}

bool allDigits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(),
                                        [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

bool isFunctionKeyName(const std::string& key) {
    if (key.size() < 2 || key.size() > 3 || key[0] != 'f' || !allDigits(key.substr(1))) {
        return false;
    }
    const int number = std::stoi(key.substr(1));
    return number >= 1 && number <= 24;
}

bool isKnownKeyName(const std::string& key) {
    if (key == "space" || key == "enter" || key == "return" || key == "tab" || key == "esc" ||
        key == "escape" || key == "backspace") {
        return true;
    }
    if (key.size() == 1) {
        const char c = key[0];
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    }
    return isFunctionKeyName(key);
}

std::string HotkeySpec::toString() const {
    std::string text;
    if (ctrl) {
        text += "Ctrl+";
    }
    if (win) {
        text += "Win+";
    }
    if (alt) {
        text += "Alt+";
    }
    if (shift) {
        text += "Shift+";
    }
    text += key;
    return text;
}

HotkeyParseResult parseHotkey(const std::string& text) {
    HotkeySpec spec;
    std::string key;

    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, '+')) {
        const std::string part = toLower(trim(token));
        if (part.empty()) {
            return {{}, "empty part in hotkey '" + text + "'"};
        }
        if (part == "ctrl" || part == "control") {
            spec.ctrl = true;
        } else if (part == "alt") {
            spec.alt = true;
        } else if (part == "shift") {
            spec.shift = true;
        } else if (part == "win" || part == "windows" || part == "cmd" || part == "super") {
            spec.win = true;
        } else if (key.empty() && isKnownKeyName(part)) {
            key = part;
        } else {
            return {{}, "unknown key '" + part + "' in hotkey '" + text + "'"};
        }
    }

    if (key.empty()) {
        return {{}, "no key in hotkey '" + text + "' (example: ctrl+shift+space)"};
    }
    if (!spec.ctrl && !spec.alt && !spec.shift && !spec.win && !isFunctionKeyName(key)) {
        return {{}, "hotkey '" + text +
                        "' needs a modifier (ctrl, alt, shift, win) - a bare key would "
                        "swallow ordinary typing"};
    }

    spec.key = key;
    return {spec, {}};
}

}  // namespace whisperflow
