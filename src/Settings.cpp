#include "Settings.h"

#include "AppConfig.h"
#include "ModelLocator.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace whisperflow {
namespace {

struct JsonValue {
    enum class Kind { Null, Str, Number, Bool };
    Kind kind{Kind::Null};
    std::string text;
    long long number{0};
    bool boolean{false};

    static JsonValue makeString(const std::string& value) {
        JsonValue v;
        v.kind = Kind::Str;
        v.text = value;
        return v;
    }

    static JsonValue makeNumber(long long value) {
        JsonValue v;
        v.kind = Kind::Number;
        v.number = value;
        return v;
    }

    static JsonValue makeBool(bool value) {
        JsonValue v;
        v.kind = Kind::Bool;
        v.boolean = value;
        return v;
    }
};

[[nodiscard]] bool asString(const JsonValue& value, std::string& out) {
    if (value.kind != JsonValue::Kind::Str) {
        return false;
    }
    out = value.text;
    return true;
}

[[nodiscard]] bool asBool(const JsonValue& value, bool& out) {
    if (value.kind != JsonValue::Kind::Bool) {
        return false;
    }
    out = value.boolean;
    return true;
}

[[nodiscard]] bool asNumber(const JsonValue& value, int& out) {
    if (value.kind != JsonValue::Kind::Number) {
        return false;
    }
    out = static_cast<int>(value.number);
    return true;
}

void skipWhitespace(const std::string& json, std::size_t& cursor) {
    while (cursor < json.size() &&
           (json[cursor] == ' ' || json[cursor] == '\t' || json[cursor] == '\r' ||
            json[cursor] == '\n')) {
        ++cursor;
    }
}

void appendUtf8(std::string& out, std::uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

std::uint32_t hex4(const std::string& json, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = json[offset + i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            throw std::invalid_argument("bad \\u escape");
        }
    }
    return value;
}

std::string parseString(const std::string& json, std::size_t& cursor) {
    if (cursor >= json.size() || json[cursor] != '"') {
        throw std::invalid_argument("expected string");
    }
    ++cursor;

    std::string out;
    while (cursor < json.size()) {
        const char c = json[cursor++];
        if (c == '"') {
            return out;
        }
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (cursor >= json.size()) {
            throw std::invalid_argument("unterminated escape");
        }
        const char escape = json[cursor++];
        switch (escape) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (cursor + 4 > json.size()) {
                    throw std::invalid_argument("short \\u escape");
                }
                std::uint32_t cp = hex4(json, cursor);
                cursor += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && cursor + 6 <= json.size() &&
                    json[cursor] == '\\' && json[cursor + 1] == 'u') {
                    const std::uint32_t low = hex4(json, cursor + 2);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        cursor += 6;
                    }
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                throw std::invalid_argument("bad escape");
        }
    }
    throw std::invalid_argument("unterminated string");
}

JsonValue parseNumber(const std::string& json, std::size_t& cursor) {
    const std::size_t start = cursor;
    while (cursor < json.size() && json[cursor] != ',' && json[cursor] != '}' &&
           json[cursor] != ']' && json[cursor] != ' ') {
        ++cursor;
    }
    const std::string text = json.substr(start, cursor - start);
    try {
        return JsonValue::makeNumber(std::stoll(text));
    } catch (const std::exception&) {
        return JsonValue{};
    }
}

JsonValue parseValue(const std::string& json, std::size_t& cursor);

