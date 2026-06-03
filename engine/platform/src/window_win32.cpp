#include "platform/window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ge::platform {

// All Win32 state lives here, in the .cpp. The header only knows about a
// void* pointing at one of these.
struct WindowImpl {
    HWND          hwnd         = nullptr;
    std::uint32_t width        = 0;
    std::uint32_t height       = 0;
    bool          should_close = false;
};

namespace {

constexpr const char* kClassName = "GE_WindowClass";

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // Stash the WindowImpl* we passed to CreateWindowEx so later messages can
    // reach it via GWLP_USERDATA.
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lparam);
        ::SetWindowLongPtrA(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcA(hwnd, msg, wparam, lparam);
    }

    auto* impl = reinterpret_cast<WindowImpl*>(::GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CLOSE:
        // Don't destroy here — just flag it. The app loop notices, exits, and
        // calls destroy() so teardown order stays under our control.
        if (impl) impl->should_close = true;
        return 0;
    case WM_SIZE:
        if (impl) {
            impl->width  = LOWORD(lparam);
            impl->height = HIWORD(lparam);
        }
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return ::DefWindowProcA(hwnd, msg, wparam, lparam);
}

} // anonymous namespace

bool Window::create(const WindowDesc& desc) {
    auto* impl = new WindowImpl();
    impl_ = impl;

    const HINSTANCE inst = ::GetModuleHandleA(nullptr);

    // Register the window class once for the whole process.
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = inst;
        wc.hCursor       = ::LoadCursorA(nullptr, IDC_ARROW);
        // Dark-gray background brush: this is our stand-in "clear" until the
        // DX12 swapchain takes over the back buffer in Phase 1.
        wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(DKGRAY_BRUSH));
        wc.lpszClassName = kClassName;
        if (!::RegisterClassExA(&wc)) {
            delete impl;
            impl_ = nullptr;
            return false;
        }
        registered = true;
    }

    // Grow the outer window so the *client* area matches the requested size.
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect{ 0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height) };
    ::AdjustWindowRect(&rect, style, FALSE);

    HWND hwnd = ::CreateWindowExA(
        0, kClassName, desc.title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, inst,
        impl /* lpCreateParams -> arrives in WM_NCCREATE */);

    if (!hwnd) {
        delete impl;
        impl_ = nullptr;
        return false;
    }

    impl->hwnd   = hwnd;
    impl->width  = desc.width;
    impl->height = desc.height;

    ::ShowWindow(hwnd, SW_SHOW);
    return true;
}

void Window::destroy() {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    if (!impl) return;
    if (impl->hwnd) ::DestroyWindow(impl->hwnd);
    delete impl;
    impl_ = nullptr;
}

void Window::pump_events() {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    MSG msg;
    while (::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT && impl) {
            impl->should_close = true;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageA(&msg);
    }
}

bool Window::should_close() const {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    return impl ? impl->should_close : true;
}

void* Window::native_handle() const {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    return impl ? impl->hwnd : nullptr;
}

std::uint32_t Window::width() const {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    return impl ? impl->width : 0;
}

std::uint32_t Window::height() const {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    return impl ? impl->height : 0;
}

} // namespace ge::platform
