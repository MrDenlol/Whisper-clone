#include "Overlay.h"

#include <string>
#include <utility>

namespace whisperflow {
#if defined(_WIN32)

namespace {
const wchar_t* const kOverlayClassName = L"WhisperFlowClone.OverlayWindow";
constexpr int kTextPadding = 18;
constexpr int kCornerRadius = 14;

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
}

class Overlay::Impl {
public:
    ~Impl() {
        destroy();
    }

    bool create() {
        if (hwnd_ != nullptr) {
            return true;
        }

        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW existing{};
        existing.cbSize = sizeof(existing);
        if (GetClassInfoExW(instance, kOverlayClassName, &existing) == 0) {
            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &Impl::windowProc;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            windowClass.lpszClassName = kOverlayClassName;
            if (RegisterClassExW(&windowClass) == 0) {
                return false;
            }
        }

        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                                kOverlayClassName, L"WhisperFlowClone", WS_POPUP, 0, 0, 1, 1,
                                nullptr, nullptr, instance, this);
        if (hwnd_ == nullptr) {
            return false;
        }
        SetLayeredWindowAttributes(hwnd_, 0, 232, LWA_ALPHA);
        return true;
    }

    void destroy() {
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void show(const std::string& text) {
        if (!create()) {
            return;
        }
        text_ = text;
        measure();
        SetWindowPos(hwnd_, HWND_TOPMOST, x_, y_, width_, height_,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        InvalidateRect(hwnd_, nullptr, FALSE);
        visible_ = true;
    }

    void hide() {
        if (hwnd_ != nullptr) {
            ShowWindow(hwnd_, SW_HIDE);
        }
        visible_ = false;
    }

    bool isVisible() const noexcept {
        return visible_;
    }

private:
    void measure() {
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int taskbarHeight = GetSystemMetrics(SM_CYSCREEN) - GetSystemMetrics(SM_CYFULLSCREEN);

        HDC dc = GetDC(nullptr);
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HGDIOBJ old = SelectObject(dc, font);
        const std::wstring wide = toWide(text_);
        RECT rect{};
        DrawTextW(dc, wide.c_str(), -1, &rect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);

        width_ = rect.right - rect.left + kTextPadding * 2;
        height_ = rect.bottom - rect.top + kTextPadding;
        if (width_ < 120) {
            width_ = 120;
        }
        if (height_ < 34) {
            height_ = 34;
        }
        x_ = (screenWidth - width_) / 2;
        y_ = screenHeight - taskbarHeight - height_ - 14;
    }

    void paint() {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);

        HPEN border = CreatePen(PS_SOLID, 1, RGB(72, 72, 72));
        HBRUSH fill = CreateSolidBrush(RGB(32, 32, 32));
        HGDIOBJ oldPen = SelectObject(dc, border);
        HGDIOBJ oldBrush = SelectObject(dc, fill);
        RoundRect(dc, 1, 1, width_ - 1, height_ - 1, kCornerRadius * 2, kCornerRadius * 2);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(fill);
        DeleteObject(border);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HGDIOBJ oldFont = SelectObject(dc, font);
        RECT textRect = rectForText();
        const std::string text = text_;
        DrawTextA(dc, text.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldFont);

        EndPaint(hwnd_, &ps);
    }

    RECT rectForText() const {
        RECT rect{0, 0, width_, height_};
        return rect;
    }

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
            if (message == WM_PAINT) {
                self->paint();
                return 0;
            }
            if (message == WM_ERASEBKGND) {
                return 1;
            }
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HWND hwnd_{nullptr};
    std::string text_;
    int x_{0};
    int y_{0};
    int width_{120};
    int height_{40};
    bool visible_{false};
};

Overlay::Overlay() : pImpl_(std::make_unique<Impl>()) {}
Overlay::~Overlay() = default;

bool Overlay::create() {
    return pImpl_->create();
}

void Overlay::destroy() {
    pImpl_->destroy();
}

void Overlay::show(const std::string& text) {
    pImpl_->show(text);
}

void Overlay::hide() {
    pImpl_->hide();
}

bool Overlay::isVisible() const noexcept {
    return pImpl_->isVisible();
}

#endif  // _WIN32

}  // namespace whisperflow
