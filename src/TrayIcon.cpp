#include "TrayIcon.h"

#if defined(_WIN32)
#include <shellapi.h>
#endif

#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace whisperflow {
#if defined(_WIN32)

namespace {

constexpr UINT_PTR kCommandIdStatus = 1000;
constexpr UINT_PTR kCommandIdRepeat = 1001;
constexpr UINT_PTR kCommandIdLanguageAuto = 1002;
constexpr UINT_PTR kCommandIdLanguageRu = 1003;
constexpr UINT_PTR kCommandIdLanguageEn = 1004;
constexpr UINT_PTR kCommandIdLanguageOther = 1005;
constexpr UINT_PTR kCommandIdModelTiny = 1006;
constexpr UINT_PTR kCommandIdModelBase = 1007;
constexpr UINT_PTR kCommandIdModelSmall = 1008;
constexpr UINT_PTR kCommandIdModelMedium = 1009;
constexpr UINT_PTR kCommandIdOpenModels = 1010;
constexpr UINT_PTR kCommandIdOpenSettings = 1011;
constexpr UINT_PTR kCommandIdAutostart = 1012;
constexpr UINT_PTR kCommandIdExit = 1013;

std::wstring toWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

void copyTruncated(wchar_t* destination, std::size_t capacity, const std::wstring& text) {
    if (capacity == 0) {
        return;
    }
    std::size_t count = text.size();
    if (count >= capacity) {
        count = capacity - 1;
    }
    if (count > 0) {
        std::memcpy(destination, text.data(), count * sizeof(wchar_t));
    }
    destination[count] = L'\0';
}

bool isCheckedCommand(UINT_PTR id, const std::string& language, const std::string& model) {
    if (id == kCommandIdLanguageAuto) {
        return language == "auto";
    }
    if (id == kCommandIdLanguageRu) {
        return language == "ru";
    }
    if (id == kCommandIdLanguageEn) {
        return language == "en";
    }
    if (id == kCommandIdLanguageOther) {
        return language != "auto" && language != "ru" && language != "en";
    }
    if (id == kCommandIdModelTiny) {
        return model == "tiny";
    }
    if (id == kCommandIdModelBase) {
        return model == "base";
    }
    if (id == kCommandIdModelSmall) {
        return model == "small";
    }
    if (id == kCommandIdModelMedium) {
        return model == "medium";
    }
    return false;
}

std::optional<TrayCommand> commandForId(UINT id) {
    switch (id) {
        case kCommandIdRepeat: return TrayCommand::RepeatLast;
        case kCommandIdLanguageAuto: return TrayCommand::LanguageAuto;
        case kCommandIdLanguageRu: return TrayCommand::LanguageRu;
        case kCommandIdLanguageEn: return TrayCommand::LanguageEn;
        case kCommandIdLanguageOther: return TrayCommand::LanguageOther;
        case kCommandIdModelTiny: return TrayCommand::ModelTiny;
        case kCommandIdModelBase: return TrayCommand::ModelBase;
        case kCommandIdModelSmall: return TrayCommand::ModelSmall;
        case kCommandIdModelMedium: return TrayCommand::ModelMedium;
        case kCommandIdOpenModels: return TrayCommand::OpenModelsFolder;
        case kCommandIdOpenSettings: return TrayCommand::OpenSettingsFile;
        case kCommandIdAutostart: return TrayCommand::ToggleAutostart;
        case kCommandIdExit: return TrayCommand::Exit;
        default: break;
    }
    return std::nullopt;
}

void appendItem(HMENU menu, UINT_PTR id, const std::string& text, bool checked = false,
                bool enabled = true, bool separator = false) {
    if (separator) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        return;
    }
    UINT flags = MF_STRING;
    if (!enabled) {
        flags |= MF_GRAYED;
    }
    if (checked) {
        flags |= MF_CHECKED;
    }
    const std::wstring wide = toWide(text);
    AppendMenuW(menu, flags, static_cast<UINT_PTR>(id), wide.c_str());
}

}  // namespace

