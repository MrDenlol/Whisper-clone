#include <filesystem>
#include <string>

#include "TextNormalizer.h"
#include "test_framework.h"
#include "test_paths.h"

namespace {

whisperflow::PunctuationDictionary defaults() {
    return whisperflow::defaultPunctuationDictionary();
}

}  // namespace

WF_TEST(Normalizer_trimsAndCollapsesWhitespace) {
    WF_CHECK_EQ(whisperflow::normalizeSpacing("  Привет   мир  "), std::string("Привет мир"));
    WF_CHECK_EQ(whisperflow::normalizeSpacing("\t строка \t\t вторая \n"), std::string("строка вторая"));
    WF_CHECK_EQ(whisperflow::normalizeSpacing("     "), std::string());
    WF_CHECK_EQ(whisperflow::normalizeSpacing(""), std::string());
}

WF_TEST(Normalizer_removesSpaceBeforeClosingPunctuation) {
    WF_CHECK_EQ(whisperflow::normalizeSpacing("Привет , как дела ?"),
                std::string("Привет, как дела?"));
    WF_CHECK_EQ(whisperflow::normalizeSpacing("значение ; конец : всё !"),
                std::string("значение; конец: всё!"));
    WF_CHECK_EQ(whisperflow::normalizeSpacing("вызов (аргумент ) ] }"),
                std::string("вызов (аргумент)]}"));
}

WF_TEST(Normalizer_neverBreaksUrlsPathsAndCode) {
    // No insertion, no reordering: mixed text must survive byte-identical.
    const std::string url = "открой https://example.com/docs?lang=ru и localhost:3000";
    WF_CHECK_EQ(whisperflow::normalizeSpacing(url), url);

    const std::string code = "python3 script.py --lang=ru";
    WF_CHECK_EQ(whisperflow::normalizeSpacing(code), code);

    const std::string cmd = "git commit -m \"сообщение\"";
    WF_CHECK_EQ(whisperflow::normalizeSpacing(cmd), cmd);
}

WF_TEST(Normalizer_replacesSpokenPunctuation) {
    WF_CHECK_EQ(whisperflow::normalizeTranscript("напиши запятая потом текст", defaults()),
                std::string("напиши, потом текст"));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("конец точка", defaults()),
                std::string("конец."));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("первое точка с запятой второе", defaults()),
                std::string("первое; второе"));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("внимание вопросительный знак", defaults()),
                std::string("внимание?"));
}

WF_TEST(Normalizer_dictionaryIsCaseInsensitiveAndWholeWord) {
    WF_CHECK_EQ(whisperflow::normalizeTranscript("сделай Запятая здесь", defaults()),
                std::string("сделай, здесь"));
    // "точка" inside another word must not be replaced.
    WF_CHECK_EQ(whisperflow::normalizeTranscript("проточка деталей", defaults()),
                std::string("проточка деталей"));
    // "опять" contains no key, but "ять"-like partials must stay untouched too.
    WF_CHECK_EQ(whisperflow::normalizeTranscript("интерпретация точки", defaults()),
                std::string("интерпретация точки"));
}

WF_TEST(Normalizer_readsCustomDictionaryFile) {
    const std::filesystem::path dir = wftest::scratchDirectory("dictionary");
    const std::filesystem::path file = dir / "dictionary.json";
    WF_CHECK(wftest::writeFile(file, "{\n  \"ёлка\": \"е\",\n  \"галочка\": \"OK\"\n}\n"));

    whisperflow::PunctuationDictionary dictionary;
    WF_CHECK(whisperflow::readPunctuationDictionary(file, dictionary));
    WF_CHECK_EQ(dictionary.size(), std::size_t(2));

    WF_CHECK_EQ(whisperflow::normalizeTranscript("ёжик Ёлка Галочка", dictionary),
                std::string("ёжик е OK"));
}

WF_TEST(Normalizer_brokenDictionaryFileIsRejected) {
    const std::filesystem::path dir = wftest::scratchDirectory("dictionary-broken");
    whisperflow::PunctuationDictionary dictionary;

    std::filesystem::path file = dir / "not-object.json";
    WF_CHECK(wftest::writeFile(file, "[\"запятая\", \",\"]"));
    WF_CHECK(!whisperflow::readPunctuationDictionary(file, dictionary));

    file = dir / "truncated.json";
    WF_CHECK(wftest::writeFile(file, "{\"запятая\": \",\""));
    WF_CHECK(!whisperflow::readPunctuationDictionary(file, dictionary));

    file = dir / "trailing-garbage.json";
    WF_CHECK(wftest::writeFile(file, "{\"запятая\": \",\"} ой"));
    WF_CHECK(!whisperflow::readPunctuationDictionary(file, dictionary));

    WF_CHECK(!whisperflow::readPunctuationDictionary(dir / "missing.json", dictionary));
}

WF_TEST(Normalizer_missingFilesFallBackToDefaults) {
    const whisperflow::PunctuationDictionary dictionary = whisperflow::loadPunctuationDictionary(
        {std::filesystem::temp_directory_path() / "whisperflow-tests" / "no-such-dir" /
         "dictionary.json"});
    WF_CHECK(!dictionary.empty());
    bool hasComma = false;
    for (const auto& entry : dictionary) {
        if (entry.first == "запятая" && entry.second == ",") {
            hasComma = true;
        }
    }
    WF_CHECK(hasComma);
}
