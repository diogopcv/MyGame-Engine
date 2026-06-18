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

private:
    void* impl_ = nullptr; ///< Opaque pointer to WindowImpl (window_win32.cpp).
};

} // namespace ge::platform
