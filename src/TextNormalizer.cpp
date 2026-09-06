#include "TextNormalizer.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace whisperflow {
namespace {

// Lowercases ASCII and Cyrillic (UTF-8) in place. Every mapping used here is
// byte-length preserving, which keeps the result index-aligned with the input:
//   А..П (D0 90..D0 9F) -> а..п (D0 B0..D0 BF): second byte + 0x20
//   Р..Я (D0 A0..D0 AF) -> р..я (D1 80..D1 8F): lead byte +1, second byte - 0x20
//   Ё (D0 81)           -> ё (D1 91)
std::string toLowerAsciiCyrillic(const std::string& text) {
    std::string out = text;
    for (std::size_t i = 0; i < out.size(); ++i) {
        char& c = out[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
            continue;
        }
        const auto lead = static_cast<unsigned char>(c);
        if (lead != 0xD0 || i + 1 >= out.size()) {
            continue;
        }
        auto second = static_cast<unsigned char>(out[i + 1]);
        if (second >= 0x90 && second <= 0x9F) {
            out[i + 1] = static_cast<char>(second + 0x20);
        } else if (second == 0x81) {  // Ё -> ё
            out[i] = static_cast<char>(0xD1);
            out[i + 1] = static_cast<char>(0x91);
        } else if (second >= 0xA0 && second <= 0xAF) {
            out[i] = static_cast<char>(0xD1);
            out[i + 1] = static_cast<char>(second - 0x20);
        }
        ++i;  // the continuation byte was already handled
    }
    return out;
}

// Any byte that can be part of a word: ASCII alphanumerics and every non-ASCII
// byte (Cyrillic letters are lead+continuation byte pairs, all >= 0x80). Used
// only for boundary checks, never for decoding, so partial UTF-8 sequences are
// handled conservatively: a match simply is not taken next to such a byte.
bool isWordByte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c >= 0x80;
}

bool hasWordByteBefore(const std::string& lower, std::size_t position) {
    return position > 0 && isWordByte(static_cast<unsigned char>(lower[position - 1]));
}

bool hasWordByteAfter(const std::string& lower, std::size_t end) {
    return end < lower.size() && isWordByte(static_cast<unsigned char>(lower[end]));
}

bool isClosingPunctuation(char c) {
    switch (c) {
        case ',':
        case '.':
        case ';':
        case ':':
        case '!':
        case '?':
        case ')':
        case ']':
        case '}':
            return true;
        default:
            return false;
    }
}

// Uppercases the single ASCII/Cyrillic letter starting at `i` in place. Every
// mapping is byte-length preserving, so indices stay valid. Returns true when a
// letter was actually cased.
bool upperInPlaceAt(std::string& out, std::size_t i) {
    if (i >= out.size()) {
        return false;
    }
    char& c = out[i];
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        return true;  // already uppercase
    }
    const auto lead = static_cast<unsigned char>(c);
    if (i + 1 >= out.size()) {
        return false;
    }
    const auto second = static_cast<unsigned char>(out[i + 1]);
    if (lead == 0xD0 && second >= 0xB0 && second <= 0xBF) {  // а..п -> А..П
        out[i + 1] = static_cast<char>(second - 0x20);
        return true;
    }
    if (lead == 0xD1 && second >= 0x80 && second <= 0x8F) {  // р..я -> Р..Я
        out[i] = static_cast<char>(0xD0);
        out[i + 1] = static_cast<char>(second + 0x20);
        return true;
    }
    if (lead == 0xD1 && second == 0x91) {  // ё -> Ё
        out[i] = static_cast<char>(0xD0);
        out[i + 1] = static_cast<char>(0x81);
        return true;
    }
    if (lead == 0xD0 && (second == 0x81 || (second >= 0x90 && second <= 0xAF))) {
        return true;  // already an uppercase Cyrillic letter
    }
    return false;
}

// Width in bytes of the sentence terminator at `i`, or 0 if there is none.
// "…" is U+2026 = E2 80 A6; everything else is single-byte ASCII.
std::size_t sentenceEndWidth(const std::string& text, std::size_t i) {
    const char c = text[i];
    if (c == '.' || c == '!' || c == '?') {
        return 1;
    }
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < text.size() &&
        static_cast<unsigned char>(text[i + 1]) == 0x80 &&
        static_cast<unsigned char>(text[i + 2]) == 0xA6) {
        return 3;
    }
    return 0;
}