JsonValue parseObject(const std::string& json, std::size_t& cursor) {
    // The caller consumed '{'.
    JsonValue value;
    value.kind = JsonValue::Kind::Null;

    skipWhitespace(json, cursor);
    if (cursor < json.size() && json[cursor] == '}') {
        ++cursor;
        return value;
    }

    for (;;) {
        skipWhitespace(json, cursor);
        if (cursor >= json.size() || json[cursor] != '"') {
            throw std::invalid_argument("expected object key");
        }
        const std::string key = parseString(json, cursor);
        skipWhitespace(json, cursor);
        if (cursor >= json.size() || json[cursor] != ':') {
            throw std::invalid_argument("expected ':'");
        }
        ++cursor;
        skipWhitespace(json, cursor);
        (void)parseValue(json, cursor);  // parsed and discarded; unknown keys are ignored
        skipWhitespace(json, cursor);
        if (cursor < json.size() && json[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (cursor < json.size() && json[cursor] == '}') {
            ++cursor;
            break;
        }
        throw std::invalid_argument("expected ',' or '}'");
    }
    return value;
}

JsonValue parseArray(const std::string& json, std::size_t& cursor) {
    JsonValue value;
    value.kind = JsonValue::Kind::Null;
    skipWhitespace(json, cursor);
    if (cursor < json.size() && json[cursor] == ']') {
        ++cursor;
        return value;
    }
    for (;;) {
        skipWhitespace(json, cursor);
        (void)parseValue(json, cursor);
        skipWhitespace(json, cursor);
        if (cursor < json.size() && json[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (cursor < json.size() && json[cursor] == ']') {
            ++cursor;
            break;
        }
        throw std::invalid_argument("expected ',' or ']'");
    }
    return value;
}

JsonValue parseValue(const std::string& json, std::size_t& cursor) {
    skipWhitespace(json, cursor);
    if (cursor >= json.size()) {
        throw std::invalid_argument("unexpected end");
    }

    const char c = json[cursor];
    if (c == '"') {
        return JsonValue::makeString(parseString(json, cursor));
    }
    if (c == '{') {
        ++cursor;
        return parseObject(json, cursor);
    }
    if (c == '[') {
        ++cursor;
        return parseArray(json, cursor);
    }
    if (c == 't' && json.compare(cursor, 4, "true") == 0) {
        cursor += 4;
        return JsonValue::makeBool(true);
    }
    if (c == 'f' && json.compare(cursor, 5, "false") == 0) {
        cursor += 5;
        return JsonValue::makeBool(false);
    }
    if (c == 'n' && json.compare(cursor, 4, "null") == 0) {
        cursor += 4;
        return JsonValue{};
    }
    if ((c >= '0' && c <= '9') || c == '-' || c == '+') {
        return parseNumber(json, cursor);
    }
    throw std::invalid_argument("unexpected character");
}

void setTypedValue(Settings& settings, const std::string& key, const JsonValue& value) {
    std::string text;
    bool boolean = false;
    int number = 0;

    if (key == "model_size" || key == "model_name" || key == "model") {
        if (asString(value, text)) {
            ModelSize size{};
            if (parseModelSize(text, size)) {
                settings.modelSize = toString(size);
            }
        }
    } else if (key == "language") {
        if (asString(value, text)) {
            settings.language = text;
        }
    } else if (key == "initial_prompt") {
        if (asString(value, text)) {
            settings.initialPrompt = text;
        }
    } else if (key == "hotkey") {
        if (asString(value, text)) {
            if (parseHotkey(text).spec) {
                settings.hotkey = text;
            }
        }
    } else if (key == "threads") {
        if (asNumber(value, number) && number >= 0) {
            settings.threads = number;
        }
    } else if (key == "use_gpu" || key == "gpu") {
        if (asBool(value, boolean)) {
            settings.useGpu = boolean;
        }
    } else if (key == "translate" || key == "translate_to_english") {
        if (asBool(value, boolean)) {
            settings.translateToEnglish = boolean;
        }
    } else if (key == "vad") {
        if (asBool(value, boolean)) {
            settings.vad = boolean;
        }
    } else if (key == "shrink_context") {
        if (asBool(value, boolean)) {
            settings.shrinkContext = boolean;
        }
    } else if (key == "start_with_windows" || key == "autostart") {
        if (asBool(value, boolean)) {
            settings.startWithWindows = boolean;
        }
    } else if (key == "max_history_entries") {
        if (asNumber(value, number) && number > 0) {
            settings.maxHistoryEntries = static_cast<std::size_t>(number);
        }
    }
    // Unknown keys are ignored intentionally: a newer settings.json must not
    // make an older build refuse to start.
}

void parseObjectInto(const std::string& json, std::size_t& cursor, std::vector<std::pair<std::string, JsonValue>>& out) {
    skipWhitespace(json, cursor);
    if (cursor < json.size() && json[cursor] == '}') {
        ++cursor;
        return;
    }
    for (;;) {
        skipWhitespace(json, cursor);
        if (cursor >= json.size() || json[cursor] != '"') {
            throw std::invalid_argument("expected object key");
        }
        const std::string key = parseString(json, cursor);
        skipWhitespace(json, cursor);
        if (cursor >= json.size() || json[cursor] != ':') {
            throw std::invalid_argument("expected ':'");
        }
        ++cursor;
        skipWhitespace(json, cursor);
        JsonValue value;
        if (cursor < json.size() && json[cursor] == '"') {
            value = JsonValue::makeString(parseString(json, cursor));
        } else if (cursor < json.size() && json[cursor] == '{') {
            ++cursor;
            (void)parseObject(json, cursor);
        } else if (cursor < json.size() && json[cursor] == '[') {
            ++cursor;
            (void)parseArray(json, cursor);
        } else {
            value = parseValue(json, cursor);
        }
        out.emplace_back(key, value);
        skipWhitespace(json, cursor);
        if (cursor < json.size() && json[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (cursor < json.size() && json[cursor] == '}') {
            ++cursor;
            break;
        }
        throw std::invalid_argument("expected ',' or '}'");
    }
}

std::string jsonString(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "\\u" << std::hex << std::nouppercase << std::setw(4)
                        << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c))
                        << std::dec << std::setfill(' ');
                } else {
                    out << c;
                }
        }
    }
    out << '"';
    return out.str();
}

}  // namespace

