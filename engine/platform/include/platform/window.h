#pragma once
#include <cstdint>

/**
 * @file window.h
 * @brief OS window and message pump (Layer 0 — Platform).
 *
 * The HWND and all `<windows.h>` types are hidden inside the .cpp behind an
 * opaque impl pointer (pImpl), so this header — and everyone who includes it —
 * stays platform-clean.
 */
namespace ge::platform {

/**
 * @brief Descriptor for creating an OS window.
 */
struct WindowDesc {
    const char*   title  = "Engine";   ///< Window title bar text.
    std::uint32_t width  = 1280;       ///< Initial client-area width in pixels.
    std::uint32_t height = 720;        ///< Initial client-area height in pixels.
};

/**
 * @brief Virtual key codes for keyboard input polling.
 *
 * Values intentionally match VK_* constants so the Win32 backend is a
 * direct lookup (no translation table).
 */
enum class KeyCode : std::uint8_t {
    W         = 0x57,
    A         = 0x41,
    S         = 0x53,
    D         = 0x44,
    Q         = 0x51,
    E         = 0x45,
    Space     = 0x20,
    LShift    = 0xA0,
    Escape    = 0x1B,
};

/**
 * @brief Mouse button identifiers.
 */
enum class MouseButton : std::uint8_t {
    Left   = 0,
    Right  = 1,
    Middle = 2,
};

/**
 * @brief Platform window abstraction with pImpl-hidden OS handles.
 */
class Window {
public:
    /**
     * @brief Create and show the OS window.
     * @param desc Window creation parameters.
     * @return `true` on success, `false` if the window could not be created.
     */
    bool create(const WindowDesc& desc);

    /** @brief Destroy the OS window and release all resources. */
    void destroy();

    /** @brief Drain all pending OS messages. Call once per frame. */
    void pump_events();

    /**
     * @brief Check whether a close was requested.
     * @return `true` after the user clicks the close button (WM_CLOSE).
     */
    bool should_close() const;

    /**
     * @brief Retrieve the native window handle.
     * @return The HWND cast to `void*`. The graphics layer casts it back to
     *         create the DX12 swap chain. Keeping it `void*` avoids leaking
     *         `<windows.h>` into higher layers.
     */
    void* native_handle() const;

    /** @brief Current client-area width in pixels. */
    std::uint32_t width()  const;
    /** @brief Current client-area height in pixels. */
    std::uint32_t height() const;

    // -- Input polling -------------------------------------------------------

    /**
     * @brief Test whether a key is currently held down.
     * @param key The key to test.
     * @return `true` if the key is pressed this frame.
     */
    bool key_down(KeyCode key) const;

    /**
     * @brief Test whether a mouse button is currently held down.
     * @param btn The button to test.
     * @return `true` if the button is pressed this frame.
     */
    bool mouse_button_down(MouseButton btn) const;

    /**
     * @brief Current mouse cursor position in client-area pixels.
     * @param[out] x Horizontal position (left = 0).
     * @param[out] y Vertical position (top = 0).
     */
    void mouse_position(float& x, float& y) const;

    /**
     * @brief Mouse movement delta since last pump_events() call.
     * @param[out] dx Horizontal delta in pixels.
     * @param[out] dy Vertical delta in pixels.
     */
    void mouse_delta(float& dx, float& dy) const;

    /**
     * @brief Enable or disable raw mouse capture for FPS-style camera control.
     *
     * When enabled, the cursor is hidden, confined to the window, and
     * mouse_delta() returns raw movement. Disabled by default.
     *
     * @param enable `true` to capture, `false` to release.
     */
    void set_mouse_capture(bool enable);

private:
    void* impl_ = nullptr; ///< Opaque pointer to WindowImpl (window_win32.cpp).
};

} // namespace ge::platform