// A terminator only ends a sentence when whitespace (or the end of the text)
// follows it. That single rule is what keeps "example.com", "script.py" and
// "localhost:3000/a.b" out of the capitalizer's way.
bool isSentenceEnd(const std::string& text, std::size_t i) {
    const std::size_t width = sentenceEndWidth(text, i);
    if (width == 0) {
        return false;
    }
    std::size_t after = i + width;
    // Runs like "?!" or "..." count as one terminator.
    while (after < text.size() && sentenceEndWidth(text, after) != 0) {
        after += sentenceEndWidth(text, after);
    }
    if (after >= text.size()) {
        return true;
    }
    const char next = text[after];
    return next == ' ' || next == '\t' || next == '\r' || next == '\n';
}

char closingBracketFor(char opening) {
    switch (opening) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '*': return '*';
        default: return '\0';
    }
}

// Russian boilerplate whisper models hallucinate on silence, noise or music.
// Matched case-insensitively as a prefix of a standalone sentence.
const std::vector<std::string>& hallucinationPrefixes() {
    static const std::vector<std::string> kPrefixes = {
        "субтитры сделал",
        "субтитры создавал",
        "субтитры делал",
        "редактор субтитров",
        "корректор субтитров",
        "продолжение следует",
        "спасибо за просмотр",
        "подписывайтесь на канал",
        "ставьте лайки",
        "субтитры и перевод",
        "перевод и субтитры",
        "dimatorzok",
    };
    return kPrefixes;
}

// Minimal reader for a flat JSON string->string object. Supports the standard
// escapes including \uXXXX. Anything else (nested objects, numbers, trailing
// garbage) throws and makes the caller fall back to the built-in defaults.
std::string parseJsonString(const std::string& json, std::size_t& cursor) {
    if (cursor >= json.size() || json[cursor] != '"') {
        throw std::invalid_argument("expected a string");
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
                unsigned int code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = json[cursor++];
                    code <<= 4;
                    if (h >= '0' && h <= '9') {
                        code |= static_cast<unsigned int>(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        code |= static_cast<unsigned int>(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        code |= static_cast<unsigned int>(h - 'A' + 10);
                    } else {
                        throw std::invalid_argument("bad \\u escape");
                    }
                }
                if (code <= 0x7F) {
                    out.push_back(static_cast<char>(code));
                } else if (code <= 0x7FF) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                break;
            }
            default:
                throw std::invalid_argument("bad escape");
        }
    }
    throw std::invalid_argument("unterminated string");
}

}  // namespace

PunctuationDictionary defaultPunctuationDictionary() {
    return {
        {"точка с запятой", ";"},
        {"вопросительный знак", "?"},
        {"восклицательный знак", "!"},
        {"открывающая скобка", "("},
        {"закрывающая скобка", ")"},
        {"открывающая кавычка", "«"},
        {"закрывающая кавычка", "»"},
        {"знак процента", "%"},
        {"знак равно", "="},
        {"знак плюс", "+"},
        {"косая черта", "/"},
        {"запятая", ","},
        {"двоеточие", ":"},
        {"многоточие", "…"},
        {"открытая скобка", "("},
        {"закрытая скобка", ")"},
        {"кавычки", "\""},
        {"тире", "—"},
        {"дефис", "-"},
        {"точка", "."},
    };
}

