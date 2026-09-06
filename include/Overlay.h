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

// A very small always-on-top pill. It uses WS_EX_NOACTIVATE / SW_SHOWNOACTIVATE so
// it never steals focus from the window the user is dictating into.
class Overlay {
public:
    Overlay();
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    bool create();
    void destroy();
    void show(const std::string& text);
    void hide();

    [[nodiscard]] bool isVisible() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

#endif  // _WIN32

}  // namespace whisperflow
