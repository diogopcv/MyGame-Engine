#include "platform/window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

namespace ge::platform {

static constexpr int kMaxKeys     = 256;
static constexpr int kMaxButtons  = 3;

struct WindowImpl {
    HWND          hwnd         = nullptr;
    std::uint32_t width        = 0;
    std::uint32_t height       = 0;
    bool          should_close = false;

    bool  keys[kMaxKeys]          = {};
    bool  mouse_buttons[kMaxButtons] = {};
    float mouse_x   = 0.0f;
    float mouse_y   = 0.0f;
    float mouse_dx  = 0.0f;
    float mouse_dy  = 0.0f;
    float pending_dx = 0.0f;
    float pending_dy = 0.0f;
    bool  mouse_captured = false;
};

namespace {

constexpr const char* kClassName = "GE_WindowClass";

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lparam);
        ::SetWindowLongPtrA(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcA(hwnd, msg, wparam, lparam);
    }

    auto* impl = reinterpret_cast<WindowImpl*>(::GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CLOSE:
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

    // -- Keyboard --
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (impl && wparam < kMaxKeys)
            impl->keys[wparam] = true;
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (impl && wparam < kMaxKeys)
            impl->keys[wparam] = false;
        return 0;

    // -- Mouse buttons --
    case WM_LBUTTONDOWN:
        if (impl) impl->mouse_buttons[0] = true;
        return 0;
    case WM_LBUTTONUP:
        if (impl) impl->mouse_buttons[0] = false;
        return 0;
    case WM_RBUTTONDOWN:
        if (impl) impl->mouse_buttons[1] = true;
        return 0;
    case WM_RBUTTONUP:
        if (impl) impl->mouse_buttons[1] = false;
        return 0;
    case WM_MBUTTONDOWN:
        if (impl) impl->mouse_buttons[2] = true;
        return 0;
    case WM_MBUTTONUP:
        if (impl) impl->mouse_buttons[2] = false;
        return 0;

    // -- Mouse movement --
    case WM_MOUSEMOVE:
        if (impl) {
            float new_x = static_cast<float>(GET_X_LPARAM(lparam));
            float new_y = static_cast<float>(GET_Y_LPARAM(lparam));
            if (!impl->mouse_captured) {
                impl->pending_dx += new_x - impl->mouse_x;
                impl->pending_dy += new_y - impl->mouse_y;
            }
            impl->mouse_x = new_x;
            impl->mouse_y = new_y;
        }
        return 0;

    case WM_INPUT:
        if (impl && impl->mouse_captured) {
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),
                              RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));
            if (raw.header.dwType == RIM_TYPEMOUSE) {
                impl->pending_dx += static_cast<float>(raw.data.mouse.lLastX);
                impl->pending_dy += static_cast<float>(raw.data.mouse.lLastY);
            }
        }
        return 0;
    }
    return ::DefWindowProcA(hwnd, msg, wparam, lparam);
}

} // anonymous namespace

bool Window::create(const WindowDesc& desc) {
    auto* impl = new WindowImpl();
    impl_ = impl;

    const HINSTANCE inst = ::GetModuleHandleA(nullptr);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = inst;
        wc.hCursor       = ::LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(DKGRAY_BRUSH));
        wc.lpszClassName = kClassName;
        if (!::RegisterClassExA(&wc)) {
            delete impl;
            impl_ = nullptr;
            return false;
        }
        registered = true;
    }

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect{ 0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height) };
    ::AdjustWindowRect(&rect, style, FALSE);

    HWND hwnd = ::CreateWindowExA(
        0, kClassName, desc.title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, inst,
        impl);

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
    if (impl->mouse_captured) set_mouse_capture(false);
    if (impl->hwnd) ::DestroyWindow(impl->hwnd);
    delete impl;
    impl_ = nullptr;
}

void Window::pump_events() {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    if (!impl) return;

    impl->pending_dx = 0.0f;
    impl->pending_dy = 0.0f;

    MSG msg;
    while (::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            impl->should_close = true;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageA(&msg);
    }

    impl->mouse_dx = impl->pending_dx;
    impl->mouse_dy = impl->pending_dy;

    if (impl->mouse_captured) {
        RECT rc;
        ::GetClientRect(impl->hwnd, &rc);
        POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
        ::ClientToScreen(impl->hwnd, &center);
        ::SetCursorPos(center.x, center.y);
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

bool Window::key_down(KeyCode key) const {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    if (!impl) return false;
    auto idx = static_cast<std::uint8_t>(key);
    return (idx < kMaxKeys) ? impl->keys[idx] : false;
}

bool Window::mouse_button_down(MouseButton btn) const {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    if (!impl) return false;
    auto idx = static_cast<std::uint8_t>(btn);
    return (idx < kMaxButtons) ? impl->mouse_buttons[idx] : false;
}

void Window::mouse_position(float& x, float& y) const {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    if (!impl) { x = 0; y = 0; return; }
    x = impl->mouse_x;
    y = impl->mouse_y;
}

void Window::mouse_delta(float& dx, float& dy) const {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    if (!impl) { dx = 0; dy = 0; return; }
    dx = impl->mouse_dx;
    dy = impl->mouse_dy;
}

void Window::set_mouse_capture(bool enable) {
    auto* impl = reinterpret_cast<WindowImpl*>(impl_);
    if (!impl || !impl->hwnd) return;

    if (enable && !impl->mouse_captured) {
        impl->mouse_captured = true;
        ::ShowCursor(FALSE);

        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
        rid.usUsage     = 0x02; // HID_USAGE_GENERIC_MOUSE
        rid.dwFlags     = RIDEV_INPUTSINK;
        rid.hwndTarget  = impl->hwnd;
        ::RegisterRawInputDevices(&rid, 1, sizeof(rid));

        RECT rc;
        ::GetClientRect(impl->hwnd, &rc);
        ::MapWindowPoints(impl->hwnd, nullptr, reinterpret_cast<POINT*>(&rc), 2);
        ::ClipCursor(&rc);
    } else if (!enable && impl->mouse_captured) {
        impl->mouse_captured = false;
        ::ShowCursor(TRUE);
        ::ClipCursor(nullptr);

        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage     = 0x02;
        rid.dwFlags     = RIDEV_REMOVE;
        rid.hwndTarget  = nullptr;
        ::RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }
}

} // namespace ge::platform
