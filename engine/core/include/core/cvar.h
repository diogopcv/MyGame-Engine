#pragma once
#include "core/types.h"
#include "core/string_id.h"

/**
 * @file cvar.h
 * @brief Console variable (CVar) system (Layer 1 — Core).
 *
 * Provides a flat registry of named configuration variables that can be
 * queried and modified at runtime.  Supports integer, float, and boolean
 * types.  CVars are looked up by StringId for O(1) name resolution.
 */
namespace ge {

/** @brief Supported CVar value types. */
enum class CVarType : u8 {
    Int,   ///< 32-bit signed integer.
    Float, ///< 32-bit float.
    Bool   ///< Boolean.
};

/**
 * @brief A single console variable entry.
 */
struct CVar {
    StringId    name;        ///< Hashed name for lookup.
    CVarType    type;        ///< Value type tag.
    union {
        i32  i;              ///< Integer value (when type == Int).
        f32  f;              ///< Float value   (when type == Float).
        bool b;              ///< Bool value    (when type == Bool).
    };
    const char* description; ///< Human-readable description.
};

/**
 * @brief Global CVar registry and command executor.
 */
class CVarSystem {
public:
    /** @brief Initialise the CVar system. */
    void init();
    /** @brief Shut down and clear all registered CVars. */
    void shutdown();

    /**
     * @brief Register an integer CVar.
     * @param name        Name string (hashed internally).
     * @param default_val Initial value.
     * @param desc        Human-readable description.
     * @return Pointer to the registered CVar.
     */
    CVar* register_int(const char* name, i32 default_val, const char* desc);

    /**
     * @brief Register a float CVar.
     * @param name        Name string (hashed internally).
     * @param default_val Initial value.
     * @param desc        Human-readable description.
     * @return Pointer to the registered CVar.
     */
    CVar* register_float(const char* name, f32 default_val, const char* desc);

    /**
     * @brief Register a boolean CVar.
     * @param name        Name string (hashed internally).
     * @param default_val Initial value.
     * @param desc        Human-readable description.
     * @return Pointer to the registered CVar.
     */
    CVar* register_bool(const char* name, bool default_val, const char* desc);

    /**
     * @brief Find a CVar by its hashed name.
     * @param name StringId to search for.
     * @return Pointer to the CVar, or `nullptr` if not found.
     */
    CVar* find(StringId name);

    /**
     * @brief Find a CVar by its string name.
     * @param name Null-terminated name.
     * @return Pointer to the CVar, or `nullptr` if not found.
     */
    CVar* find(const char* name);

    /**
     * @brief Execute a console command string (e.g. `"r_vsync 1"`).
     * @param cmd Null-terminated command.
     * @return `true` if the command was parsed and applied successfully.
     */
    bool execute(const char* cmd);

    /**
     * @brief Iterate over all registered CVars.
     * @tparam Fn Callable with signature `void(const CVar&)`.
     * @param  fn Function to invoke for each CVar.
     */
    template <typename Fn>
    void for_each(Fn&& fn) const;

private:
    static constexpr u32 kMaxCVars = 256;
    CVar  cvars_[kMaxCVars];
    u32   count_ = 0;

    CVar* find_internal(StringId id);
};

template <typename Fn>
void CVarSystem::for_each(Fn&& fn) const {
    for (u32 i = 0; i < count_; ++i)
        fn(cvars_[i]);
}

/** @brief Access the global CVarSystem singleton. */
CVarSystem& cvar_system();

} // namespace ge
