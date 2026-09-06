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
                std::string("Напиши, потом текст"));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("конец точка", defaults()),
                std::string("Конец."));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("первое точка с запятой второе", defaults()),
                std::string("Первое; второе"));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("внимание вопросительный знак", defaults()),
                std::string("Внимание?"));
}

WF_TEST(Normalizer_dictionaryIsCaseInsensitiveAndWholeWord) {
    WF_CHECK_EQ(whisperflow::normalizeTranscript("сделай Запятая здесь", defaults()),
                std::string("Сделай, здесь"));
    // "точка" inside another word must not be replaced.
    WF_CHECK_EQ(whisperflow::normalizeTranscript("проточка деталей", defaults()),
                std::string("Проточка деталей"));
    // "опять" contains no key, but "ять"-like partials must stay untouched too.
    WF_CHECK_EQ(whisperflow::normalizeTranscript("интерпретация точки", defaults()),
                std::string("Интерпретация точки"));
}

WF_TEST(Normalizer_readsCustomDictionaryFile) {
    const std::filesystem::path dir = wftest::scratchDirectory("dictionary");
    const std::filesystem::path file = dir / "dictionary.json";
    WF_CHECK(wftest::writeFile(file, "{\n  \"ёлка\": \"е\",\n  \"галочка\": \"OK\"\n}\n"));

    whisperflow::PunctuationDictionary dictionary;
    WF_CHECK(whisperflow::readPunctuationDictionary(file, dictionary));
    WF_CHECK_EQ(dictionary.size(), std::size_t(2));

    WF_CHECK_EQ(whisperflow::normalizeTranscript("ёжик Ёлка Галочка", dictionary),
                std::string("Ёжик е OK"));
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

WF_TEST(Normalizer_dropsNonSpeechAnnotations) {
    WF_CHECK_EQ(whisperflow::normalizeTranscript("(кашель) Привет, как дела? (аплодисменты)",
                                                 defaults()),
                std::string("Привет, как дела?"));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("[музыка] Начинаем работу", defaults()),
                std::string("Начинаем работу"));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("*смех* всё хорошо", defaults()),
                std::string("Всё хорошо"));
}

WF_TEST(Normalizer_keepsRealParenthesesAndCode) {
    // Digits and punctuation inside the brackets mean real content, not an
    // annotation: it must survive.
    WF_CHECK_EQ(whisperflow::stripNonSpeechAnnotations("смотри (см. пункт 3) внимательно"),
                std::string("смотри (см. пункт 3) внимательно"));
    WF_CHECK_EQ(whisperflow::stripNonSpeechAnnotations("вызови sum(a, b)"),
                std::string("вызови sum(a, b)"));
    WF_CHECK_EQ(whisperflow::stripNonSpeechAnnotations("массив arr[0] пуст"),
                std::string("массив arr[0] пуст"));
}

WF_TEST(Normalizer_dropsHallucinatedBoilerplate) {
    WF_CHECK_EQ(whisperflow::normalizeTranscript("Субтитры сделал DimaTorzok", defaults()),
                std::string());
    WF_CHECK_EQ(whisperflow::normalizeTranscript("Привет. Продолжение следует...", defaults()),
                std::string("Привет."));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("Спасибо за просмотр!", defaults()),
                std::string());
    // Real speech that merely contains a similar word is never dropped.
    WF_CHECK_EQ(whisperflow::normalizeTranscript("включи субтитры на видео", defaults()),
                std::string("Включи субтитры на видео"));
}

WF_TEST(Normalizer_capitalizesSentences) {
    WF_CHECK_EQ(whisperflow::capitalizeSentences("привет. как дела? всё хорошо!"),
                std::string("Привет. Как дела? Всё хорошо!"));
    WF_CHECK_EQ(whisperflow::capitalizeSentences("ёжик спит"), std::string("Ёжик спит"));
    WF_CHECK_EQ(whisperflow::capitalizeSentences("hello world. bye"),
                std::string("Hello world. Bye"));
    // Already capitalized text is left alone, byte for byte.
    const std::string done = "Привет, мир. Ещё раз.";
    WF_CHECK_EQ(whisperflow::capitalizeSentences(done), done);
}

WF_TEST(Normalizer_capitalizationNeverBreaksUrlsOrCode) {
    // A URL/command mid-sentence must not be uppercased.
    WF_CHECK_EQ(whisperflow::normalizeTranscript("открой https://example.com/docs?lang=ru",
                                                 defaults()),
                std::string("Открой https://example.com/docs?lang=ru"));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("запусти python3 script.py", defaults()),
                std::string("Запусти python3 script.py"));
}

WF_TEST(Normalizer_dictionaryRoundTripsThroughDisk) {
    const std::filesystem::path dir = wftest::scratchDirectory("dictionary-write");
    const std::filesystem::path file = dir / "dictionary.json";

    std::string error;
    WF_CHECK(whisperflow::writePunctuationDictionary(file, defaults(), error));
    WF_CHECK(error.empty());

    whisperflow::PunctuationDictionary loaded;
    WF_CHECK(whisperflow::readPunctuationDictionary(file, loaded));
    WF_CHECK_EQ(loaded.size(), defaults().size());
    WF_CHECK_EQ(whisperflow::normalizeTranscript("конец точка", loaded), std::string("Конец."));
}

WF_TEST(Normalizer_extendedDictionaryEntries) {
    // Brackets and quotes dictated in the newer wording.
    WF_CHECK_EQ(whisperflow::normalizeTranscript(
                    "смотри открывающая скобка важно закрывающая скобка дальше", defaults()),
                std::string("Смотри (важно) дальше"));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("рост знак процента за год", defaults()),
                std::string("Рост % за год"));
    WF_CHECK_EQ(whisperflow::normalizeTranscript("путь косая черта дальше", defaults()),
                std::string("Путь / дальше"));
    // "процент"/"плюс" on their own are ordinary words and must survive.
    WF_CHECK_EQ(whisperflow::normalizeTranscript("это большой плюс проекта", defaults()),
                std::string("Это большой плюс проекта"));
}

WF_TEST(Normalizer_guillemetsHugTheirContent) {
    WF_CHECK_EQ(whisperflow::normalizeTranscript(
                    "он сказал открывающая кавычка привет закрывающая кавычка", defaults()),
                std::string("Он сказал «привет»"));
    // The spacing pass alone must be idempotent and lossless here.
    const std::string done = "Он сказал «привет» и ушёл.";
    WF_CHECK_EQ(whisperflow::normalizeSpacing(done), done);
    // Brackets and URLs still survive untouched.
    const std::string url = "открой (https://example.com/docs?lang=ru) сейчас";
    WF_CHECK_EQ(whisperflow::normalizeSpacing(url), url);
}