namespace {

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

std::filesystem::path settingsUserDirectory() {
    const std::string appData = envValue("APPDATA");
    if (!appData.empty()) {
        return std::filesystem::path(appData) / "WhisperFlowClone";
    }
    const std::string localAppData = envValue("LOCALAPPDATA");
    if (!localAppData.empty()) {
        return std::filesystem::path(localAppData) / "WhisperFlowClone";
    }
    return userConfigDirectory();
}

}  // namespace

std::filesystem::path settingsFilePath(const std::filesystem::path& executableDirectory) {
    if (!executableDirectory.empty()) {
        const std::filesystem::path portable = executableDirectory / "settings.json";
        std::error_code ec;
        if (std::filesystem::is_regular_file(portable, ec)) {
            return portable;
        }
    }
    return settingsUserDirectory() / "settings.json";
}

std::filesystem::path phraseHistoryFilePath(const std::filesystem::path& executableDirectory) {
    return settingsFilePath(executableDirectory).parent_path() / "phrase_history.json";
}

bool parseSettingsJson(const std::string& json, Settings& out) {
    Settings parsed;
    try {
        std::size_t cursor = 0;
        skipWhitespace(json, cursor);
        if (cursor >= json.size() || json[cursor] != '{') {
            return false;
        }
        ++cursor;
        std::vector<std::pair<std::string, JsonValue>> entries;
        parseObjectInto(json, cursor, entries);
        for (const auto& entry : entries) {
            setTypedValue(parsed, entry.first, entry.second);
        }
    } catch (const std::exception&) {
        return false;
    }
    out = parsed;
    return true;
}

Settings loadSettings(const std::filesystem::path& path) {
    Settings settings;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return settings;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        return settings;
    }
    Settings parsed;
    if (parseSettingsJson(buffer.str(), parsed)) {
        return parsed;
    }
    return settings;
}

bool saveSettings(const std::filesystem::path& path, const Settings& settings, std::string& error) {
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "Could not create settings directory: " + ec.message();
            return false;
        }
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "Could not open settings file for writing: " + path.string();
        return false;
    }

    file << "{\n";
    file << "  \"model_name\": " << jsonString(settings.modelSize) << ",\n";
    file << "  \"language\": " << jsonString(settings.language) << ",\n";
    file << "  \"initial_prompt\": " << jsonString(settings.initialPrompt) << ",\n";
    file << "  \"hotkey\": " << jsonString(settings.hotkey) << ",\n";
    file << "  \"threads\": " << settings.threads << ",\n";
    file << "  \"use_gpu\": " << (settings.useGpu ? "true" : "false") << ",\n";
    file << "  \"translate\": " << (settings.translateToEnglish ? "true" : "false") << ",\n";
    file << "  \"vad\": " << (settings.vad ? "true" : "false") << ",\n";
    file << "  \"shrink_context\": " << (settings.shrinkContext ? "true" : "false") << ",\n";
    file << "  \"start_with_windows\": " << (settings.startWithWindows ? "true" : "false") << ",\n";
    file << "  \"max_history_entries\": " << settings.maxHistoryEntries << "\n";
    file << "}\n";

    if (!file.good()) {
        error = "Could not write settings file: " + path.string();
        return false;
    }
    return true;
}

void applySettingsToConfig(Settings& settings, AppConfig& config) {
    ModelSize size{};
    if (parseModelSize(settings.modelSize, size)) {
        config.modelSize = size;
    }
    config.language = settings.language;
    config.initialPrompt = settings.initialPrompt;
    config.hotkey = settings.hotkey;
    config.threads = settings.threads;
    config.useGpu = settings.useGpu;
    config.translateToEnglish = settings.translateToEnglish;
    config.vad = settings.vad;
    config.shrinkContext = settings.shrinkContext;
}

}  // namespace whisperflow
