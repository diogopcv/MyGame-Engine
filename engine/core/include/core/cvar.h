#pragma once
#include "core/types.h"
#include "core/string_id.h"

namespace ge {

enum class CVarType : u8 { Int, Float, Bool };

struct CVar {
    StringId    name;
    CVarType    type;
    union {
        i32  i;
        f32  f;
        bool b;
    };
    const char* description;
};

class CVarSystem {
public:
    void init();
    void shutdown();

    CVar* register_int(const char* name, i32 default_val, const char* desc);
    CVar* register_float(const char* name, f32 default_val, const char* desc);
    CVar* register_bool(const char* name, bool default_val, const char* desc);

    CVar* find(StringId name);
    CVar* find(const char* name);

    bool execute(const char* cmd);

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

CVarSystem& cvar_system();

} // namespace ge