class TrayIcon::Impl {
public:
    ~Impl() {
        destroy();
    }

    bool create(HWND owner, const std::string& tooltip) {
        owner_ = owner;
        tooltip_ = tooltip;
        const HINSTANCE instance = GetModuleHandleW(nullptr);

        HICON icon = nullptr;
        if (const HMODULE module = GetModuleHandleW(nullptr)) {
            icon = LoadIconW(module, MAKEINTRESOURCEW(101));
        }
        if (icon == nullptr) {
            // IDI_APPLICATION/text macros are ANSI (LPSTR); LoadIconW needs LPCWSTR.
            icon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
        }
        icon_ = icon;

        memset(&data_, 0, sizeof(data_));
        data_.cbSize = sizeof(data_);
        data_.hWnd = owner;
        data_.uID = 1;
        data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data_.uCallbackMessage = kTrayCallbackMessage;
        data_.hIcon = icon_;
        setTooltip();

        created_ = (Shell_NotifyIconW(NIM_ADD, &data_) != FALSE);
        if (!created_) {
            return false;
        }
        updateMenu();
        return true;
    }

    void destroy() {
        if (created_) {
            Shell_NotifyIconW(NIM_DELETE, &data_);
            created_ = false;
        }
        if (menu_ != nullptr) {
            DestroyMenu(menu_);
            menu_ = nullptr;
        }
    }

    void setCommandHandler(CommandHandler handler) {
        onCommand_ = std::move(handler);
    }

    void setStatus(const std::string& status) {
        status_ = status;
        tooltip_ = "WhisperFlowClone - " + status;
        setTooltip();
        Shell_NotifyIconW(NIM_MODIFY, &data_);
        updateMenu();
    }

    void setAutostartEnabled(bool enabled) {
        autostartEnabled_ = enabled;
        updateMenu();
    }

    void setLanguage(const std::string& language) {
        language_ = language;
        updateMenu();
    }

    void setModel(const std::string& model) {
        model_ = model;
        updateMenu();
    }

    void showBalloon(const std::string& title, const std::string& message) {
        if (!created_) {
            return;
        }
        NOTIFYICONDATAW balloon = data_;
        balloon.uFlags |= NIF_INFO;
        balloon.dwInfoFlags = NIIF_INFO;
        const std::wstring titleWide = toWide(title);
        const std::wstring messageWide = toWide(message);
        copyTruncated(balloon.szInfoTitle, ARRAYSIZE(balloon.szInfoTitle), titleWide);
        copyTruncated(balloon.szInfo, ARRAYSIZE(balloon.szInfo), messageWide);
        balloon.uTimeout = 4000;
        Shell_NotifyIconW(NIM_MODIFY, &balloon);
    }

    bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == kTrayCallbackMessage) {
            switch (LOWORD(lParam)) {
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    showMenu();
                    return true;
                case WM_LBUTTONUP:
                    showMenu();
                    return true;
                default:
                    break;
            }
            return true;
        }
        if (message == WM_COMMAND) {
            const UINT id = static_cast<UINT>(LOWORD(wParam));
            if (const auto command = commandForId(id)) {
                if (onCommand_) {
                    onCommand_(*command);
                }
            }
            return true;
        }
        return false;
    }

    HWND owner() const noexcept {
        return owner_;
    }

