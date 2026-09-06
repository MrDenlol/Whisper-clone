#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace whisperflow {

// Ordered "spoken phrase -> written symbol" pairs, e.g. "запятая" -> ",".
// An ordered vector (not a map) matters: longer phrases such as
// "точка с запятой" must be tried before their single-word prefix "точка".
using PunctuationDictionary = std::vector<std::pair<std::string, std::string>>;

// Built-in Russian spoken-punctuation phrases. Used when dictionary.json is
// missing or corrupt so the app never loses the feature.
[[nodiscard]] PunctuationDictionary defaultPunctuationDictionary();

// Parses a flat JSON object {"spoken phrase": "replacement", ...}. Returns
// false if the file cannot be opened or is not a valid flat string->string
// object; `out` is left untouched in that case.
[[nodiscard]] bool readPunctuationDictionary(const std::filesystem::path& path,
                                             PunctuationDictionary& out);

// Tries every candidate path in order and returns the first readable
// dictionary. If no file can be read the built-in defaults are returned:
// a missing/broken dictionary.json must never break dictation.
[[nodiscard]] PunctuationDictionary loadPunctuationDictionary(
    const std::vector<std::filesystem::path>& candidatePaths);

// Replaces spoken punctuation phrases with the real symbols, longest phrase
// first, case-insensitive, whole word/phrase matches only (so "точка" inside
// "двухточечный" is never touched). Replacement is byte-exact and never
// reorders characters, which keeps URLs and code fragments intact.
[[nodiscard]] std::string applyPunctuationDictionary(
    const std::string& text, const PunctuationDictionary& dictionary);

// Whitespace-only cleanup before the text is pasted into an editor:
//   - trims the ends,
//   - collapses runs of spaces/tabs/newlines into one space,
//   - drops a space before closing punctuation , . ; : ! ? ) ] }
// It never inserts characters and never reorders anything, therefore
// "python3 script.py", "https://example.com/docs?lang=ru" and
// "localhost:3000" pass through byte-identical.
[[nodiscard]] std::string normalizeSpacing(const std::string& text);

// The full pre-insertion pipeline: spoken punctuation first, then spacing.
[[nodiscard]] std::string normalizeTranscript(const std::string& text,
                                              const PunctuationDictionary& dictionary);

}  // namespace whisperflow
