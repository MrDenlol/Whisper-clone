#include "Hotkey.h"

#if defined(_WIN32)

#include <atomic>
#include <cstdio>
#include <string>
#include <utility>

namespace whisperflow {
namespace {

constexpr int kHotkeyId = 0xC0FF;
constexpr UINT_PTR kReleaseTimerId = 1;
constexpr UINT kReleasePollMs = 20;

const wchar_t* const kWindowClassName = L"WhisperFlowClone.HotkeyWindow";

bool isKeyDown(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

std::string keyName(unsigned int key) {
    if (key == VK_SPACE) {
        return "Space";
    }
    if (key == VK_RETURN) {
        return "Enter";
    }
    if (key == VK_TAB) {
        return "Tab";
    }
    if (key >= static_cast<unsigned int>('A') && key <= static_cast<unsigned int>('Z')) {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= static_cast<unsigned int>('0') && key <= static_cast<unsigned int>('9')) {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= static_cast<unsigned int>(VK_F1) && key <= static_cast<unsigned int>(VK_F24)) {
        return "F" + std::to_string(key - static_cast<unsigned int>(VK_F1) + 1u);
    }
    char buffer[16] = {0};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "VK=0x%02X", key);
    return buffer;
}

}  // namespace

std::optional<HotkeyCombination> toCombination(const HotkeySpec& spec) {
    unsigned int virtualKey = 0;
    const std::string& key = spec.key;

    if (key == "space") {
        virtualKey = VK_SPACE;
    } else if (key == "enter" || key == "return") {
        virtualKey = VK_RETURN;
    } else if (key == "tab") {
        virtualKey = VK_TAB;
    } else if (key == "esc" || key == "escape") {
        virtualKey = VK_ESCAPE;
    } else if (key == "backspace") {
        virtualKey = VK_BACK;
    } else if (isFunctionKeyName(key)) {
        virtualKey = static_cast<unsigned int>(VK_F1) +
                     static_cast<unsigned int>(std::stoi(key.substr(1)) - 1);
    } else if (key.size() == 1) {
        const char c = key[0];
        // VK_A..VK_Z equal 'A'..'Z' and VK_0..VK_9 equal '0'..'9'.
        virtualKey = (c >= 'a' && c <= 'z') ? static_cast<unsigned int>('A' + (c - 'a'))
                                            : static_cast<unsigned int>(c);
    } else {
        return std::nullopt;
    }

    HotkeyCombination combination;
    combination.key = virtualKey;
    if (spec.ctrl) {
        combination.modifiers |= MOD_CONTROL;
    }
    if (spec.alt) {
        combination.modifiers |= MOD_ALT;
    }
    if (spec.shift) {
        combination.modifiers |= MOD_SHIFT;
    }
    if (spec.win) {
        combination.modifiers |= MOD_WIN;
    }
    return combination;
}

std::string describeHotkey(const HotkeyCombination& combination) {
    std::string text;
    if ((combination.modifiers & MOD_CONTROL) != 0) {
        text += "Ctrl+";
    }
    if ((combination.modifiers & MOD_WIN) != 0) {
        text += "Win+";
    }
    if ((combination.modifiers & MOD_ALT) != 0) {
        text += "Alt+";
    }
    if ((combination.modifiers & MOD_SHIFT) != 0) {
        text += "Shift+";
    }
    text += keyName(combination.key);
    return text;
}

class HotkeyManager::Impl {
public:
    Impl() = default;

