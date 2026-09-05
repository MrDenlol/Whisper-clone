#include "HotkeySpec.h"

#include <string>

#include "test_framework.h"

WF_TEST(HotkeySpec_parsesDefaultCombination) {
    const auto result = whisperflow::parseHotkey(whisperflow::kDefaultHotkey);
    WF_CHECK(result.error.empty());
    WF_CHECK(result.spec.has_value());
    WF_CHECK(result.spec->ctrl);
    WF_CHECK(result.spec->shift);
    WF_CHECK(!result.spec->alt);
    WF_CHECK(!result.spec->win);
    WF_CHECK_EQ(result.spec->key, std::string("space"));
}

WF_TEST(HotkeySpec_isCaseAndSpaceInsensitive) {
    const auto result = whisperflow::parseHotkey(" Ctrl + WIN + Space ");
    WF_CHECK(result.error.empty());
    WF_CHECK(result.spec.has_value());
    WF_CHECK(result.spec->ctrl);
    WF_CHECK(result.spec->win);
    WF_CHECK(!result.spec->shift);
    WF_CHECK_EQ(result.spec->key, std::string("space"));
}

WF_TEST(HotkeySpec_acceptsBareFunctionKey) {
    const auto result = whisperflow::parseHotkey("f9");
    WF_CHECK(result.error.empty());
    WF_CHECK(result.spec.has_value());
    WF_CHECK(!result.spec->ctrl);
    WF_CHECK_EQ(result.spec->key, std::string("f9"));
}

WF_TEST(HotkeySpec_rejectsBareLetter) {
    // A bare letter would make typing that letter impossible, so it must be refused.
    const auto result = whisperflow::parseHotkey("q");
    WF_CHECK(!result.spec.has_value());
    WF_CHECK(result.error.find("modifier") != std::string::npos);
}

WF_TEST(HotkeySpec_rejectsUnknownKey) {
    const auto result = whisperflow::parseHotkey("ctrl+hyper");
    WF_CHECK(!result.spec.has_value());
    WF_CHECK(result.error.find("hyper") != std::string::npos);
}

WF_TEST(HotkeySpec_rejectsEmptyAndMalformedInput) {
    WF_CHECK(!whisperflow::parseHotkey("").spec.has_value());
    WF_CHECK(!whisperflow::parseHotkey("ctrl+").spec.has_value());
    WF_CHECK(!whisperflow::parseHotkey("ctrl").spec.has_value());
    WF_CHECK(!whisperflow::parseHotkey("ctrl+space+space").spec.has_value());
}

WF_TEST(HotkeySpec_rejectsFunctionKeyOutOfRange) {
    WF_CHECK(whisperflow::parseHotkey("f1").spec.has_value());
    WF_CHECK(whisperflow::parseHotkey("f24").spec.has_value());
    WF_CHECK(!whisperflow::parseHotkey("f0").spec.has_value());
    WF_CHECK(!whisperflow::parseHotkey("f25").spec.has_value());
}

WF_TEST(HotkeySpec_roundTripsThroughToString) {
    const auto result = whisperflow::parseHotkey("ctrl+alt+shift+tab");
    WF_CHECK(result.spec.has_value());
    WF_CHECK_EQ(result.spec->toString(), std::string("Ctrl+Alt+Shift+tab"));

    const auto reparsed = whisperflow::parseHotkey(result.spec->toString());
    WF_CHECK(reparsed.spec.has_value());
    WF_CHECK(reparsed.spec->ctrl);
    WF_CHECK(reparsed.spec->alt);
    WF_CHECK(reparsed.spec->shift);
    WF_CHECK_EQ(reparsed.spec->key, std::string("tab"));
}

WF_TEST(HotkeySpec_keyNameRecognition) {
    WF_CHECK(whisperflow::isKnownKeyName("space"));
    WF_CHECK(whisperflow::isKnownKeyName("escape"));
    WF_CHECK(whisperflow::isKnownKeyName("z"));
    WF_CHECK(whisperflow::isKnownKeyName("7"));
    WF_CHECK(whisperflow::isKnownKeyName("f13"));
    WF_CHECK(!whisperflow::isKnownKeyName(""));
    WF_CHECK(!whisperflow::isKnownKeyName("hyper"));
    WF_CHECK(whisperflow::isFunctionKeyName("f13"));
    WF_CHECK(!whisperflow::isFunctionKeyName("space"));
}
