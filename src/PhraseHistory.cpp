#include "PhraseHistory.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace whisperflow {
namespace {

void skipWhitespace(const std::string& text, std::size_t& cursor) {
    while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t' ||
                                    text[cursor] == '\r' || text[cursor] == '\n')) {
        ++cursor;
    }
}

std::uint32_t hex4(const std::string& text, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = text[offset + i];
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

std::string parseJsonString(const std::string& text, std::size_t& cursor) {
    if (cursor >= text.size() || text[cursor] != '"') {
        throw std::invalid_argument("expected string");
    }
    ++cursor;
    std::string out;
    while (cursor < text.size()) {
        const char c = text[cursor++];
        if (c == '"') {
            return out;
        }
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (cursor >= text.size()) {
            throw std::invalid_argument("unterminated escape");
        }
        const char escape = text[cursor++];
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
                if (cursor + 4 > text.size()) {
                    throw std::invalid_argument("short \\u escape");
                }
                std::uint32_t cp = hex4(text, cursor);
                cursor += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && cursor + 6 <= text.size() &&
                    text[cursor] == '\\' && text[cursor + 1] == 'u') {
                    const std::uint32_t low = hex4(text, cursor + 2);
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

std::string trim(const std::string& text) {
    const char* const kSpaces = " \t\r\n";
    const std::size_t first = text.find_first_not_of(kSpaces);
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(kSpaces);
    return text.substr(first, last - first + 1);
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

PhraseHistory::PhraseHistory(std::size_t maxEntries) : maxEntries_(maxEntries) {}

bool PhraseHistory::parseJson(const std::string& json, std::vector<std::string>& out) {
    out.clear();
    try {
        std::size_t cursor = 0;
        skipWhitespace(json, cursor);
        if (cursor >= json.size() || json[cursor] != '[') {
            return false;
        }
        ++cursor;
        skipWhitespace(json, cursor);
        if (cursor < json.size() && json[cursor] == ']') {
            ++cursor;
            return true;
        }
        for (;;) {
            skipWhitespace(json, cursor);
            if (cursor >= json.size() || json[cursor] != '"') {
                throw std::invalid_argument("history entries must be strings");
            }
            out.push_back(parseJsonString(json, cursor));
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
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string PhraseHistory::toJson(const std::vector<std::string>& entries) {
    std::ostringstream out;
    out << "[\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        out << "  " << jsonString(entries[i]);
        if (i + 1 < entries.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "]\n";
    return out.str();
}

bool PhraseHistory::load(const std::filesystem::path& path) {
    path_ = path;
    std::vector<std::string> parsed;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        entries_.clear();
        return true;  // optional file
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        entries_.clear();
        return false;
    }
    const bool ok = parseJson(buffer.str(), parsed);
    if (ok) {
        entries_ = std::move(parsed);
    } else {
        entries_.clear();  // corrupt history is not fatal, just start it over
    }
    return ok;
}

bool PhraseHistory::save(std::string& error) const {
    if (path_.empty()) {
        error = "Phrase history path is not set.";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "Could not create history directory: " + ec.message();
            return false;
        }
    }
    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "Could not open phrase history for writing: " + path_.string();
        return false;
    }
    file << toJson(entries_);
    if (!file.good()) {
        error = "Could not write phrase history: " + path_.string();
        return false;
    }
    return true;
}

void PhraseHistory::add(const std::string& phrase) {
    const std::string normalized = trim(phrase);
    if (normalized.empty()) {
        return;
    }
    if (!entries_.empty() && entries_.back() == normalized) {
        return;  // consecutive duplicate, nothing new to store
    }
    entries_.push_back(normalized);
    if (maxEntries_ > 0 && entries_.size() > maxEntries_) {
        entries_.erase(entries_.begin(),
                       entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - maxEntries_));
    }
}

const std::string& PhraseHistory::last() const {
    static const std::string kEmpty;
    return entries_.empty() ? kEmpty : entries_.back();
}

const std::string& PhraseHistory::at(std::size_t index) const {
    return entries_.at(index);
}

std::size_t PhraseHistory::size() const noexcept {
    return entries_.size();
}

std::size_t PhraseHistory::maxEntries() const noexcept {
    return maxEntries_;
}

void PhraseHistory::setMaxEntries(std::size_t maxEntries) {
    maxEntries_ = maxEntries;
    if (maxEntries_ > 0 && entries_.size() > maxEntries_) {
        entries_.erase(entries_.begin(),
                       entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - maxEntries_));
    }
}

void PhraseHistory::clear() {
    entries_.clear();
}

}  // namespace whisperflow
