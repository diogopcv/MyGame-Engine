#pragma once
#include "core/types.h"
#include "core/string_id.h"

/**
 * @file reflect.h
 * @brief Lightweight compile-time reflection system (Layer 1 — Core).
 *
 * Provides TypeInfo / FieldInfo metadata for structs annotated with the
 * `GE_REFLECT_*` macros.  Used by the serialization system and (later) by
 * the editor inspector.  See ADR-0007 for design rationale.
 */
namespace ge {

/** @brief Supported field types for the reflection system. */
enum class FieldType : u8 {
    Bool, U8, U16, U32, U64, I8, I16, I32, I64, F32, F64,
    Vec2, Vec3, Vec4, Mat4, Quat, SId,
    Struct, ///< Nested reflected struct.
};

struct TypeInfo;

/**
 * @brief Metadata for a single reflected field within a struct.
 */
struct FieldInfo {
    StringId        name;   ///< Hashed field name.
    FieldType       type;   ///< Primitive or nested struct type tag.
    usize           offset; ///< Byte offset from the start of the owning struct.
    usize           size;   ///< Size of the field in bytes.
    const TypeInfo* nested; ///< TypeInfo for nested structs, or `nullptr`.
};

/**
 * @brief Metadata for a reflected struct type.
 */
struct TypeInfo {
    StringId         name;        ///< Hashed type name.
    usize            size;        ///< `sizeof(Type)`.
    usize            alignment;   ///< `alignof(Type)`.
    u32              field_count; ///< Number of reflected fields.
    const FieldInfo* fields;      ///< Pointer to the field descriptor array.
    u32              version;     ///< Schema version (for forward/backward compat).
};

/**
 * @brief Register a TypeInfo descriptor in the global type registry.
 * @param info Pointer to a static TypeInfo (must outlive the registry).
 */
void type_register(const TypeInfo* info);

/**
 * @brief Look up a registered type by its hashed name.
 * @param name StringId of the type name.
 * @return Pointer to the TypeInfo, or `nullptr` if not found.
 */
const TypeInfo* type_find(StringId name);

} // namespace ge

/**
 * @brief Begin a reflection block for @p Type.
 *
 * Place inside the struct body.  Follow with GE_FIELD / GE_FIELD_STRUCT
 * entries and close with GE_REFLECT_END.
 */
#define GE_REFLECT_BEGIN(Type)                                                  \
    static const ::ge::TypeInfo* ge_type_info() {                              \
        using Self_ = Type;                                                    \
        static const ::ge::FieldInfo fields_[] = {

/**
 * @brief Declare a primitive reflected field.
 * @param field Name of the member variable.
 * @param ftype FieldType enum value (e.g. `F32`, `U32`).
 */
#define GE_FIELD(field, ftype)                                                 \
            { ::ge::string_id(#field), ::ge::FieldType::ftype,                 \
              offsetof(Self_, field), sizeof(Self_::field), nullptr },

/**
 * @brief Declare a nested struct reflected field.
 * @param field      Name of the member variable.
 * @param NestedType The nested struct type (must also be reflected).
 */
#define GE_FIELD_STRUCT(field, NestedType)                                     \
            { ::ge::string_id(#field), ::ge::FieldType::Struct,                \
              offsetof(Self_, field), sizeof(Self_::field),                     \
              NestedType::ge_type_info() },

/** @brief Close a reflection block opened by GE_REFLECT_BEGIN. */
#define GE_REFLECT_END(Type)                                                   \
        };                                                                     \
        static const ::ge::TypeInfo info_ {                                    \
            ::ge::string_id(#Type), sizeof(Type), alignof(Type),               \
            static_cast<::ge::u32>(sizeof(fields_) / sizeof(fields_[0])),      \
            fields_, 1                                                         \
        };                                                                     \
        return &info_;                                                         \
    }
