# Core Systems

This document covers the advanced core systems built in Phase 2: containers, string identifiers, the reflection system, binary serialization, the job system, generational handles, the profiler, and the console variable system. Together, these form the infrastructure that every higher-level engine system depends on.

## Table of Contents

- [Containers](#containers)
  - [Array\<T\> — Dynamic Array](#arrayt--dynamic-array)
  - [HashMap\<K,V\> — Open-Addressing Hash Map](#hashmapkv--open-addressing-hash-map)
- [String Identifiers (`string_id.h`)](#string-identifiers-string_idh)
  - [The FNV-1a Hash Algorithm](#the-fnv-1a-hash-algorithm)
  - [Compile-Time Hashing](#compile-time-hashing)
  - [Debug Reverse Lookup](#debug-reverse-lookup)
- [Reflection System (`reflect.h`)](#reflection-system-reflecth)
  - [Why Reflection?](#why-reflection)
  - [TypeInfo and FieldInfo](#typeinfo-and-fieldinfo)
  - [The Annotation Macros](#the-annotation-macros)
  - [The Type Registry](#the-type-registry)
- [Binary Serialization (`serialize.h`)](#binary-serialization-serializeh)
  - [BinaryWriter and BinaryReader](#binarywriter-and-binaryreader)
  - [Reflection-Driven Serialization](#reflection-driven-serialization)
  - [Versioning and Forward Compatibility](#versioning-and-forward-compatibility)
- [Job System (`job_system.h`)](#job-system-job_systemh)
  - [Fork/Join Model](#forkjoin-model)
  - [Worker Threads and the Job Queue](#worker-threads-and-the-job-queue)
  - [Job Counters and Synchronization](#job-counters-and-synchronization)
  - [Counter Recycling](#counter-recycling)
- [Generational Handles (`handle.h`)](#generational-handles-handleh)
  - [The Use-After-Free Problem](#the-use-after-free-problem)
  - [How Generational Handles Work](#how-generational-handles-work)
  - [HandleTable\<T\> — The Slot Array](#handletablet--the-slot-array)
  - [Type Safety via Phantom Tags](#type-safety-via-phantom-tags)
- [Profiler (`profiler.h`)](#profiler-profilerh)
  - [Hierarchical Scopes](#hierarchical-scopes)
  - [The Ring Buffer](#the-ring-buffer)
  - [The GE_PROFILE_SCOPE Macro](#the-ge_profile_scope-macro)
- [Console Variables (`cvar.h`)](#console-variables-cvarh)
  - [What Are CVars?](#what-are-cvars)
  - [The CVar Registry](#the-cvar-registry)
  - [Command Execution](#command-execution)

---

## Containers

The engine provides two container types that replace `std::vector` and `std::unordered_map`. The key difference from the standard containers: they allocate through the engine's `Allocator` interface, not the CRT heap.

### Array\<T\> — Dynamic Array

`Array<T>` is a contiguous, dynamically-growing array, functionally similar to `std::vector<T>`.

```cpp
ge::Arena arena;
arena.init(GE_MEGABYTES(1));
ge::Array<int> numbers(arena.allocator());
numbers.push(42);
numbers.push(7);
int x = numbers[0];  // 42
```

**Growth strategy:** When the array is full and a push is requested, it doubles its capacity (starting from 8). This amortizes the cost of reallocation: on average, each element is copied only O(1) times across all insertions.

**Move semantics:** The array is move-only (non-copyable). This prevents accidental copies of large arrays. When the array grows, existing elements are move-constructed into the new storage, and the old storage is freed through the allocator.

**Destructor behavior:** On destruction, `~Array()` calls `~T()` on every element, then frees the underlying storage. For POD types, the destructors are no-ops, and the compiler optimizes them away.

**Key operations:**
| Operation | Complexity | Description |
|-----------|-----------|-------------|
| `push(value)` | O(1) amortized | Append to the back; may trigger a reallocation |
| `pop()` | O(1) | Remove and destroy the last element |
| `operator[i]` | O(1) | Bounds-checked random access |
| `reserve(n)` | O(n) | Pre-allocate for n elements (avoids future reallocations) |
| `resize(n)` | O(n) | Change element count; value-initializes new elements |
| `clear()` | O(n) | Destroy all elements (capacity unchanged) |
| Iteration | O(n) | `begin()`/`end()` for range-based for loops |

### HashMap\<K,V\> — Open-Addressing Hash Map

`HashMap<K,V>` is an open-addressing hash map with linear probing.

```cpp
ge::Arena arena;
arena.init(GE_MEGABYTES(1));
ge::HashMap<ge::u32, const char*> names(arena.allocator());
names.set(1, "player");
names.set(2, "enemy");
const char** found = names.get(1);  // "player"
```

**Open addressing** means all entries are stored directly in the slot array (no separate linked-list nodes). When a collision occurs (two keys hash to the same slot), the map probes forward through consecutive slots until it finds an empty one. This is called "linear probing."

**Why open addressing over chaining?** Chaining (using linked lists at each bucket) requires per-entry heap allocations for list nodes, which is exactly what the engine's allocator design avoids. Open addressing stores everything in a single contiguous array, which is cache-friendly.

**Load factor:** The map grows when the combined count of live entries and tombstones exceeds 75% of capacity. This ensures that probe chains remain short. The load factor is a critical tuning parameter: too high causes long probe chains; too low wastes memory.

**Tombstones:** When an entry is removed, its slot is marked as "deleted" (a tombstone) rather than "empty." This is necessary because probing must continue past deleted slots to find entries that were inserted after them. Without tombstones, removal could break existing probe chains, making some entries unreachable.

The map counts tombstones separately and includes them in the growth trigger. If many entries are added and removed, tombstones accumulate. The growth trigger ensures that even a "tombstone-heavy" map eventually rehashes, sweeping away the tombstones and shortening probe chains.

**Hashing:** The default hash function is FNV-1a over the key's raw bytes. Specializations for `u32`, `u64`, and `i32` use bit-mixing functions that produce better distributions for integer keys:

```cpp
// u64 specialization (splitmix64-style bit mixing):
template <> inline u64 hash_key<u64>(const u64& key) {
    u64 h = key;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
    h = h ^ (h >> 31);
    return h;
}
```

These mixing functions avalanche every bit of the input across the output, preventing clustering patterns that arise when consecutive integers are hashed naively.

---

## String Identifiers (`string_id.h`)

### The FNV-1a Hash Algorithm

String comparisons are expensive: comparing two 20-character strings requires up to 20 byte comparisons. In a game engine, strings are used everywhere (type names, component names, event names, asset paths), and comparing them in inner loops is a performance bottleneck.

**StringId** solves this by hashing each string to a 64-bit integer. Comparing two StringIds is a single integer comparison — one CPU instruction.

The hash algorithm is FNV-1a (Fowler-Noll-Vo, alternate):

```cpp
constexpr StringId string_id(const char* str) {
    u64 h = 14695981039346656037ull;  // FNV offset basis
    for (const char* p = str; *p != '\0'; ++p) {
        h ^= static_cast<u64>(static_cast<u8>(*p));
        h *= 1099511628211ull;  // FNV prime
    }
    return { h };
}
```

FNV-1a processes each byte of the string:
1. XOR the byte into the hash.
2. Multiply by the FNV prime.

The specific offset basis and prime constants are chosen to produce a good distribution with minimal collision probability for typical string inputs.

### Compile-Time Hashing

Because `string_id()` is `constexpr`, the compiler can evaluate it during compilation:

```cpp
constexpr StringId player_id = string_id("player");
// The hash is computed at compile time — no runtime cost
```

The `GE_SID` macro adds debug registration on top:

```cpp
#define GE_SID(lit) ([]() -> ::ge::StringId {
    static const ::ge::StringId id_ = ::ge::string_id(lit);
    static const bool reg_ = (::ge::string_id_register(id_, lit), true);
    return id_;
}())
```

This ensures the string is registered in the debug lookup table exactly once (via the `static` variable), even if the macro is invoked multiple times.

### Debug Reverse Lookup

In debug builds, `string_id_register()` stores the original string in a lookup table keyed by the hash. `string_id_str()` retrieves it. This enables meaningful debug output:

```cpp
// Instead of: "Component hash=0x7A3F..."
// You see:    "Component name=Transform"
```

In release builds (`NDEBUG` defined), the registration functions and the lookup table are compiled out entirely, leaving only the raw integer hash.

---

## Reflection System (`reflect.h`)

### Why Reflection?

C++ has no built-in way to enumerate the fields of a struct at runtime. If you have a `struct PlayerData { vec3 position; f32 health; }`, there is no standard C++ mechanism to iterate over its fields, read their names, or know their types.

Game engines need this capability for:
- **Serialization:** automatically writing/reading all fields to/from a file.
- **Editor inspectors:** generating property panels that display and edit every field.
- **Network replication:** sending changed fields to remote clients.
- **Scripting bindings:** exposing engine types to a scripting language.

The engine's reflection system provides exactly this: metadata that describes the fields of a struct — their names, types, offsets, and sizes.

### TypeInfo and FieldInfo

The two key data structures:

```cpp
struct FieldInfo {
    StringId        name;    // Hashed field name
    FieldType       type;    // Enum: Bool, U32, F32, Vec3, Struct, etc.
    usize           offset;  // Byte offset from struct start (via offsetof)
    usize           size;    // sizeof(field)
    const TypeInfo* nested;  // For nested struct fields
};

struct TypeInfo {
    StringId         name;        // Hashed type name
    usize            size;        // sizeof(Type)
    usize            alignment;   // alignof(Type)
    u32              field_count;
    const FieldInfo* fields;      // Array of field descriptors
    u32              version;     // Schema version
};
```

A `TypeInfo` describes a type: its name, size, alignment, schema version, and an array of `FieldInfo` entries (one per reflected field). Each `FieldInfo` describes a single field: its name, type tag, byte offset from the start of the struct, and (for nested structs) a pointer to the nested type's `TypeInfo`.

### The Annotation Macros

To reflect a struct, you annotate it with macros:

```cpp
struct PlayerData {
    vec3 position;
    f32  health;
    u32  score;

    GE_REFLECT_BEGIN(PlayerData)
        GE_FIELD(position, Vec3)
        GE_FIELD(health,   F32)
        GE_FIELD(score,    U32)
    GE_REFLECT_END(PlayerData)
};
```

These macros expand to a static `ge_type_info()` method that returns a compile-time-initialized `TypeInfo`:

```cpp
static const TypeInfo* ge_type_info() {
    using Self_ = PlayerData;
    static const FieldInfo fields_[] = {
        { string_id("position"), FieldType::Vec3, offsetof(Self_, position), sizeof(Self_::position), nullptr },
        { string_id("health"),   FieldType::F32,  offsetof(Self_, health),   sizeof(Self_::health),   nullptr },
        { string_id("score"),    FieldType::U32,  offsetof(Self_, score),    sizeof(Self_::score),    nullptr },
    };
    static const TypeInfo info_ {
        string_id("PlayerData"), sizeof(PlayerData), alignof(PlayerData),
        3, fields_, 1  // 3 fields, version 1
    };
    return &info_;
}
```

The `offsetof` macro (from `<cstddef>`) computes the byte offset of a member within a struct at compile time. This is how the serialization system knows where each field lives in memory.

### The Type Registry

Types can be registered in a global registry for runtime lookup by name:

```cpp
type_register(PlayerData::ge_type_info());
// Later:
const TypeInfo* info = type_find(string_id("PlayerData"));
```

This will be used by the editor (to build inspectors dynamically) and by the network system (to replicate fields by name).

---

## Binary Serialization (`serialize.h`)

### BinaryWriter and BinaryReader

**BinaryWriter** is a growable output stream backed by an `Array<u8>`:

```cpp
ge::Arena arena;
arena.init(GE_MEGABYTES(1));
ge::BinaryWriter writer(arena.allocator());
writer.write_u32(42);
writer.write_f32(3.14f);
// writer.data() points to 8 bytes of binary data
```

**BinaryReader** reads from a contiguous byte buffer:

```cpp
ge::BinaryReader reader(data_ptr, data_size);
u32 x; reader.read_u32(x);  // x = 42
f32 y; reader.read_f32(y);  // y = 3.14
```

Both maintain an internal offset that advances with each read/write. The reader returns `false` if there is not enough data remaining, enabling graceful handling of truncated files.

### Reflection-Driven Serialization

The `serialize()` and `deserialize()` functions walk a `TypeInfo` descriptor and automatically process every reflected field:

```cpp
void serialize(BinaryWriter& w, const void* data, const TypeInfo& type) {
    w.write_u32(type.version);       // Schema version for forward compatibility
    w.write_u32(type.field_count);   // Number of fields

    for (u32 i = 0; i < type.field_count; ++i) {
        const FieldInfo& f = type.fields[i];
        const void* field_ptr = static_cast<const u8*>(data) + f.offset;

        if (f.type == FieldType::Struct) {
            serialize(w, field_ptr, *f.nested);  // Recurse for nested structs
        } else {
            w.write_bytes(field_ptr, field_primitive_size(f.type));
        }
    }
}
```

The function uses pointer arithmetic (`data + offset`) to locate each field in the struct's memory, then writes its raw bytes. For nested structs, it recurses into the nested `TypeInfo`.

Deserialization does the reverse: it reads bytes from the stream and writes them into the appropriate offset in the target struct. It also validates the version and field count, returning `false` on mismatch.

### Versioning and Forward Compatibility

Each `TypeInfo` carries a `version` number (currently 1). The serialized format includes this version alongside the field count. During deserialization, the reader checks that the stored version matches the expected version. If they differ, deserialization fails gracefully (returns `false`) rather than silently reading corrupted data.

This is the foundation for schema evolution: when a struct gains or loses fields in a future version, the version number changes, and the deserializer can handle the migration.

---

## Job System (`job_system.h`)

### Fork/Join Model

The job system enables parallel execution on multiple CPU cores. It uses a "fork/join" model: the main thread submits a batch of independent jobs (fork), and later waits for all of them to finish (join).

```cpp
ge::JobSystem jobs;
jobs.init();  // Spawns (hardware_concurrency - 1) worker threads

// Create 100 independent jobs
ge::JobDecl decls[100];
for (int i = 0; i < 100; ++i) {
    decls[i].fn = [](void* data) {
        int* index = static_cast<int*>(data);
        // ... do work for item *index ...
    };
    decls[i].data = &indices[i];
}

// Submit and wait
ge::JobCounter* counter = jobs.kick_jobs(decls, 100);
jobs.wait_for_counter(counter);  // Blocks until all 100 jobs finish
```

### Worker Threads and the Job Queue

At startup, `init()` creates a pool of worker threads (default: `hardware_concurrency - 1`, so on an 8-core CPU, 7 workers are created, plus the main thread). Each worker runs a loop:

```
while not shutting down:
    wait for a job to appear in the queue
    pop a job from the queue
    execute it
    decrement the job's counter
    notify any threads waiting on the counter
```

The job queue is a simple `std::queue` protected by a mutex. Workers block on a condition variable when the queue is empty, so they consume zero CPU when idle.

### Job Counters and Synchronization

Each batch of jobs gets a `JobCounter` — an atomic integer that starts at the job count and is decremented by each worker upon completion. The main thread can poll or wait on this counter:

```cpp
JobCounter* counter = jobs.kick_jobs(decls, count);
// counter starts at count, decremented to 0 as jobs finish

jobs.wait_for_counter(counter);  // Blocks until counter == 0
```

The wait uses a condition variable (not busy-spinning), so the waiting thread sleeps until woken by a completing worker. This is efficient: no CPU is wasted polling.

The atomic operations on the counter use appropriate memory ordering:
- `fetch_add` uses `relaxed` (no ordering constraint needed for incrementing).
- `fetch_sub` uses `release` (ensures the job's side effects are visible to the waiter).
- `load` uses `acquire` (ensures the waiter sees the job's side effects after observing the counter change).

### Counter Recycling

Counters are pooled and recycled. When `wait_for_counter(counter, 0)` returns, the counter is placed on a free list for reuse. This avoids repeated heap allocation for counters in a frame-loop scenario.

---

## Generational Handles (`handle.h`)

### The Use-After-Free Problem

When a system creates an object and gives the caller a pointer to it, what happens when the object is destroyed? If the caller still holds the pointer, they have a "dangling pointer" — dereferencing it is undefined behavior and typically crashes or corrupts data.

Raw pointers cannot detect this. The pointer looks the same whether the object exists or not.

### How Generational Handles Work

A generational handle is an index paired with a generation counter:

```cpp
template <typename Tag>
struct Handle {
    u32 index      = ~u32(0);  // Slot index (invalid sentinel = 0xFFFFFFFF)
    u32 generation = 0;        // Generation counter
};
```

When an object is created, it gets a slot in the table. The handle stores the slot's index and current generation. When the object is destroyed, the slot's generation is incremented. Any outstanding handles still hold the old generation and will fail the generation check on the next access attempt.

```
Timeline:
  1. Create object A in slot 3, generation 0  →  Handle{3, 0}
  2. Destroy object A: slot 3 generation becomes 1
  3. Create object B in slot 3, generation 1  →  Handle{3, 1}
  4. Old handle {3, 0} tries to access slot 3:
     slot generation (1) != handle generation (0) → returns nullptr (stale handle)
```

This catches use-after-free at runtime, returning `nullptr` instead of accessing the wrong object.

### HandleTable\<T\> — The Slot Array

The `HandleTable<T>` stores objects in a flat array of slots. Each slot has the object, a generation counter, and an alive flag. A free list of available slot indices enables O(1) allocation and deallocation:

```cpp
template <typename T, typename Tag = T>
class HandleTable {
    struct Slot {
        T    data;
        u32  generation;
        bool alive;
    };

    Slot* slots_;
    u32*  free_list_;
    u32   capacity_, count_, free_count_;
};
```

**add(item):** Pop an index from the free list, construct the item in that slot, return a handle with the slot's current generation.

**remove(handle):** Validate the handle (check generation), destroy the item, bump the slot's generation, push the index back onto the free list.

**get(handle):** Validate the handle. If the generation matches and the slot is alive, return a pointer to the data. Otherwise, return `nullptr`.

### Type Safety via Phantom Tags

The `Tag` template parameter is a "phantom type" — it exists only in the type system, carrying no runtime data. Its purpose is to prevent accidentally using a `BufferHandle` where a `TextureHandle` is expected:

```cpp
struct BufferTag {};
struct TextureTag {};

using BufferHandle  = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;

BufferHandle buf = ...;
TextureHandle tex = ...;
rhi.set_texture(0, buf);  // COMPILE ERROR: BufferHandle != TextureHandle
```

Without phantom tags, both handles would be `Handle<>` (same type), and the compiler would not catch the mistake.

---

## Profiler (`profiler.h`)

### Hierarchical Scopes

The profiler records the duration of named code sections (scopes). Scopes can be nested, creating a hierarchy:

```
Frame
  ├─ update (2.1 ms)
  │    ├─ physics (1.2 ms)
  │    └─ animation (0.8 ms)
  └─ render (5.3 ms)
       ├─ shadow pass (1.1 ms)
       └─ main pass (4.0 ms)
```

Each scope records its name (as a `StringId`), start time, end time, and nesting depth.

### The Ring Buffer

The profiler stores the last 120 frames of profiling data in a ring buffer. When the buffer is full, the oldest frame is overwritten. This provides a rolling history that can be displayed in an overlay or exported for analysis, without unbounded memory growth.

```cpp
class Profiler {
    static constexpr u32 kRingSize = 120;
    ProfileFrame frames_[kRingSize];
    u32 write_idx_ = 0;  // Wraps around modulo kRingSize
};
```

Each `ProfileFrame` holds up to 128 scopes (`ProfileScope` entries) and the total frame duration.

### The GE_PROFILE_SCOPE Macro

The `GE_PROFILE_SCOPE` macro creates an RAII (Resource Acquisition Is Initialization) guard that opens a scope on construction and closes it on destruction:

```cpp
#define GE_PROFILE_SCOPE(name) \
    ::ge::ProfileScopeGuard ge_profile_guard_##__LINE__(::ge::string_id(name))
```

Usage:
```cpp
void render() {
    GE_PROFILE_SCOPE("render");  // Opens scope "render"
    // ... rendering code ...
}  // Scope automatically closed when the function returns
```

The RAII pattern guarantees the scope is closed even if the function has multiple return paths or throws an exception. The `__LINE__` suffix ensures unique variable names when multiple scopes are declared in the same function.

---

## Console Variables (`cvar.h`)

### What Are CVars?

Console variables (CVars) are named, typed configuration values that can be queried and modified at runtime. They bridge the gap between compile-time constants and fully dynamic configuration.

```cpp
cvar_system().register_bool("r_vsync", true, "enable vsync");
cvar_system().register_int("r_width", 1280, "render width");

// Later, at runtime:
CVar* vsync = cvar_system().find("r_vsync");
if (vsync && vsync->b) {
    // vsync is enabled
}

// Or via command string (from console input):
cvar_system().execute("r_vsync 0");  // Disable vsync
```

CVars are the standard mechanism in game engines for runtime tweaking. A debug console can display and modify them, config files can set them at startup, and code can query them anywhere.

### The CVar Registry

The `CVarSystem` stores up to 256 CVars in a flat array. Each CVar is a tagged union:

```cpp
struct CVar {
    StringId    name;         // Hashed name for O(1) lookup
    CVarType    type;         // Int, Float, or Bool
    union {
        i32  i;
        f32  f;
        bool b;
    };
    const char* description;  // Human-readable tooltip
};
```

Lookup is by `StringId`: the system scans the array comparing hashed names. With a maximum of 256 entries, a linear scan is fast enough (the data fits in a few cache lines).

### Command Execution

The `execute()` method parses a simple command string:

```
"r_vsync 1"     →  find CVar "r_vsync", set its value to 1
"r_width 1920"  →  find CVar "r_width", set its value to 1920
```

The parser splits the string into name and value, looks up the CVar by name, and sets the value according to the CVar's type. This will eventually be connected to an in-game debug console where the user can type commands to modify engine behavior on the fly.
