#include "TextInjector.h"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace whisperflow {
namespace {

#if defined(_WIN32)

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

std::string lastErrorText(const char* what) {
    return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")";
}

bool readClipboardText(std::wstring& outText) {
    if (OpenClipboard(nullptr) == 0) {
        return false;
    }

    bool ok = false;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle != nullptr) {
        const void* pointer = GlobalLock(handle);
        if (pointer != nullptr) {
            outText.assign(static_cast<const wchar_t*>(pointer));
            GlobalUnlock(handle);
            ok = true;
        }
    }

    CloseClipboard();
    return ok;
}

bool setClipboardText(const std::wstring& text, std::string& outError) {
    if (OpenClipboard(nullptr) == 0) {
        outError = lastErrorText("OpenClipboard");
        return false;
    }

    bool ok = false;
    if (EmptyClipboard() == 0) {
        outError = lastErrorText("EmptyClipboard");
    } else {
        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (handle == nullptr) {
            outError = lastErrorText("GlobalAlloc");
        } else {
            void* pointer = GlobalLock(handle);
            if (pointer == nullptr) {
                GlobalFree(handle);
                outError = lastErrorText("GlobalLock");
            } else {
                std::memcpy(pointer, text.c_str(), bytes);
                GlobalUnlock(handle);
                if (SetClipboardData(CF_UNICODETEXT, handle) != nullptr) {
                    ok = true;
                } else {
                    GlobalFree(handle);
                    outError = lastErrorText("SetClipboardData");
                }
            }
        }
    }

    CloseClipboard();
    return ok;
}

bool sendCtrlV(std::string& outError) {
    INPUT inputs[4] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    const UINT sent = SendInput(4, inputs, sizeof(INPUT));
    if (sent != 4u) {
        outError = "SendInput delivered " + std::to_string(sent) + " of 4 events";
        return false;
    }
    return true;
}

#endif  // _WIN32

}  // namespace

bool TextInjector::inject([[maybe_unused]] const std::string& utf8Text, const ReportHandler& onReport) {
    InjectionReport report;

#if defined(_WIN32)
    if (utf8Text.empty()) {
        report.message = "nothing to paste: the transcript is empty";
        if (onReport) {
            onReport(report);
        }
        return false;
    }

    std::wstring previous;
    const bool hadPrevious = readClipboardText(previous);

    std::string error;
    if (!setClipboardText(toWide(utf8Text), error)) {
        report.message = "clipboard is not available: " + error;
        if (onReport) {
            onReport(report);
        }
        return false;
    }
    report.textLeftInClipboard = true;

    Sleep(pasteDelayMs_);

    if (!sendCtrlV(error)) {
        report.pasted = false;
        report.message = "paste failed (" + error +
                         "). The text was left on the clipboard - press Ctrl+V manually.";
        if (onReport) {
            onReport(report);
        }
        return false;
    }

    report.pasted = true;

    // Give the target window time to read the clipboard before we overwrite it.
    Sleep(restoreDelayMs_);

    if (hadPrevious && !previous.empty()) {
        if (setClipboardText(previous, error)) {
            report.clipboardRestored = true;
            report.textLeftInClipboard = false;
            report.message = "pasted, previous clipboard content restored";
        } else {
            report.message = "pasted, but the previous clipboard content could not be restored: " + error;
        }
    } else {
        report.message = "pasted (the clipboard was empty before)";
    }
#else
    report.message = "text injection is only implemented on Windows";
#endif

    if (onReport) {
        onReport(report);
    }
    return report.pasted;
}

}  // namespace whisperflow
