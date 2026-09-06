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

// Drops whisper's non-speech annotations - "(кашель)", "[музыка]",
// "*аплодисменты*" - which the decoder still emits now and then despite the
// prompt and suppress_nst. Only bracketed groups that contain no sentence
// punctuation and are made of letters/spaces are removed, so "(см. пункт 3)"
// and code like "sum(a, b)" survive untouched.
[[nodiscard]] std::string stripNonSpeechAnnotations(const std::string& text);

// Removes whole "hallucinated" boilerplate lines Russian whisper models like to
// invent on silence or noise ("Субтитры сделал DimaTorzok", "Продолжение
// следует...", "Редактор субтитров ..."). The comparison is case-insensitive
// and only fires when such a phrase is the entire transcript or a standalone
// trailing sentence, so real speech is never truncated.
[[nodiscard]] std::string dropHallucinatedBoilerplate(const std::string& text);

// Uppercases the first letter of the transcript and of every sentence that
// follows ". ", "! ", "? " or "… ". Byte-length preserving for ASCII and
// Cyrillic, so nothing is inserted or reordered.
[[nodiscard]] std::string capitalizeSentences(const std::string& text);

// Whitespace-only cleanup before the text is pasted into an editor:
//   - trims the ends,
//   - collapses runs of spaces/tabs/newlines into one space,
//   - drops a space before closing punctuation , . ; : ! ? ) ] }
// It never inserts characters and never reorders anything, therefore
// "python3 script.py", "https://example.com/docs?lang=ru" and
// "localhost:3000" pass through byte-identical.
[[nodiscard]] std::string normalizeSpacing(const std::string& text);

// The full pre-insertion pipeline, in this order:
//   1. drop non-speech annotations and hallucinated boilerplate lines,
//   2. replace spoken punctuation from the dictionary,
//   3. normalize spacing,
//   4. capitalize sentences.
[[nodiscard]] std::string normalizeTranscript(const std::string& text,
                                              const PunctuationDictionary& dictionary);

// Writes a dictionary back as pretty, UTF-8 JSON so the tray "Edit dictionary"
// action can create the file the user is about to edit. Returns false and fills
// `error` on I/O failure.
[[nodiscard]] bool writePunctuationDictionary(const std::filesystem::path& path,
                                              const PunctuationDictionary& dictionary,
                                              std::string& error);

}  // namespace whisperflow
