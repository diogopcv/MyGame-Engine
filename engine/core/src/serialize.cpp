#include "core/serialize.h"
#include "core/assert.h"
#include <cstring>

namespace ge {

// --- BinaryWriter ---

void BinaryWriter::write_bytes(const void* data, usize size) {
    const u8* p = static_cast<const u8*>(data);
    for (usize i = 0; i < size; ++i) buf_.push(p[i]);
}

void BinaryWriter::write_u8(u8 v)   { write_bytes(&v, 1); }
void BinaryWriter::write_u32(u32 v) { write_bytes(&v, 4); }
void BinaryWriter::write_u64(u64 v) { write_bytes(&v, 8); }
void BinaryWriter::write_f32(f32 v) { write_bytes(&v, 4); }
void BinaryWriter::write_f64(f64 v) { write_bytes(&v, 8); }

// --- BinaryReader ---

bool BinaryReader::read_bytes(void* out, usize size) {
    if (offset_ + size > size_) return false;
    std::memcpy(out, data_ + offset_, size);
    offset_ += size;
    return true;
}

bool BinaryReader::read_u8(u8& v)   { return read_bytes(&v, 1); }
bool BinaryReader::read_u32(u32& v) { return read_bytes(&v, 4); }
bool BinaryReader::read_u64(u64& v) { return read_bytes(&v, 8); }
bool BinaryReader::read_f32(f32& v) { return read_bytes(&v, 4); }
bool BinaryReader::read_f64(f64& v) { return read_bytes(&v, 8); }

// --- Reflection-driven serialization ---

namespace {
    usize field_primitive_size(FieldType t) {
        switch (t) {
            case FieldType::Bool: return 1;
            case FieldType::U8:  case FieldType::I8:  return 1;
            case FieldType::U16: case FieldType::I16: return 2;
            case FieldType::U32: case FieldType::I32: case FieldType::F32: return 4;
            case FieldType::U64: case FieldType::I64: case FieldType::F64: return 8;
            case FieldType::SId: return 8;
            case FieldType::Vec2: return 8;
            case FieldType::Vec3: return 12;
            case FieldType::Vec4: return 16;
            case FieldType::Mat4: return 64;
            case FieldType::Quat: return 16;
            case FieldType::Struct: return 0;
        }
        return 0;
    }
}

void serialize(BinaryWriter& w, const void* data, const TypeInfo& type) {
    w.write_u32(type.version);
    w.write_u32(type.field_count);

    for (u32 i = 0; i < type.field_count; ++i) {
        const FieldInfo& f = type.fields[i];
        const void* field_ptr = static_cast<const u8*>(data) + f.offset;

        if (f.type == FieldType::Struct) {
            GE_ASSERT(f.nested != nullptr);
            serialize(w, field_ptr, *f.nested);
        } else {
            usize sz = field_primitive_size(f.type);
            w.write_bytes(field_ptr, sz);
        }
    }
}

bool deserialize(BinaryReader& r, void* data, const TypeInfo& type) {
    u32 version = 0, field_count = 0;
    if (!r.read_u32(version)) return false;
    if (!r.read_u32(field_count)) return false;

    if (version != type.version || field_count != type.field_count)
        return false;

    for (u32 i = 0; i < type.field_count; ++i) {
        const FieldInfo& f = type.fields[i];
        void* field_ptr = static_cast<u8*>(data) + f.offset;

        if (f.type == FieldType::Struct) {
            GE_ASSERT(f.nested != nullptr);
            if (!deserialize(r, field_ptr, *f.nested)) return false;
        } else {
            usize sz = field_primitive_size(f.type);
            if (!r.read_bytes(field_ptr, sz)) return false;
        }
    }
    return true;
}

} // namespace ge
