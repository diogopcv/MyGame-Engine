#include "core/cvar.h"
#include "core/assert.h"
#include "core/log.h"
#include <cstring>
#include <cstdlib>

namespace ge {

CVarSystem& cvar_system() {
    static CVarSystem s;
    return s;
}

void CVarSystem::init() {
    count_ = 0;
}

void CVarSystem::shutdown() {
    count_ = 0;
}

CVar* CVarSystem::find_internal(StringId id) {
    for (u32 i = 0; i < count_; ++i) {
        if (cvars_[i].name == id) return &cvars_[i];
    }
    return nullptr;
}

CVar* CVarSystem::find(StringId name) {
    return find_internal(name);
}

CVar* CVarSystem::find(const char* name) {
    return find_internal(string_id(name));
}

CVar* CVarSystem::register_int(const char* name, i32 default_val, const char* desc) {
    GE_ASSERT(count_ < kMaxCVars);
    CVar& cv = cvars_[count_++];
    cv.name = string_id(name);
    cv.type = CVarType::Int;
    cv.i    = default_val;
    cv.description = desc;
#ifndef NDEBUG
    string_id_register(cv.name, name);
#endif
    return &cv;
}

CVar* CVarSystem::register_float(const char* name, f32 default_val, const char* desc) {
    GE_ASSERT(count_ < kMaxCVars);
    CVar& cv = cvars_[count_++];
    cv.name = string_id(name);
    cv.type = CVarType::Float;
    cv.f    = default_val;
    cv.description = desc;
#ifndef NDEBUG
    string_id_register(cv.name, name);
#endif
    return &cv;
}

CVar* CVarSystem::register_bool(const char* name, bool default_val, const char* desc) {
    GE_ASSERT(count_ < kMaxCVars);
    CVar& cv = cvars_[count_++];
    cv.name = string_id(name);
    cv.type = CVarType::Bool;
    cv.b    = default_val;
    cv.description = desc;
#ifndef NDEBUG
    string_id_register(cv.name, name);
#endif
    return &cv;
}

bool CVarSystem::execute(const char* cmd) {
    char name_buf[128];
    char val_buf[128];

    const char* space = std::strchr(cmd, ' ');
    if (!space) return false;

    usize name_len = static_cast<usize>(space - cmd);
    if (name_len >= sizeof(name_buf)) return false;
    std::memcpy(name_buf, cmd, name_len);
    name_buf[name_len] = '\0';

    const char* val_start = space + 1;
    while (*val_start == ' ') ++val_start;
    usize val_len = std::strlen(val_start);
    if (val_len >= sizeof(val_buf)) return false;
    std::memcpy(val_buf, val_start, val_len + 1);

    CVar* cv = find(name_buf);
    if (!cv) {
        GE_WARN("CVar '{}' not found", name_buf);
        return false;
    }

    switch (cv->type) {
        case CVarType::Int:
            cv->i = std::atoi(val_buf);
            break;
        case CVarType::Float:
            cv->f = static_cast<f32>(std::atof(val_buf));
            break;
        case CVarType::Bool:
            cv->b = (std::strcmp(val_buf, "1") == 0 ||
                     std::strcmp(val_buf, "true") == 0);
            break;
    }
    return true;
}

} // namespace ge
