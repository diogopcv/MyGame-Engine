# ECS and World (Phase 4)

This document covers the Entity-Component-System (ECS) that Phase 4 added: how entities, components, and queries work, why storage is sparse-set rather than archetype-based, how `Scene` layers a name registry and save/load on top of the raw ECS, and how `ge_input` decouples gameplay code from raw key codes. It closes with the "plain C++ systems" model that ties all of this to a running frame.

## Table of Contents

- [What Problem Does an ECS Solve?](#what-problem-does-an-ecs-solve)
- [Entity: A Generational Handle](#entity-a-generational-handle)
  - [The Use-After-Free Problem](#the-use-after-free-problem)
  - [Reusing HandleTable for Entity Lifecycle](#reusing-handletable-for-entity-lifecycle)
- [ComponentPool\<T\>: Sparse-Set Storage](#componentpoolt-sparse-set-storage)
  - [Sparse-Set vs. Archetype Storage](#sparse-set-vs-archetype-storage)
  - [The Sparse/Dense Layout](#the-sparsedense-layout)
  - [Add: Appending to the Dense Array](#add-appending-to-the-dense-array)
  - [Remove: Swap-and-Pop](#remove-swap-and-pop)
- [World: Tying Pools Together](#world-tying-pools-together)
  - [Type-Erased Pool Registry (No RTTI)](#type-erased-pool-registry-no-rtti)
  - [Queries: each\<T\> and each\<A,B\>](#queries-eacht-and-eacha-b)
- [Scene: Roster, Names, and Save/Load](#scene-roster-names-and-saveload)
  - [The Entity Roster](#the-entity-roster)
  - [The Name Registry](#the-name-registry)
  - [Save/Load Through Reflection](#saveload-through-reflection)
- [Baseline Components](#baseline-components)
- [ge_input: Decoupling Gameplay from Raw Keys](#ge_input-decoupling-gameplay-from-raw-keys)
  - [Actions and Axes](#actions-and-axes)
  - [resolve() and Testability](#resolve-and-testability)
- [The Scripting/Iteration Model: Plain C++ Systems](#the-scriptingiteration-model-plain-c-systems)
  - [Options Considered](#options-considered)
  - [What a System Looks Like](#what-a-system-looks-like)

---

## What Problem Does an ECS Solve?

A naive way to represent game objects is a class hierarchy: `GameObject` has subclasses `Player`, `Enemy`, `Pickup`, each carrying whatever fields and virtual methods it needs. This runs into two problems as a codebase grows:

1. **Combinatorial explosion.** A flying, shootable, health-having enemy and a stationary, shootable, health-having turret share two of three traits but not the third. Inheritance forces awkward choices — a common base class with unused fields, or multiple inheritance, or endless small subclasses.
2. **Cache-hostile iteration.** Looping over "every object with health" when objects are scattered across the heap as polymorphic pointers means chasing a pointer per object, with no guarantee that consecutive objects are anywhere near each other in memory.

An **Entity-Component-System** inverts the relationship. An entity is not an object with behavior — it is just an ID. Behavior-relevant data lives in components (plain structs: `Transform`, `Health`, `RigidBodyRef`), stored in flat arrays, one array per component type. "Every object with health" becomes "iterate the `Health` array" — a linear scan over contiguous memory, exactly the access pattern CPUs are fast at (see [00-architecture-overview.md](00-architecture-overview.md)'s Data-Oriented Design section). An entity that needs health and a transform simply has an entry in both arrays; there is no class to design up front for that combination.

MyEngine's ECS lives in `engine/world/` (`ge_world`, Layer 4). Per ADR-0008, its storage model is **sparse-set**, not archetype-based — the next section explains what that means and why.

---

## Entity: A Generational Handle

### The Use-After-Free Problem

If an entity were just a raw index into an array, a classic bug becomes possible: entity 5 is destroyed and its slot is reused for a brand-new entity; code that was still holding "entity 5" from before the destruction now silently refers to the wrong, unrelated object. This is a use-after-free bug, just at the entity-ID level instead of the pointer level.

The fix is a **generational handle**: pair the index with a generation counter that increments every time the slot is reused. A stale handle's generation no longer matches the slot's current generation, so a lookup can detect and reject it instead of silently returning garbage.

### Reusing HandleTable for Entity Lifecycle

MyEngine already has exactly this generational-handle machinery in `core/handle.h` (`Handle<Tag>` + `HandleTable<T, Tag>`), built in Phase 2 for GPU resource handles (see `06-graphics-and-rendering.md`'s Handle-Based Resource Management). Rather than write a second implementation, `Entity` reuses it directly:

```cpp
struct EntityTag {};
using Entity = ge::Handle<EntityTag>;
using EntityAllocator = ge::HandleTable<ge::u8, EntityTag>;
```

`Entity` is a `Handle<EntityTag>` — an `{index, generation}` pair, with `EntityTag` as a phantom type that stops an `Entity` from being accidentally compared against, say, a `BufferHandle`. `EntityAllocator` is a `HandleTable<u8, EntityTag>`: the stored payload type is `u8`, but that byte is never read — the ECS only wants `HandleTable`'s already-tested free-list-plus-generation-check machinery (`add()`/`remove()`/`get()` returning `nullptr` on a stale handle), not any payload it stores. `World::create_entity()` is a one-line call to `entities_.add(u8(0))`; `is_alive()` is `entities_.get(e) != nullptr`.

This is the same "reuse, don't reinvent" instinct that keeps the codebase small: a generational-handle bug fixed once in `core/handle.h` (and covered by its own unit tests) is fixed everywhere that type is reused, including here.

---

## ComponentPool\<T\>: Sparse-Set Storage

### Sparse-Set vs. Archetype Storage

Two storage models are common in ECS implementations:

- **Archetype (SoA chunks).** Entities are grouped by their exact set of component types into contiguous chunks — an entity with `{Transform, Health}` lives in a different chunk than one with `{Transform, Health, Velocity}`. Iteration over any single archetype is extremely cache-friendly. The cost is on the *write* side: adding or removing a component moves the entity's entire row to a different chunk (a "structural change"), which is more code and more edge cases to get right (chunk migration, an archetype graph, keeping any pointers into the old chunk from dangling).
- **Sparse set.** One dense, contiguous array per component type, plus a sparse lookup array mapping `entity index -> dense index`. Adding or removing a component only ever touches that one component's arrays — no migration, no archetype graph.

ADR-0008 chose sparse-set for MyEngine. The reasoning: archetypes pay for iteration speed on *stable, multi-component* queries over *large* entity counts, and Phase 4's scenes are small (a controllable character, a handful of lights and markers — low hundreds of entities at most, well short of where archetype-vs-sparse-set iteration speed would actually matter). Sparse-set is simpler to implement correctly, and simplicity that's cheap now and doesn't foreclose a later archetype rewrite (if a phase ever needs one) is the right trade at this scale. This is the same "don't build for hypothetical future requirements" instinct the whole codebase follows.

### The Sparse/Dense Layout

`ComponentPool<T>` (`engine/world/include/world/component_pool.h`) holds three parallel arrays:

```
dense_:           [T0]  [T1]  [T2]  ...      <- contiguous component data (iteration-friendly)
dense_entities_:  [E7]  [E2]  [E9]  ...      <- which entity owns each dense slot
sparse_:  index by Entity.index -> dense slot, or kInvalidDenseIndex
           E2 -> 1
           E7 -> 0
           E9 -> 2
```

`dense_` is what gets iterated — a plain `Array<T>` with no gaps, exactly as cache-friendly as a hand-written flat array. `sparse_` is what makes `has(e)` / `get(e)` O(1): look up `sparse_[e.index]`, and if it is not the sentinel `kInvalidDenseIndex`, that is the slot in `dense_`/`dense_entities_` holding `e`'s component.

### Add: Appending to the Dense Array

```cpp
T& add(Entity e, const T& value) {
    ensure_sparse(e.index);
    u32& slot = sparse_[e.index];
    if (slot != kInvalidDenseIndex) {
        dense_[slot] = value;      // already has one: overwrite in place
        return dense_[slot];
    }
    dense_.push(value);
    dense_entities_.push(e);
    slot = static_cast<u32>(dense_.size() - 1);
    return dense_.back();
}
```

A brand-new component is simply appended to the end of `dense_`/`dense_entities_`, and `sparse_[e.index]` is set to that new last index. `ensure_sparse` grows `sparse_` (filling new entries with the sentinel) if `e.index` is past its current size — entity indices are assigned by `EntityAllocator` independently of any particular pool, so a pool has no way to know its sparse array's required size in advance.

### Remove: Swap-and-Pop

Removing from the middle of a dense array without leaving a hole means swapping the removed element with the last one, then shrinking by one:

```cpp
void remove_entity(Entity e) override {
    if (e.index >= sparse_.size()) return;
    u32& slot = sparse_[e.index];
    if (slot == kInvalidDenseIndex) return;   // no-op if e has no component here

    usize last = dense_.size() - 1;
    if (slot != last) {
        dense_[slot]          = std::move(dense_[last]);
        dense_entities_[slot] = dense_entities_[last];
        sparse_[dense_entities_[slot].index] = slot;  // fix up the moved entity's sparse entry
    }
    dense_.pop();
    dense_entities_.pop();
    slot = kInvalidDenseIndex;
}
```

```
Before remove(E2):                  After remove(E2):
dense_:          [T(E7)] [T(E2)] [T(E9)]     dense_:          [T(E7)] [T(E9)]
dense_entities_: [E7]    [E2]    [E9]        dense_entities_: [E7]    [E9]
sparse_: E7->0, E2->1, E9->2                 sparse_: E7->0, E9->1 (moved!), E2->invalid
```

`E9`'s component was physically the last element, so it gets moved into the hole `E2` left, and `sparse_[E9.index]` is updated to point at its new slot. This keeps `dense_` fully packed (no gaps to skip over during iteration) at the cost of one extra move and one extra sparse-array write per removal — a good trade for a structure that is iterated far more often than it is mutated.

`remove_entity` (not `remove`) is also the method name `IComponentPool` requires — a small type-erased interface (`has()` / `remove_entity()` only) that lets `World::destroy_entity()` clean up an entity from every pool without needing to know each pool's component type `T`. This is the *one* place a virtual call appears in the ECS; it is never called in a per-component hot loop (only once per entity destruction), so it does not conflict with the "no virtual calls in hot paths" rule from [00-architecture-overview.md](00-architecture-overview.md).

---

## World: Tying Pools Together

### Type-Erased Pool Registry (No RTTI)

`World` (`engine/world/include/world/world.h`) owns one `ComponentPool<T>` per distinct `T` ever used with it, created lazily on first access. Since RTTI is disabled engine-wide (ADR-0002), `World` cannot ask "what's the `ComponentPool<Transform>`'s type ID" via `typeid`. Instead it uses the same trick as the reflection system (ADR-0007): a function template with a `static` local variable, which the compiler guarantees is initialized exactly once per instantiation:

```cpp
namespace detail {
    inline u64 next_component_type_id() {
        static u64 counter = 0;
        return counter++;
    }
    template <typename T>
    u64 component_type_id() {
        static const u64 id = next_component_type_id();
        return id;
    }
}
```

`component_type_id<Transform>()` and `component_type_id<RigidBodyRef>()` each get their own `static const u64 id`, assigned the next unused counter value the first time either is called — a compile-time-distinct, runtime-cheap type identifier with no RTTI involved. `World::pool<T>()` uses this ID as the key into a `HashMap<u64, void*>`, lazily constructing a new `ComponentPool<T>` (placement-`new`'d from the `World`'s allocator) the first time a given `T` is requested.

### Queries: each\<T\> and each\<A,B\>

Iterating every component of one type is a direct pass-through to the pool:

```cpp
template <typename T, typename Fn>
void each(Fn&& fn) {                // Fn: void(Entity, T&)
    pool<T>().for_each(std::forward<Fn>(fn));
}
```

Iterating entities that have *both* of two component types is a sparse-set join: walk the smaller pool's dense array (fewer entries to probe), and for each entity, check whether the larger pool also has it:

```cpp
template <typename A, typename B, typename Fn>
void each(Fn&& fn) {                // Fn: void(Entity, A&, B&)
    auto& pa = pool<A>();
    auto& pb = pool<B>();
    if (pa.size() <= pb.size()) {
        for (usize i = 0; i < pa.size(); ++i) {
            Entity e = pa.dense_entity(i);
            if (B* b = pb.get(e)) fn(e, pa.dense_value(i), *b);
        }
    } else {
        // symmetric, walking pb instead
    }
}
```

Picking the smaller pool to walk bounds the work by `min(|A|, |B|)` probes rather than `max(|A|, |B|)` — a small optimization that costs nothing to implement given both pools already expose `size()`. There is no persistent "query cache" or archetype index behind this — per ADR-0008, that is exactly the complexity sparse-set storage was chosen to avoid, and at Phase 4/5 entity counts, a linear scan plus O(1) probes is not a bottleneck.

---

## Scene: Roster, Names, and Save/Load

`World` on its own has no way to enumerate "every entity that currently exists" — it only supports create/destroy/query by component. `Scene` (`engine/world/include/world/scene.h`) wraps a `World` and adds exactly the bookkeeping that requires knowing about every live entity: a roster, a name lookup, and save/load.

### The Entity Roster

`Scene` is the *sole* creator of entities in the `World` it owns (nothing outside `Scene::create_entity()` calls `world().create_entity()` directly), so `Scene`'s own `Array<Entity> entities_` — appended to on every `create_entity()` call — is exactly the "every live entity" roster, with no separate enumeration API needed on `World` itself:

```cpp
Entity Scene::create_entity(const char* name) {
    Entity e = world_.create_entity();
    entities_->push(e);
    if (name != nullptr) names_->set(string_id(name), e);
    return e;
}
```

`destroy_entity()` removes the entity from `World` (which cleans it out of every component pool via `IComponentPool::remove_entity()`) and then swap-and-pops it out of the roster array — the same removal pattern `ComponentPool` uses, for the same reason.

### The Name Registry

Entities can optionally be looked up by name — useful for a hand-authored scene with a handful of well-known objects ("player", "sun") rather than hundreds of anonymous ones:

```cpp
Entity Scene::find(StringId name) const {
    if (Entity* e = names_->get(name)) return *e;
    return {};   // default-constructed Entity: is_valid() == false
}
```

`names_` is a `HashMap<StringId, Entity>` (Phase 2's `StringId`/FNV-1a hashing, see `05-core-systems.md`). Passing `nullptr` as the name to `create_entity()` skips registration entirely — most entities in a larger scene would be anonymous, found only through component queries.

### Save/Load Through Reflection

`Scene::save()`/`load()` round-trip every roster entity's `Transform` through Phase 2's binary serialization, driven by the reflection system built in the same phase:

```cpp
const Transform* t = world_.get_component<Transform>(e);
w.write_u8(t != nullptr ? 1 : 0);
if (t != nullptr)
    serialize(w, t, *Transform::ge_type_info());
```

`Transform::ge_type_info()` comes from the `GE_REFLECT_BEGIN`/`GE_FIELD`/`GE_REFLECT_END` macros on the `Transform` struct itself (see `05-core-systems.md`'s reflection section) — `serialize()` walks that field list generically, so `Scene` never has to know `Transform`'s exact layout. Loading is the mirror image: read the entity count, then for each one, create a fresh entity, restore its name (if any) into the registry, and `deserialize()` a `Transform` back onto it if the saved data had one.

Only `Transform` is serialized today — Phase 4 registered exactly one component type this way, not because the mechanism is `Transform`-specific (it is entirely generic over any reflected type), but because `Transform` was the only component whose persistence actually mattered for that phase's scope. Extending `save()`/`load()` to a general "serialize every reflected component on every entity" registry is realistic future work, deferred until a phase actually needs to persist more than position/rotation/scale — the same no-speculative-generality reasoning applied throughout the engine.

---

## Baseline Components

Two components shipped with the ECS itself, plus one added in Phase 5:

```cpp
struct Transform {
    vec3 position = {0, 0, 0};
    quat rotation = quat_identity();
    vec3 scale    = {1, 1, 1};
    GE_REFLECT_BEGIN(Transform)
        GE_FIELD(position, Vec3)
        GE_FIELD(rotation, Quat)
        GE_FIELD(scale, Vec3)
    GE_REFLECT_END(Transform)
};

struct CharacterController {
    f32 move_speed       = 3.0f;
    f32 look_sensitivity = 0.003f;
    f32 yaw              = 0.0f;   // accumulated directly from mouse-look input
    f32 pitch            = 0.0f;
};

// Phase 5 addition — see 09-physics-engine.md
struct RigidBodyRef {
    u32 body_id = kInvalidBodyId;  // opaque index into a physics::PhysicsWorld
};
```

`CharacterController`'s `yaw`/`pitch` are stored here rather than derived from `Transform::rotation`, because the movement system accumulates them directly from raw mouse deltas every frame (exactly like `gfx::Camera` does — see `06-graphics-and-rendering.md`) — extracting yaw/pitch back out of a quaternion every frame just to add a small delta and re-encode it would be strictly more work for the same result.

`RigidBodyRef` is deliberately *not* defined in terms of any `ge::physics` type (see [09-physics-engine.md](09-physics-engine.md) for the full rationale) — it is a bare `u32`, so `ge_world` never gains a dependency on `ge_physics` just to describe "this entity has a rigid body somewhere."

---

## ge_input: Decoupling Gameplay from Raw Keys

`engine/input/` (`ge_input`, Layer 3) sits between the platform's raw key polling (`platform::Window::key_down(KeyCode)`) and gameplay code. Without it, a movement system would hard-code `KeyCode::W`; changing a keybinding later would mean hunting down every place that checks a specific key.

### Actions and Axes

`InputMap` supports two binding kinds:

```cpp
struct ActionBinding { StringId name; platform::KeyCode key; };                          // digital: down/up
struct AxisBinding   { StringId name; platform::KeyCode positive, negative; };            // analog: -1..1
```

Gameplay code binds names once at startup:

```cpp
input_map.bind_axis(string_id("MoveForward"), platform::KeyCode::W, platform::KeyCode::S);
input_map.bind_action(string_id("Quit"), platform::KeyCode::Escape);
```

...then queries by name every frame:

```cpp
f32  move_fwd = input_map.axis(string_id("MoveForward"));   // W held: +1, S held: -1, both/neither: 0
bool quit     = input_map.action_down(string_id("Quit"));
```

An axis is just `(positive_key_down ? 1 : 0) - (negative_key_down ? 1 : 0)` — simple enough that MyEngine does not (yet) support blending multiple keys per axis or analog input devices, matching the "no speculative generality" rule: the sandbox's current bindings don't need it.

### resolve() and Testability

`InputMap::resolve()` is a template over the key source rather than being hard-wired to `platform::Window`:

```cpp
template <typename KeySource>
void resolve(const KeySource& keys) {   // any type with bool key_down(KeyCode) const
    for (...) action_state_[i] = keys.key_down(actions_[i].key);
    for (...) axis_value_[i]   = (keys.key_down(axes_[i].positive) ? 1.f : 0.f)
                                - (keys.key_down(axes_[i].negative) ? 1.f : 0.f);
}
```

In the running sandbox, `KeySource` is `platform::Window`. In `ge_input`'s unit tests, it is a small fake struct with a hand-set `key_down()` — so the binding-resolution *logic* (which key maps to which named axis, sign conventions, etc.) is fully testable without ever creating a real OS window. This is the same "build to be testable" principle from `docs/testing.md` (ADR-0003) showing up at the API-design level: a template parameter instead of a concrete dependency turns an untestable OS-coupled function into a trivially testable one, for free.

---

## The Scripting/Iteration Model: Plain C++ Systems

### Options Considered

ADR-0009 weighed three ways to write gameplay logic on top of the ECS:

- **Embedded Lua** (e.g. via sol2) — fast iteration (edit a script, see the result without recompiling), mature, but adds a runtime and a binding layer generated off the reflection system, plus a third-party dependency, for a capability not yet proven necessary.
- **A custom bytecode VM** — full control, but a large effort easy to over-invest in before there is enough gameplay logic to justify it.
- **Hot-reloadable C++ gameplay DLLs** — native speed, no second language, but real complexity: state has to survive a reload safely (vtables, static data, any in-flight component pointers), which is its own hard problem.

MyEngine picked the simplest option that unblocks the current phase: **plain compiled C++ systems** — free functions taking a `World&` (or `Scene&`) and whatever explicit inputs they need, called directly from the game loop. No scheduler, no dependency graph, no scripting binding layer. This is deliberately the least you could build; the other two options remain available once gameplay code churns often enough that edit-compile-relink actually becomes painful, which has not happened yet.

### What a System Looks Like

`system_move_player` (Phase 4, later renamed `system_move_player_physics` in Phase 5 — see [07-application-and-game-loop.md](07-application-and-game-loop.md)) is the reference example:

```cpp
void system_move_player(world::World& w, world::Entity player,
                         f32 move_fwd, f32 move_right, f32 move_up,
                         f32 mouse_dx, f32 mouse_dy, f32 dt) {
    auto* t  = w.get_component<world::Transform>(player);
    auto* cc = w.get_component<world::CharacterController>(player);
    if (t == nullptr || cc == nullptr) return;   // missing components: no-op, not a crash

    // ... update cc->yaw/pitch from mouse, compute a movement vector ...
    t->position = t->position + velocity * (cc->move_speed * dt);
    t->rotation = quat_from_axis_angle({0, 1, 0}, cc->yaw);
}
```

A system is nothing more than: look up the components it needs by entity (or iterate them via `each<T>`/`each<A,B>`), guard against absence, and read/write the data in place. There is no base class to inherit, no registration step, no lifecycle hooks — the game loop simply calls the function each fixed step (see `07-application-and-game-loop.md`'s Frame Lifecycle section). This is what "data-oriented, no virtual dispatch, explicit data flow" (from `00-architecture-overview.md`) looks like once it reaches the gameplay layer: a system is just a function, and the data it operates on is just structs in arrays.
