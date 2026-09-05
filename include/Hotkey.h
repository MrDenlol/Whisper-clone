#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "HotkeySpec.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace whisperflow {

// A global key combination. On Windows these are RegisterHotKey values
// (MOD_CONTROL | MOD_WIN | MOD_ALT | MOD_SHIFT and a virtual key code).
struct HotkeyCombination {
    unsigned int modifiers{0};
    unsigned int key{0};
};

#if defined(_WIN32)
// Maps the platform-independent description onto RegisterHotKey values.
// Returns nullopt when the key name has no Windows virtual key code here.
[[nodiscard]] std::optional<HotkeyCombination> toCombination(const HotkeySpec& spec);

[[nodiscard]] std::string describeHotkey(const HotkeyCombination& combination);

// Registers a system-wide hotkey and reports press/release through std::function
// callbacks. No inheritance, no interface: composition only.
class HotkeyManager {
public:
    using PressHandler = std::function<void()>;
    using ReleaseHandler = std::function<void()>;
    using ErrorHandler = std::function<void(const std::string& message)>;

    HotkeyManager();
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;
    HotkeyManager(HotkeyManager&&) noexcept;
    HotkeyManager& operator=(HotkeyManager&&) noexcept;

    void setErrorHandler(ErrorHandler handler);

    // Creates the hidden window that receives WM_HOTKEY and registers the
    // combination. Both callbacks run on the message loop thread.
    bool registerPushToTalk(const HotkeyCombination& combination, PressHandler onPress,
                            ReleaseHandler onRelease);
    void unregister();

    // Blocks until stopMessageLoop() is called. Must run on the registering thread.
    void runMessageLoop();
    void stopMessageLoop();

    [[nodiscard]] bool isRegistered() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};
#endif  // _WIN32

}  // namespace whisperflow