bool readPunctuationDictionary(const std::filesystem::path& path, PunctuationDictionary& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();

    try {
        std::size_t cursor = 0;
        auto skip = [&json](std::size_t& at) {
            while (at < json.size() &&
                   (json[at] == ' ' || json[at] == '\t' || json[at] == '\r' || json[at] == '\n')) {
                ++at;
            }
        };

        skip(cursor);
        if (cursor >= json.size() || json[cursor] != '{') {
            return false;
        }
        ++cursor;
        skip(cursor);

        PunctuationDictionary parsed;
        if (cursor < json.size() && json[cursor] == '}') {
            ++cursor;
        } else {
            for (;;) {
                skip(cursor);
                std::string key = parseJsonString(json, cursor);
                skip(cursor);
                if (cursor >= json.size() || json[cursor] != ':') {
                    return false;
                }
                ++cursor;
                skip(cursor);
                std::string value = parseJsonString(json, cursor);
                skip(cursor);

                if (!key.empty()) {
                    parsed.emplace_back(std::move(key), std::move(value));
                }

                if (cursor < json.size() && json[cursor] == ',') {
                    ++cursor;
                    continue;
                }
                if (cursor < json.size() && json[cursor] == '}') {
                    ++cursor;
                    break;
                }
                return false;
            }
        }

        skip(cursor);
        if (cursor != json.size()) {
            return false;  // trailing garbage after the closing brace
        }
        out = std::move(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

PunctuationDictionary loadPunctuationDictionary(
    const std::vector<std::filesystem::path>& candidatePaths) {
    for (const std::filesystem::path& path : candidatePaths) {
        if (path.empty()) {
            continue;
        }
        PunctuationDictionary dictionary;
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec) &&
            readPunctuationDictionary(path, dictionary) && !dictionary.empty()) {
            return dictionary;
        }
    }
    return defaultPunctuationDictionary();
}

std::string applyPunctuationDictionary(const std::string& text,
                                       const PunctuationDictionary& dictionary) {
    if (text.empty() || dictionary.empty()) {
        return text;
    }

    // Longest keys first so "точка с запятой" wins over "точка".
    PunctuationDictionary ordered = dictionary;
    std::sort(ordered.begin(), ordered.end(),
              [](const PunctuationDictionary::value_type& a,
                 const PunctuationDictionary::value_type& b) {
                  return a.first.size() > b.first.size();
              });

    const std::string lower = toLowerAsciiCyrillic(text);
    std::string out;
    out.reserve(text.size());

    std::size_t copied = 0;  // bytes of `text` already appended to `out`
    std::size_t scan = 0;    // position in `lower`/`text` the search continues from
    while (scan <= lower.size()) {
        // Find the leftmost whole-phrase match across all keys; ties are won by
        // the longest key ("точка с запятой" beats "точка" at the same spot).
        std::size_t bestStart = std::string::npos;
        std::size_t bestEnd = std::string::npos;
        const std::string* replacement = nullptr;

        for (const auto& entry : ordered) {
            const std::string& key = entry.first;
            if (key.empty() || key.size() > lower.size()) {
                continue;
            }
            std::size_t from = scan;
            for (;;) {
                const std::size_t position = lower.find(key, from);
                if (position == std::string::npos) {
                    break;  // this key has no further occurrence
                }
                const std::size_t end = position + key.size();
                if (!hasWordByteBefore(lower, position) && !hasWordByteAfter(lower, end)) {
                    // First passing occurrence of this key: the earliest it can match.
                    if (bestStart == std::string::npos || position < bestStart ||
                        (position == bestStart && key.size() > bestEnd - bestStart)) {
                        bestStart = position;
                        bestEnd = end;
                        replacement = &entry.second;
                    }
                    break;
                }
                from = position + 1;  // boundary hit: try the next occurrence
            }
        }

        if (replacement == nullptr) {
            break;  // nothing else matches from `scan` on
        }

        out.append(text, copied, bestStart - copied);
        out.append(*replacement);
        copied = bestEnd;
        scan = bestEnd;
    }

    out.append(text, copied, text.size() - copied);
    return out;
}

std::string stripNonSpeechAnnotations(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        const char opening = text[i];
        const char closing = closingBracketFor(opening);
        if (closing == '\0') {
            out.push_back(text[i++]);
            continue;
        }

        const std::size_t end = text.find(closing, i + 1);
        if (end == std::string::npos) {
            out.push_back(text[i++]);
            continue;
        }

        // Only drop short groups made of letters and spaces: anything with
        // digits, punctuation or code-like characters is real content.
        const std::string inner = text.substr(i + 1, end - i - 1);
        const bool empty = inner.empty();
        bool annotation = !empty && inner.size() <= 64;
        for (const char c : inner) {
            const auto byte = static_cast<unsigned char>(c);
            const bool letterish = byte >= 0x80 || (c >= 'a' && c <= 'z') ||
                                   (c >= 'A' && c <= 'Z') || c == ' ' || c == '-';
            if (!letterish) {
                annotation = false;
                break;
            }
        }
        if (!annotation) {
            out.push_back(text[i++]);
            continue;
        }

        // Swallow the group; a space is left behind so words do not glue
        // together. normalizeSpacing() collapses the leftovers afterwards.
        out.push_back(' ');
        i = end + 1;
    }
    return out;
}

std::string dropHallucinatedBoilerplate(const std::string& text) {
    // Split into sentences, keeping their terminators, then drop the ones that
    // are pure boilerplate. A sentence is only dropped when the whole sentence
    // starts with a known phrase, never in the middle of real speech.
    std::vector<std::string> sentences;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (!isSentenceEnd(text, i)) {
            continue;
        }
        std::size_t end = i + (static_cast<unsigned char>(text[i]) == 0xE2 ? 3 : 1);
        while (end < text.size() && isSentenceEnd(text, end)) {
            end += (static_cast<unsigned char>(text[end]) == 0xE2 ? 3 : 1);
        }
        sentences.push_back(text.substr(start, end - start));
        start = end;
        i = end - 1;
    }
    if (start < text.size()) {
        sentences.push_back(text.substr(start));
    }

    std::string out;
    out.reserve(text.size());
    for (const std::string& sentence : sentences) {
        const std::string lower = toLowerAsciiCyrillic(normalizeSpacing(sentence));
        bool boilerplate = false;
        for (const std::string& prefix : hallucinationPrefixes()) {
            if (lower.rfind(prefix, 0) == 0) {
                boilerplate = true;
                break;
            }
        }
        if (!boilerplate) {
            out.append(sentence);
        }
    }
    return out;
}

