#pragma once

#include <functional>
#include <memory>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace whisperflow {

#if defined(_WIN32)

enum class TrayCommand {
    RepeatLast,
    LanguageAuto,
    LanguageRu,
    LanguageEn,
    LanguageOther,
    ModelTiny,
    ModelBase,
    ModelSmall,
    ModelMedium,
    OpenModelsFolder,
    OpenSettingsFile,
    ToggleAutostart,
    Exit,
};

inline constexpr UINT kTrayCallbackMessage = WM_APP + 0x100;

// Shell_NotifyIcon wrapper. The icons menu is built with owner-draw handling by
// the app's message loop through HotkeyManager::setUserMessageHandler.
class TrayIcon {
public:
    using CommandHandler = std::function<void(TrayCommand)>;

    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool create(HWND owner, const std::string& tooltip);
    void destroy();

    void setCommandHandler(CommandHandler handler);
    void setStatus(const std::string& status);
    void setAutostartEnabled(bool enabled);
    void setLanguage(const std::string& language);
    void setModel(const std::string& model);
    void showBalloon(const std::string& title, const std::string& message);

    // Called by the owner's message handler for tray callback / WM_COMMAND messages.
    bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] HWND owner() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

#endif  // _WIN32

}  // namespace whisperflow
