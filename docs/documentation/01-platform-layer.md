# Platform Layer (Layer 0)

The platform layer is the engine's lowest level. It provides a thin abstraction over the operating system: window creation, timing, virtual memory, file I/O, and input. Everything above depends on it, but it depends on nothing — not even the core layer.

## Table of Contents

- [Design Principles](#design-principles)
- [Virtual Memory (`memory.h`)](#virtual-memory-memoryh)
  - [What is Virtual Memory?](#what-is-virtual-memory)
  - [API](#api)
  - [Implementation: VirtualAlloc](#implementation-virtualalloc)
- [High-Resolution Timer (`timer.h`)](#high-resolution-timer-timerh)
  - [Why Not `std::chrono`?](#why-not-stdchrono)
  - [API](#api-1)
  - [Implementation: QueryPerformanceCounter](#implementation-queryperformancecounter)
- [Window and Message Pump (`window.h`)](#window-and-message-pump-windowh)
  - [The Win32 Window Model](#the-win32-window-model)
  - [API](#api-2)
  - [The pImpl Pattern in Practice](#the-pimpl-pattern-in-practice)
  - [Input Handling](#input-handling)
  - [Raw Mouse Input](#raw-mouse-input)
- [File I/O (`file.h`)](#file-io-fileh)
  - [API](#api-3)
  - [Implementation: Win32 File APIs](#implementation-win32-file-apis)

---

## Design Principles

The platform layer has three strict rules:

1. **No dependency on the engine.** It uses only standard library types (`std::size_t`, `std::uint32_t`) and OS APIs. It never includes headers from `engine/core/` or any other engine module. This is the foundation of the layering discipline.

2. **No OS headers in public APIs.** `<windows.h>` is included only inside `_win32.cpp` files. Public headers expose OS handles as `void*`. This keeps the API clean and makes future porting possible.

3. **Minimal abstraction.** Each platform service is a thin wrapper over the OS API. The goal is to expose the OS capability, not to invent a general abstraction. The platform layer makes the OS usable; it does not try to be the OS.

---

## Virtual Memory (`memory.h`)

### What is Virtual Memory?

Every modern operating system gives each process a large, flat address space called "virtual memory." This address space does not correspond directly to physical RAM. Instead, the OS uses a page table to map virtual addresses to physical memory pages (typically 4 KB each) on demand.

When you call the OS virtual memory API (like `VirtualAlloc` on Windows), you are asking the OS to:

1. **Reserve** a range of virtual addresses (no physical memory is committed yet).
2. **Commit** pages within that range (physical memory is assigned, zero-initialized).

This is different from `malloc` in several ways:

| Feature | `malloc` (CRT heap) | `VirtualAlloc` |
|---------|---------------------|----------------|
| Granularity | Any size | Page-aligned (4 KB) |
| Overhead | Per-allocation metadata | None (OS tracks pages) |
| Speed | Fast for small allocs | Relatively slow (system call) |
| Fragmentation | Can fragment over time | None (each reservation is contiguous) |
| Zero-initialization | Not guaranteed | Always zero-initialized |

The engine uses `VirtualAlloc` as its foundation because the allocators above (arena, pool, stack) manage their own sub-allocation. They need a large contiguous block from the OS and then hand out pieces of it with zero overhead. `VirtualAlloc` gives them exactly that.

### API

```cpp
namespace ge::platform {
    void*       mem_alloc(std::size_t size);  // Reserve + commit
    void        mem_free(void* ptr, std::size_t size);  // Decommit + release
    std::size_t page_size();  // Query the OS page size (typically 4096)
}
```

- **`mem_alloc(size)`** reserves and commits `size` bytes of virtual memory. The returned pointer is page-aligned and zero-initialized. Returns `nullptr` on failure.
- **`mem_free(ptr, size)`** releases the memory back to the OS. The `size` parameter is kept for portability (some platforms need it for decommitting).
- **`page_size()`** returns the system page size. Useful for aligning allocations to page boundaries.

### Implementation: VirtualAlloc

The Win32 implementation (`memory_win32.cpp`) calls:

```cpp
void* mem_alloc(std::size_t size) {
    return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void mem_free(void* ptr, std::size_t size) {
    VirtualFree(ptr, 0, MEM_RELEASE);
}

std::size_t page_size() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}
```

The `MEM_RESERVE | MEM_COMMIT` flags tell the OS to both reserve the address range and back it with physical pages in a single call. For the engine's allocators (which grab a large block up front and sub-allocate from it), this is the right behavior.

---

## High-Resolution Timer (`timer.h`)

### Why Not `std::chrono`?

`std::chrono::high_resolution_clock` is portable, but its resolution and behavior vary between implementations. On MSVC, it typically uses `QueryPerformanceCounter` internally, but wrapping it through the `std::chrono` type system adds overhead and complexity for no benefit when the engine already targets Windows exclusively.

Using `QueryPerformanceCounter` directly gives:

- **Sub-microsecond precision** (typically 10 MHz or higher on modern systems).
- **Monotonic** — never goes backward, unlike wall-clock time.
- **Minimal overhead** — a single RDTSC instruction on most modern CPUs.

### API

```cpp
namespace ge::platform {
    void   time_init();      // Must be called once at startup
    double now_seconds();    // Seconds elapsed since time_init()
}
```

- **`time_init()`** caches the performance counter frequency (`QueryPerformanceFrequency`) and records the reference time. Must be called once before any `now_seconds()` call.
- **`now_seconds()`** returns the elapsed time since `time_init()` as a `double` for sub-microsecond precision.

### Implementation: QueryPerformanceCounter

The Windows high-resolution timer works as follows:

1. **`QueryPerformanceFrequency(&freq)`** returns the number of ticks per second. This value is constant for the lifetime of the system and is typically 10,000,000 (10 MHz) on modern Windows.

2. **`QueryPerformanceCounter(&count)`** returns the current tick count.

3. **Elapsed time** = `(current_count - start_count) / frequency`

The implementation caches the frequency and start count at `time_init()`:

```cpp
static LARGE_INTEGER g_freq;
static LARGE_INTEGER g_start;

void time_init() {
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_start);
}

double now_seconds() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - g_start.QuadPart)
         / static_cast<double>(g_freq.QuadPart);
}
```

The division by frequency converts ticks to seconds. Using `double` for the result preserves precision even after hours of runtime.

---

## Window and Message Pump (`window.h`)

### The Win32 Window Model

Windows uses a message-driven programming model. When the user clicks a mouse button, presses a key, moves the mouse, or resizes the window, the OS creates a message and places it in the application's message queue. The application must periodically drain this queue by calling `PeekMessage`/`DispatchMessage` in its main loop — this is the "message pump."

If the application stops pumping messages, the window becomes unresponsive (the "(Not Responding)" label in the title bar). This is why `pump_events()` must be called every frame.

### API

The `Window` class wraps the Win32 HWND with a clean, platform-independent interface:

```cpp
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
    void pump_events();
    bool should_close() const;
    void* native_handle() const;     // HWND as void*
    std::uint32_t width()  const;
    std::uint32_t height() const;

    // Input polling
    bool key_down(KeyCode key) const;
    bool mouse_button_down(MouseButton btn) const;
    void mouse_position(float& x, float& y) const;
    void mouse_delta(float& dx, float& dy) const;
    void set_mouse_capture(bool enable);
};

}
```

### The pImpl Pattern in Practice

The `Window` class stores a single `void* impl_` member. This pointer is allocated in `create()` and freed in `destroy()`. The implementation struct (in `window_win32.cpp`) holds all the Win32-specific state:

```
Window (public header)          WindowImpl (private, in .cpp)
+-------------------+          +---------------------------+
| impl_  ------>------------>  | HWND hwnd                 |
+-------------------+          | bool should_close         |
                               | bool keys[256]            |
                               | bool mouse_buttons[3]     |
                               | float mouse_x, mouse_y    |
                               | float delta_x, delta_y    |
                               | bool raw_input_enabled    |
                               +---------------------------+
```

The window procedure (`WndProc`) is a static callback that Win32 invokes when a message is dispatched. It retrieves the `WindowImpl*` from the window's user data (set during `CreateWindowEx`) and updates the appropriate fields.

### Input Handling

Input state is captured during the message pump. The window procedure processes these Win32 messages:

| Message | What it captures |
|---------|-----------------|
| `WM_KEYDOWN` / `WM_KEYUP` | `keys[vk_code]` set to `true` / `false` |
| `WM_LBUTTONDOWN` / `WM_LBUTTONUP` | `mouse_buttons[Left]` |
| `WM_RBUTTONDOWN` / `WM_RBUTTONUP` | `mouse_buttons[Right]` |
| `WM_MBUTTONDOWN` / `WM_MBUTTONUP` | `mouse_buttons[Middle]` |
| `WM_MOUSEMOVE` | `mouse_x`, `mouse_y` (client-area coordinates) |
| `WM_INPUT` | Raw mouse delta (`delta_x`, `delta_y`) |
| `WM_CLOSE` | `should_close = true` |
| `WM_SIZE` | `width`, `height` updated |

The `KeyCode` enum values intentionally match the Win32 `VK_*` virtual key codes, so the window procedure can use the virtual key code directly as an array index with no translation:

```cpp
enum class KeyCode : std::uint8_t {
    W      = 0x57,  // VK_W
    A      = 0x41,  // VK_A
    S      = 0x53,  // VK_S
    D      = 0x44,  // VK_D
    Escape = 0x1B,  // VK_ESCAPE
    LShift = 0xA0,  // VK_LSHIFT
    // ...
};
```

This means `key_down(KeyCode::W)` is a direct array lookup: `impl->keys[0x57]`. No hash map, no switch statement, no branching.

### Raw Mouse Input

For FPS-style camera control, standard `WM_MOUSEMOVE` messages are inadequate. They report the cursor position in screen coordinates, which is bounded by the screen edges — when the cursor reaches the edge, movement stops. They also have OS acceleration (pointer ballistics) applied.

Raw mouse input (`RAWINPUT`) solves both problems:

1. **Unbounded deltas.** Raw input reports mouse movement in device units (mickeys), not screen coordinates. There are no edges.
2. **No acceleration.** The raw values represent actual physical mouse movement.
3. **Works while captured.** Raw input continues to report movement even when the cursor is hidden and clipped.

The `set_mouse_capture(true)` call:
1. Registers the window for raw mouse input (`RegisterRawInputDevices`).
2. Hides the cursor (`ShowCursor(FALSE)`).
3. Clips the cursor to the window area (`ClipCursor`).

When raw input is enabled, `WM_INPUT` messages deliver `RAWINPUT` data with `lLastX`/`lLastY` deltas. These accumulate during `pump_events()` and are reset each frame when `mouse_delta()` is called.

---

## File I/O (`file.h`)

### API

```cpp
namespace ge::platform {

struct FileData {
    void*       data = nullptr;  // Owned; free with file_data_free()
    std::size_t size = 0;
};

FileData    file_read_all(const char* path);   // Read entire file into memory
void        file_data_free(FileData& fd);       // Free memory from file_read_all
bool        file_write_all(const char* path, const void* data, std::size_t size);
bool        file_exists(const char* path);
std::size_t file_size(const char* path);

}
```

The key design choice: `file_read_all()` returns memory allocated via `platform::mem_alloc()` (the VirtualAlloc wrapper), not via `new` or `malloc`. This keeps the memory lifecycle within the platform layer's own allocator, maintaining the rule that the platform depends on nothing but the OS.

The caller is responsible for freeing the returned memory with `file_data_free()`, which calls `platform::mem_free()` internally.

### Implementation: Win32 File APIs

The Win32 implementation uses:

- **`CreateFile`** — opens a file handle with specified access mode (read, write, or both).
- **`GetFileSize`** — queries the file size to allocate the right amount of memory.
- **`ReadFile`** — reads the file contents into the allocated buffer in a single call.
- **`WriteFile`** — writes a buffer to disk.
- **`GetFileAttributes`** — checks if a file exists (returns `INVALID_FILE_ATTRIBUTES` if not).

These are lower-level than C's `fopen`/`fread`, but they give more control over buffering, access patterns, and error handling. For the engine's use case (read the entire file at once, or write the entire file at once), they are straightforward.
