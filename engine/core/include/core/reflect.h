#pragma once
#include "core/types.h"
#include "core/string_id.h"

namespace ge {

enum class FieldType : u8 {
    Bool, U8, U16, U32, U64, I8, I16, I32, I64, F32, F64,
    Vec2, Vec3, Vec4, Mat4, Quat, SId,
    Struct,
};

struct TypeInfo;

struct FieldInfo {
    StringId        name;
    FieldType       type;
    usize           offset;
    usize           size;
    const TypeInfo* nested;
};

struct TypeInfo {
    StringId         name;
    usize            size;
    usize            alignment;
    u32              field_count;
    const FieldInfo* fields;
    u32              version;
};

void type_register(const TypeInfo* info);
const TypeInfo* type_find(StringId name);

} // namespace ge

#define GE_REFLECT_BEGIN(Type)                                                  \
    static const ::ge::TypeInfo* ge_type_info() {                              \
        using Self_ = Type;                                                    \
        static const ::ge::FieldInfo fields_[] = {

#define GE_FIELD(field, ftype)                                                 \
            { ::ge::string_id(#field), ::ge::FieldType::ftype,                 \
              offsetof(Self_, field), sizeof(Self_::field), nullptr },

#define GE_FIELD_STRUCT(field, NestedType)                                     \
            { ::ge::string_id(#field), ::ge::FieldType::Struct,                \
              offsetof(Self_, field), sizeof(Self_::field),                     \
              NestedType::ge_type_info() },

#define GE_REFLECT_END(Type)                                                   \
        };                                                                     \
        static const ::ge::TypeInfo info_ {                                    \
            ::ge::string_id(#Type), sizeof(Type), alignof(Type),               \
            static_cast<::ge::u32>(sizeof(fields_) / sizeof(fields_[0])),      \
            fields_, 1                                                         \
        };                                                                     \
        return &info_;                                                         \
    }