private:
    void setTooltip() {
        const std::wstring wide = toWide(tooltip_);
        copyTruncated(data_.szTip, ARRAYSIZE(data_.szTip), wide);
    }

    void updateMenu() {
        if (menu_ != nullptr) {
            DestroyMenu(menu_);
            menu_ = nullptr;
        }
        menu_ = CreatePopupMenu();
        appendItem(menu_, kCommandIdStatus, "Status: " + status_, false, false);
        appendItem(menu_, 0, "", false, true, true);
        appendItem(menu_, kCommandIdRepeat, "Repeat last insertion");
        appendItem(menu_, 0, "", false, true, true);
        appendItem(menu_, kCommandIdLanguageAuto, "Language: auto",
                   isCheckedCommand(kCommandIdLanguageAuto, language_, model_));
        appendItem(menu_, kCommandIdLanguageRu, "Language: ru",
                   isCheckedCommand(kCommandIdLanguageRu, language_, model_));
        appendItem(menu_, kCommandIdLanguageEn, "Language: en",
                   isCheckedCommand(kCommandIdLanguageEn, language_, model_));
        appendItem(menu_, kCommandIdLanguageOther, "Language: other (set in settings.json)",
                   isCheckedCommand(kCommandIdLanguageOther, language_, model_), false);
        appendItem(menu_, 0, "", false, true, true);
        appendItem(menu_, kCommandIdModelTiny, "Model: tiny",
                   isCheckedCommand(kCommandIdModelTiny, language_, model_));
        appendItem(menu_, kCommandIdModelBase, "Model: base",
                   isCheckedCommand(kCommandIdModelBase, language_, model_));
        appendItem(menu_, kCommandIdModelSmall, "Model: small",
                   isCheckedCommand(kCommandIdModelSmall, language_, model_));
        appendItem(menu_, kCommandIdModelMedium, "Model: medium",
                   isCheckedCommand(kCommandIdModelMedium, language_, model_));
        appendItem(menu_, 0, "", false, true, true);
        appendItem(menu_, kCommandIdOpenModels, "Open models folder");
        appendItem(menu_, kCommandIdOpenSettings, "Open settings file");
        appendItem(menu_, kCommandIdAutostart, "Start with Windows", autostartEnabled_);
        appendItem(menu_, 0, "", false, true, true);
        appendItem(menu_, kCommandIdExit, "Exit");
    }

    void showMenu() {
        if (menu_ != nullptr) {
            DestroyMenu(menu_);
            menu_ = nullptr;
        }
        updateMenu();

        POINT cursor{};
        GetCursorPos(&cursor);
        SetForegroundWindow(owner_);
        TrackPopupMenu(menu_, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x,
                       cursor.y, 0, owner_, nullptr);
        PostMessageW(owner_, WM_NULL, 0, 0);
        if (menu_ != nullptr) {
            DestroyMenu(menu_);
            menu_ = nullptr;
        }
    }

    HWND owner_{nullptr};
    HICON icon_{nullptr};
    std::string status_{"waiting"};
    std::string tooltip_;
    std::string language_{"auto"};
    std::string model_{"small"};
    bool autostartEnabled_{false};
    bool created_{false};
    NOTIFYICONDATAW data_{};
    HMENU menu_{nullptr};
    CommandHandler onCommand_;
};

TrayIcon::TrayIcon() : pImpl_(std::make_unique<Impl>()) {}
TrayIcon::~TrayIcon() = default;

bool TrayIcon::create(HWND owner, const std::string& tooltip) {
    return pImpl_->create(owner, tooltip);
}

void TrayIcon::destroy() {
    pImpl_->destroy();
}

void TrayIcon::setCommandHandler(CommandHandler handler) {
    pImpl_->setCommandHandler(std::move(handler));
}

void TrayIcon::setStatus(const std::string& status) {
    pImpl_->setStatus(status);
}

void TrayIcon::setAutostartEnabled(bool enabled) {
    pImpl_->setAutostartEnabled(enabled);
}

void TrayIcon::setLanguage(const std::string& language) {
    pImpl_->setLanguage(language);
}

void TrayIcon::setModel(const std::string& model) {
    pImpl_->setModel(model);
}

void TrayIcon::showBalloon(const std::string& title, const std::string& message) {
    pImpl_->showBalloon(title, message);
}

bool TrayIcon::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    return pImpl_->handleMessage(message, wParam, lParam);
}

HWND TrayIcon::owner() const noexcept {
    return pImpl_->owner();
}

#endif  // _WIN32

}  // namespace whisperflow
