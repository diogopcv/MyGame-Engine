#pragma once
#include "core/types.h"
#include "core/allocator.h"
#include "core/array.h"
#include "core/reflect.h"

namespace ge {

class BinaryWriter {
public:
    explicit BinaryWriter(Allocator alloc) : buf_(alloc) {}

    void write_bytes(const void* data, usize size);
    void write_u8(u8 v);
    void write_u32(u32 v);
    void write_u64(u64 v);
    void write_f32(f32 v);
    void write_f64(f64 v);

    const u8* data() const { return buf_.data(); }
    usize     size() const { return buf_.size(); }

private:
    Array<u8> buf_;
};

class BinaryReader {
public:
    BinaryReader(const u8* data, usize size) : data_(data), size_(size) {}

    bool read_bytes(void* out, usize size);
    bool read_u8(u8& v);
    bool read_u32(u32& v);
    bool read_u64(u64& v);
    bool read_f32(f32& v);
    bool read_f64(f64& v);

    usize offset() const { return offset_; }
    usize remaining() const { return size_ - offset_; }

private:
    const u8* data_;
    usize     size_;
    usize     offset_ = 0;
};

void serialize(BinaryWriter& w, const void* data, const TypeInfo& type);
bool deserialize(BinaryReader& r, void* data, const TypeInfo& type);

} // namespace ge
