#pragma once
#include "core/types.h"
#include "core/allocator.h"
#include "core/array.h"
#include "core/reflect.h"

/**
 * @file serialize.h
 * @brief Binary serialization driven by the reflection system (Layer 1 — Core).
 *
 * BinaryWriter / BinaryReader provide low-level typed I/O.  The free
 * functions serialize() / deserialize() walk a TypeInfo descriptor and
 * read/write every reflected field automatically.
 */
namespace ge {

/**
 * @brief Growable binary output stream.
 *
 * Backed by an Array<u8> obtained through an explicit Allocator.
 */
class BinaryWriter {
public:
    /**
     * @brief Construct a writer using the given allocator for its internal buffer.
     * @param alloc Allocator for the byte buffer.
     */
    explicit BinaryWriter(Allocator alloc) : buf_(alloc) {}

    /**
     * @brief Write a raw byte range.
     * @param data Pointer to source bytes.
     * @param size Number of bytes to append.
     */
    void write_bytes(const void* data, usize size);
    /** @brief Write a single unsigned 8-bit integer. */
    void write_u8(u8 v);
    /** @brief Write an unsigned 32-bit integer (little-endian). */
    void write_u32(u32 v);
    /** @brief Write an unsigned 64-bit integer (little-endian). */
    void write_u64(u64 v);
    /** @brief Write a 32-bit float (IEEE 754). */
    void write_f32(f32 v);
    /** @brief Write a 64-bit double (IEEE 754). */
    void write_f64(f64 v);

    /** @brief Pointer to the accumulated output bytes. */
    const u8* data() const { return buf_.data(); }
    /** @brief Number of bytes written so far. */
    usize     size() const { return buf_.size(); }

private:
    Array<u8> buf_;
};

/**
 * @brief Sequential binary input stream over a contiguous byte buffer.
 */
class BinaryReader {
public:
    /**
     * @brief Construct a reader over an existing byte buffer.
     * @param data Pointer to the first byte.
     * @param size Total number of bytes available.
     */
    BinaryReader(const u8* data, usize size) : data_(data), size_(size) {}

    /**
     * @brief Read @p size bytes into @p out.
     * @return `true` on success, `false` if insufficient data remains.
     */
    bool read_bytes(void* out, usize size);
    /** @brief Read an unsigned 8-bit integer. @return `false` if not enough data. */
    bool read_u8(u8& v);
    /** @brief Read an unsigned 32-bit integer. @return `false` if not enough data. */
    bool read_u32(u32& v);
    /** @brief Read an unsigned 64-bit integer. @return `false` if not enough data. */
    bool read_u64(u64& v);
    /** @brief Read a 32-bit float. @return `false` if not enough data. */
    bool read_f32(f32& v);
    /** @brief Read a 64-bit double. @return `false` if not enough data. */
    bool read_f64(f64& v);

    /** @brief Current read offset in bytes from the start of the buffer. */
    usize offset() const { return offset_; }
    /** @brief Number of unread bytes remaining. */
    usize remaining() const { return size_ - offset_; }

private:
    const u8* data_;
    usize     size_;
    usize     offset_ = 0;
};

/**
 * @brief Serialize a reflected struct into binary format.
 * @param w    Output writer.
 * @param data Pointer to the struct instance.
 * @param type TypeInfo describing the struct layout.
 */
void serialize(BinaryWriter& w, const void* data, const TypeInfo& type);

/**
 * @brief Deserialize binary data into a reflected struct.
 * @param r    Input reader.
 * @param data Pointer to the destination struct instance.
 * @param type TypeInfo describing the expected layout.
 * @return `true` on success, `false` on format or size mismatch.
 */
bool deserialize(BinaryReader& r, void* data, const TypeInfo& type);

} // namespace ge
