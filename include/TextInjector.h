#pragma once

#include <functional>
#include <string>

namespace whisperflow {

// What happened during one paste attempt.
struct InjectionReport {
    bool pasted{false};               // Ctrl+V was delivered to the focused window
    bool clipboardRestored{false};    // the user's previous clipboard text is back
    bool textLeftInClipboard{false};  // the transcript is still on the clipboard
    std::string message;              // human readable, safe to log
};

// Inserts text into whatever window currently has focus:
// clipboard -> Ctrl+V via SendInput -> restore the previous clipboard text.
// No inheritance, results are reported through a std::function.
class TextInjector {
public:
    using ReportHandler = std::function<void(const InjectionReport& report)>;

    TextInjector() = default;

    // How long to wait between filling the clipboard and sending Ctrl+V, and
    // between the paste and restoring the previous clipboard content.
    void setPasteDelayMs(unsigned long milliseconds) noexcept { pasteDelayMs_ = milliseconds; }
    void setRestoreDelayMs(unsigned long milliseconds) noexcept { restoreDelayMs_ = milliseconds; }

    bool inject(const std::string& utf8Text, const ReportHandler& onReport);

private:
    unsigned long pasteDelayMs_{40};
    unsigned long restoreDelayMs_{250};
};

}  // namespace whisperflow
