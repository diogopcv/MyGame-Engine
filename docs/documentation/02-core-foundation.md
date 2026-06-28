# Core Foundation: Types, Logging, and Assertions

The core foundation provides the most basic building blocks that every other engine system uses: fixed-width type aliases, a logging system, and assertion macros. These are so fundamental that they appear in virtually every source file.

## Table of Contents

- [Fixed-Width Type Aliases (`types.h`)](#fixed-width-type-aliases-typesh)
  - [Why Aliases?](#why-aliases)
  - [The Platform Exception](#the-platform-exception)
  - [Size Macros](#size-macros)
- [Logging System (`log.h`)](#logging-system-logh)
  - [Log Levels](#log-levels)
  - [Compile-Time Format Checking](#compile-time-format-checking)
  - [Implementation Details](#implementation-details)
  - [Usage Examples](#usage-examples)
- [Assertion System (`assert.h`)](#assertion-system-asserth)
  - [When to Use Assertions](#when-to-use-assertions)
  - [How GE_ASSERT Works](#how-ge_assert-works)
  - [The do-while(0) Idiom](#the-do-while0-idiom)

---

## Fixed-Width Type Aliases (`types.h`)

### Why Aliases?

C++ has built-in types like `int`, `long`, and `float`, but their sizes are platform-dependent. An `int` is 32 bits on most desktop platforms, but that is not guaranteed by the standard. In a game engine, where binary data layouts must be exact (for serialization, GPU uploads, network packets), relying on platform-dependent sizes is a bug waiting to happen.

The `<cstdint>` header provides fixed-width types like `std::uint32_t`, but these names are verbose. Writing `std::uint32_t` hundreds of times per file adds visual noise and hurts readability.

MyEngine defines short, unambiguous aliases in `core/types.h`:

```cpp
namespace ge {
    using u8  = std::uint8_t;   // Unsigned  8-bit (0 to 255)
    using u16 = std::uint16_t;  // Unsigned 16-bit (0 to 65,535)
    using u32 = std::uint32_t;  // Unsigned 32-bit (0 to ~4.2 billion)
    using u64 = std::uint64_t;  // Unsigned 64-bit

    using i8  = std::int8_t;    // Signed  8-bit (-128 to 127)
    using i16 = std::int16_t;   // Signed 16-bit
    using i32 = std::int32_t;   // Signed 32-bit
    using i64 = std::int64_t;   // Signed 64-bit

    using f32 = float;          // 32-bit IEEE 754 float
    using f64 = double;         // 64-bit IEEE 754 double

    using usize = std::size_t;  // Pointer-sized unsigned integer
}
```

These aliases are used everywhere in the engine (in core and above). They make the intended data width explicit and visually compact.

### The Platform Exception

The platform layer (Layer 0) deliberately does **not** use these aliases. It uses raw `std::size_t` and `std::uint32_t` from `<cstdint>`. This is because the platform layer cannot depend on the core layer (the dependency would point upward, violating the layering rule). The type aliases live in `core/types.h`, so the platform layer uses the standard types directly.

This is a deliberate architectural choice: it keeps the platform layer self-contained, even at the cost of slightly more verbose type names in platform code.

### Size Macros

Three convenience macros convert human-readable sizes to bytes:

```cpp
#define GE_KILOBYTES(x) ((x) * 1024ull)
#define GE_MEGABYTES(x) (GE_KILOBYTES(x) * 1024ull)
#define GE_GIGABYTES(x) (GE_MEGABYTES(x) * 1024ull)
```

Usage:

```cpp
arena.init(GE_MEGABYTES(64));  // Reserve 64 MB
```

The `ull` suffix ensures the computation uses 64-bit unsigned arithmetic, preventing overflow for large sizes.

---

## Logging System (`log.h`)

### Log Levels

The logger supports four severity levels, from most verbose to most critical:

| Level | Macro | When to use |
|-------|-------|-------------|
| `Trace` | `GE_TRACE(...)` | Verbose diagnostic output. Use during development to trace execution flow. |
| `Info` | `GE_INFO(...)` | General informational messages. Startup banners, system initialization, configuration. |
| `Warn` | `GE_WARN(...)` | Something unexpected that does not prevent execution. Missing optional file, fallback behavior. |
| `Error` | `GE_ERROR(...)` | Something is wrong and may affect correctness. Failed initialization, invalid state. |

### Compile-Time Format Checking

The log macros use `std::format` (from C++20) under the hood:

```cpp
#define GE_INFO(...) \
    ::ge::log::write(::ge::log::Level::Info, __FILE__, __LINE__, std::format(__VA_ARGS__))
```

The `std::format` call uses `std::format_string`, which validates the format string at compile time. If you write:

```cpp
GE_INFO("Position: ({}, {}, {})", x, y);  // ERROR: missing 3rd argument
```

The compiler will produce an error. This catches a common class of bugs (mismatched format string and arguments) that would otherwise be runtime errors with `printf`.

The format syntax follows Python/C++ format strings:

```cpp
GE_INFO("Loaded {} meshes in {:.2f} ms", count, elapsed_ms);
GE_INFO("Arena: {}/{} bytes used", arena.used(), arena.capacity());
```

### Implementation Details

The `log::write()` function outputs to two destinations:

1. **Standard output (`stdout`)** — visible in the terminal/console.
2. **`OutputDebugString`** — visible in Visual Studio's "Output" window while debugging. This is extremely useful during development because you can see log output without switching to a terminal.

Each log line is prefixed with:
- The severity level (`[TRACE]`, `[INFO]`, `[WARN]`, `[ERROR]`)
- The source file and line number

This makes it easy to find the log call site when investigating an issue.

### Usage Examples

```cpp
// Startup
GE_INFO("Booting {} ...", "MyEngine");
GE_INFO("Window created ({}x{})", window.width(), window.height());

// Normal operation
GE_INFO("FPS: {} | frame: {:.2f} ms", frames, avg_ms);
GE_INFO("Loaded model: {}", model_path);

// Warnings
GE_WARN("Could not load model -- using built-in cube");

// Errors
GE_ERROR("Failed to create window");
GE_ERROR("Failed to initialize the DX12 renderer");
```

---

## Assertion System (`assert.h`)

### When to Use Assertions

Assertions check invariants — conditions that must always be true if the code is correct. They are a debugging tool, not an error-handling mechanism.

Good uses of assertions:
- **Preconditions:** `GE_ASSERT(ptr != nullptr)` at the start of a function that requires a valid pointer.
- **Array bounds:** `GE_ASSERT(index < count_)` before indexing into an array.
- **Allocation:** `GE_ASSERT(base_ != nullptr)` after a memory allocation that should never fail.
- **State invariants:** `GE_ASSERT(offset_ <= size_)` verifying that an allocator's state is consistent.

Bad uses of assertions:
- Checking user input (use validation and error returns).
- Checking file existence (use `file_exists()` and handle the failure).
- Checking network responses (use error codes).

The distinction is: assertions catch **programmer bugs**; error handling catches **runtime conditions**.

### How GE_ASSERT Works

```cpp
#define GE_ASSERT(cond)                                                    \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ::ge::log::write(::ge::log::Level::Error, __FILE__, __LINE__,  \
                             "Assertion failed: " #cond);                  \
            GE_DEBUGBREAK();                                               \
        }                                                                  \
    } while (0)
```

When the condition is false:

1. **Logs the failure** with the file, line number, and the stringified condition. The `#cond` preprocessor operator converts the condition expression to a string literal, so `GE_ASSERT(x > 0)` logs `"Assertion failed: x > 0"`.

2. **Breaks into the debugger** via `GE_DEBUGBREAK()`, which expands to `__debugbreak()` on MSVC. If a debugger is attached, execution pauses at the assert line, letting you inspect the full call stack and variable state.

### The do-while(0) Idiom

The `do { ... } while (0)` wrapper is a standard C/C++ macro idiom. It ensures the macro expands to a single statement that works correctly in all contexts, including after `if` without braces:

```cpp
if (condition)
    GE_ASSERT(x > 0);  // Without do-while(0), this could misbehave
else
    do_something();
```

Without the `do-while(0)`, the `if` inside the macro would consume the `else`, causing a subtle and hard-to-find bug. The `do-while(0)` wraps everything into a single compound statement, and the compiler optimizes away the dead loop.

### Debug Break

The `GE_DEBUGBREAK()` macro is platform-specific:

```cpp
#if defined(_MSC_VER)
    #define GE_DEBUGBREAK() __debugbreak()  // MSVC: triggers INT 3 breakpoint
#else
    #define GE_DEBUGBREAK() __builtin_trap() // GCC/Clang: triggers a trap instruction
#endif
```

`__debugbreak()` is an MSVC intrinsic that generates an `INT 3` instruction — the x86 breakpoint interrupt. If a debugger is attached, execution pauses. If no debugger is attached, the program crashes with a breakpoint exception, which is still useful because it generates a crash dump pointing to the exact line.