    ~Impl() {
        unregister();
        if (hwnd_ != nullptr) {
            // The window is created on the thread that called registerPushToTalk;
            // the application destroys this object on the same thread.
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void setErrorHandler(ErrorHandler handler) {
        onError_ = std::move(handler);
    }

    bool registerPushToTalk(const HotkeyCombination& combination, PressHandler onPress,
                            ReleaseHandler onRelease) {
        if (registered_) {
            return true;
        }

        if (!createWindow()) {
            return false;
        }

        onPress_ = std::move(onPress);
        onRelease_ = std::move(onRelease);
        combination_ = combination;

        if (RegisterHotKey(hwnd_, kHotkeyId, static_cast<UINT>(combination.modifiers),
                           static_cast<UINT>(combination.key)) == 0) {
            lastError_ = "RegisterHotKey failed for " + describeHotkey(combination) +
                         " (error " + std::to_string(GetLastError()) +
                         "). The combination is probably taken by another application.";
            reportError();
            return false;
        }

        registered_ = true;
        return true;
    }

    void unregister() {
        if (hwnd_ != nullptr && timerId_ != 0) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        if (registered_ && hwnd_ != nullptr) {
            UnregisterHotKey(hwnd_, kHotkeyId);
            registered_ = false;
        }
    }

    void runMessageLoop() {
        loopThreadId_.store(GetCurrentThreadId());

        MSG message{};
        BOOL result = GetMessageW(&message, nullptr, 0, 0);
        while (result > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            result = GetMessageW(&message, nullptr, 0, 0);
        }
        // result == 0 (WM_QUIT) or -1 (error): leave the loop either way.
    }

    void stopMessageLoop() {
        const DWORD threadId = loopThreadId_.load();
        if (threadId != 0) {
            PostThreadMessageW(threadId, WM_QUIT, 0, 0);
        }
    }

    bool isRegistered() const noexcept {
        return registered_;
    }

    const std::string& lastError() const noexcept {
        return lastError_;
    }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        Impl* self = nullptr;
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self != nullptr) {
            if (message == WM_HOTKEY) {
                self->handleHotkeyDown();
                return 0;
            }
            if (message == WM_TIMER && wParam == static_cast<WPARAM>(kReleaseTimerId)) {
                self->pollForRelease();
                return 0;
            }
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool createWindow() {
        if (hwnd_ != nullptr) {
            return true;
        }

        HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW existing{};
        existing.cbSize = sizeof(existing);
        if (GetClassInfoExW(instance, kWindowClassName, &existing) == 0) {
            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &Impl::windowProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kWindowClassName;
            if (RegisterClassExW(&windowClass) == 0) {
                lastError_ = "RegisterClassExW failed (error " + std::to_string(GetLastError()) + ")";
                reportError();
                return false;
            }
        }

        hwnd_ = CreateWindowExW(0, kWindowClassName, L"WhisperFlowClone", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, instance, this);
        if (hwnd_ == nullptr) {
            lastError_ = "CreateWindowExW failed (error " + std::to_string(GetLastError()) + ")";
            reportError();
            return false;
        }
        return true;
    }

    void handleHotkeyDown() {
        if (timerId_ != 0) {
            return;  // already tracking this press
        }

        if (onPress_) {
            onPress_();
        }

        // RegisterHotKey only reports the press; poll until the user lets go.
        timerId_ = SetTimer(hwnd_, kReleaseTimerId, kReleasePollMs, nullptr);
    }

    void pollForRelease() {
        bool held = isKeyDown(static_cast<int>(combination_.key));

        if ((combination_.modifiers & MOD_CONTROL) != 0) {
            held = held && isKeyDown(VK_CONTROL);
        }
        if ((combination_.modifiers & MOD_WIN) != 0) {
            held = held && (isKeyDown(VK_LWIN) || isKeyDown(VK_RWIN));
        }
        if ((combination_.modifiers & MOD_ALT) != 0) {
            held = held && isKeyDown(VK_MENU);
        }
        if ((combination_.modifiers & MOD_SHIFT) != 0) {
            held = held && isKeyDown(VK_SHIFT);
        }

        if (held) {
            return;
        }

        if (timerId_ != 0) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        if (onRelease_) {
            onRelease_();
        }
    }

    void reportError() {
        if (onError_ && !lastError_.empty()) {
            onError_(lastError_);
        }
    }

    HWND hwnd_{nullptr};
    UINT_PTR timerId_{0};
    bool registered_{false};
    HotkeyCombination combination_{};
    PressHandler onPress_;
    ReleaseHandler onRelease_;
    ErrorHandler onError_;
    std::string lastError_;
    std::atomic<DWORD> loopThreadId_{0};
};

HotkeyManager::HotkeyManager() : pImpl_(std::make_unique<Impl>()) {}
HotkeyManager::~HotkeyManager() = default;
HotkeyManager::HotkeyManager(HotkeyManager&&) noexcept = default;
HotkeyManager& HotkeyManager::operator=(HotkeyManager&&) noexcept = default;

void HotkeyManager::setErrorHandler(ErrorHandler handler) {
    pImpl_->setErrorHandler(std::move(handler));
}

bool HotkeyManager::registerPushToTalk(const HotkeyCombination& combination, PressHandler onPress,
                                       ReleaseHandler onRelease) {
    return pImpl_->registerPushToTalk(combination, std::move(onPress), std::move(onRelease));
}

void HotkeyManager::unregister() {
    pImpl_->unregister();
}

void HotkeyManager::runMessageLoop() {
    pImpl_->runMessageLoop();
}

void HotkeyManager::stopMessageLoop() {
    pImpl_->stopMessageLoop();
}

bool HotkeyManager::isRegistered() const noexcept {
    return pImpl_->isRegistered();
}

const std::string& HotkeyManager::lastError() const noexcept {
    return pImpl_->lastError();
}

}  // namespace whisperflow

#endif  // _WIN32
