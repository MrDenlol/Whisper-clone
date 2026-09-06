#include "PhraseHistory.h"
#include "test_framework.h"
#include "test_paths.h"

#include <filesystem>

WF_TEST(PhraseHistoryEncodesAndDecodes) {
    const std::vector<std::string> entries = {
        "hello",
        "line one\nline two",
        "quote \" and backslash \\",
    };
    const std::string json = whisperflow::PhraseHistory::toJson(entries);
    std::vector<std::string> decoded;
    WF_CHECK(whisperflow::PhraseHistory::parseJson(json, decoded));
    WF_CHECK_EQ(decoded.size(), entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        WF_CHECK_EQ(decoded[i], entries[i]);
    }
}

WF_TEST(PhraseHistoryRejectsNonArray) {
    std::vector<std::string> out{"keep?"};
    WF_CHECK(!whisperflow::PhraseHistory::parseJson("{}", out));
}

WF_TEST(PhraseHistoryConsecutiveDuplicateIsDropped) {
    whisperflow::PhraseHistory history(10);
    history.add("first");
    history.add("first");
    history.add("second");
    WF_CHECK_EQ(history.size(), std::size_t(2));
    WF_CHECK_EQ(history.at(0), std::string("first"));
    WF_CHECK_EQ(history.last(), std::string("second"));
}

WF_TEST(PhraseHistoryRespectsLimit) {
    whisperflow::PhraseHistory history(3);
    history.add("a");
    history.add("b");
    history.add("c");
    history.add("d");
    WF_CHECK_EQ(history.size(), std::size_t(3));
    WF_CHECK_EQ(history.at(0), std::string("b"));
    WF_CHECK_EQ(history.last(), std::string("d"));
}

WF_TEST(PhraseHistoryIgnoresEmptyPhrases) {
    whisperflow::PhraseHistory history(3);
    history.add("");
    history.add(" ");
    WF_CHECK_EQ(history.size(), std::size_t(0));
}

WF_TEST(PhraseHistoryLoadSaveRoundTrip) {
    const std::filesystem::path dir = wftest::scratchDirectory("phrase_history");
    const std::filesystem::path file = dir / "phrase_history.json";

    whisperflow::PhraseHistory history(10);
    WF_CHECK(history.load(file));  // missing file: sets the path and starts empty
    history.add("Привет");
    history.add("second\nline");
    std::string error;
    WF_CHECK(history.save(error));
    WF_CHECK_EQ(error, std::string());
    WF_CHECK(std::filesystem::is_regular_file(file));

    whisperflow::PhraseHistory loaded(10);
    WF_CHECK(loaded.load(file));
    WF_CHECK_EQ(loaded.size(), std::size_t(2));
    WF_CHECK_EQ(loaded.at(0), std::string("Привет"));
    WF_CHECK_EQ(loaded.at(1), std::string("second\nline"));
}

WF_TEST(PhraseHistoryMissingFileIsEmptyNotError) {
    const std::filesystem::path dir = wftest::scratchDirectory("phrase_history_missing");
    whisperflow::PhraseHistory history(10);
    WF_CHECK(history.load(dir / "does-not-exist.json"));
    WF_CHECK(!history.size());
}