std::string capitalizeSentences(const std::string& text) {
    std::string out = text;
    bool atSentenceStart = true;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const char c = out[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '(' || c == '[') {
            continue;  // leading whitespace/quotes do not end the sentence start
        }
        if (isSentenceEnd(out, i)) {
            atSentenceStart = true;
            i += (static_cast<unsigned char>(c) == 0xE2) ? 2 : 0;
            continue;
        }
        if (atSentenceStart) {
            if (upperInPlaceAt(out, i)) {
                atSentenceStart = false;
            }
        } else {
            atSentenceStart = false;
        }
        // Skip the continuation byte of a two-byte Cyrillic letter.
        if (static_cast<unsigned char>(out[i]) >= 0xC0 && i + 1 < out.size()) {
            ++i;
        }
    }
    return out;
}

bool writePunctuationDictionary(const std::filesystem::path& path,
                                const PunctuationDictionary& dictionary, std::string& error) {
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "Could not create the dictionary directory: " + ec.message();
            return false;
        }
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "Could not open the dictionary file for writing: " + path.string();
        return false;
    }

    auto quote = [](const std::string& value) {
        std::string out = "\"";
        for (const char c : value) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out.push_back(c); break;
            }
        }
        out.push_back('"');
        return out;
    };

    file << "{\n";
    for (std::size_t i = 0; i < dictionary.size(); ++i) {
        file << "  " << quote(dictionary[i].first) << ": " << quote(dictionary[i].second)
             << (i + 1 < dictionary.size() ? ",\n" : "\n");
    }
    file << "}\n";

    if (!file.good()) {
        error = "Could not write the dictionary file: " + path.string();
        return false;
    }
    return true;
}

std::string normalizeSpacing(const std::string& text) {
    // 1. Trim both ends.
    const char* const kSpaces = " \t\r\n";
    const std::size_t first = text.find_first_not_of(kSpaces);
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(kSpaces);
    const std::string trimmed = text.substr(first, last - first + 1);

    // 2. Collapse runs of whitespace into a single space.
    std::string collapsed;
    collapsed.reserve(trimmed.size());
    bool pendingSpace = false;
    for (const char c : trimmed) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !collapsed.empty()) {
            collapsed.push_back(' ');
        }
        pendingSpace = false;
        collapsed.push_back(c);
    }

    // 3. Drop a space before closing punctuation and after an opening bracket
    //    or opening quote. Only spaces are removed: nothing is inserted and
    //    nothing is reordered, so URLs and code stay byte-identical.
    std::string out;
    out.reserve(collapsed.size());
    bool dropNextSpace = false;
    for (std::size_t i = 0; i < collapsed.size(); ++i) {
        const char c = collapsed[i];
        if (c == ' ' && dropNextSpace) {
            dropNextSpace = false;
            continue;
        }
        dropNextSpace = false;
        // "»" = C2 BB: the space belongs before the two-byte sequence, so the
        // check fires on the lead byte C2 while looking ahead at the next one.
        const bool closingGuillemet = static_cast<unsigned char>(c) == 0xC2 &&
                                      i + 1 < collapsed.size() &&
                                      static_cast<unsigned char>(collapsed[i + 1]) == 0xBB;
        if ((isClosingPunctuation(c) || closingGuillemet) && !out.empty() && out.back() == ' ') {
            out.pop_back();
        }
        out.push_back(c);
        if (c == '(' || c == '[' || c == '{') {
            dropNextSpace = true;
        } else if (static_cast<unsigned char>(c) == 0xAB &&  // "«" = C2 AB
                   !out.empty() && out.size() >= 2 &&
                   static_cast<unsigned char>(out[out.size() - 2]) == 0xC2) {
            dropNextSpace = true;
        }
    }
    return out;
}

std::string normalizeTranscript(const std::string& text,
                                const PunctuationDictionary& dictionary) {
    const std::string cleaned =
        dropHallucinatedBoilerplate(stripNonSpeechAnnotations(text));
    return capitalizeSentences(normalizeSpacing(applyPunctuationDictionary(cleaned, dictionary)));
}

}  // namespace whisperflow
