#pragma once
#include <cstdint>

// An OS window plus its message pump. The HWND and all <windows.h> types are
// hidden inside the .cpp behind an opaque impl pointer, so this header — and
// everyone who includes it — stays platform-clean.
namespace ge::platform {

struct WindowDesc {
    const char*   title  = "Engine";
    std::uint32_t width  = 1280;
    std::uint32_t height = 720;
};

class Window {
public:
    bool create(const WindowDesc& desc);
    void destroy();

    // Drain all pending OS messages. Call once per frame.
    void pump_events();

    // True after the user clicks the close button (WM_CLOSE).
    bool should_close() const;

    // Returns the HWND as a void*. The graphics layer will cast this back in
    // Phase 1 to create the DX12 swapchain. Keeping it void* avoids leaking
    // <windows.h> into higher layers.
    void* native_handle() const;

    std::uint32_t width()  const;
    std::uint32_t height() const;

private:
    void* impl_ = nullptr; // -> WindowImpl, defined in window_win32.cpp
};

} // namespace ge::platform
