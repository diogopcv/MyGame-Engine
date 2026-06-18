#pragma once
#include "core/types.h"
#include "core/allocator.h"
#include "core/assert.h"
#include <new>
#include <utility>

/**
 * @file array.h
 * @brief Dynamic array backed by an explicit Allocator (Layer 1 — Core).
 *
 * Move-only container similar to `std::vector` but allocating through the
 * engine's Allocator interface instead of the CRT heap.  Growth strategy
 * doubles capacity starting from 8.
 */
namespace ge {

/**
 * @brief Dynamically-growing contiguous array.
 * @tparam T Element type.
 *
 * Non-copyable, move-only.  All memory is obtained through the Allocator
 * supplied at construction time.
 */
template <typename T>
class Array {
public:
    /**
     * @brief Construct an empty array using the given allocator.
     * @param alloc Allocator to use for all storage.
     */
    explicit Array(Allocator alloc) : alloc_(alloc) {}

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    /** @brief Move constructor. Transfers ownership; leaves @p o empty. */
    Array(Array&& o) noexcept
        : alloc_(o.alloc_), data_(o.data_), size_(o.size_), capacity_(o.capacity_) {
        o.data_ = nullptr;
        o.size_ = 0;
        o.capacity_ = 0;
    }

    /** @brief Move assignment. Transfers ownership; leaves @p o empty. */
    Array& operator=(Array&& o) noexcept {
        if (this != &o) {
            clear();
            free_storage();
            alloc_    = o.alloc_;
            data_     = o.data_;
            size_     = o.size_;
            capacity_ = o.capacity_;
            o.data_ = nullptr;
            o.size_ = 0;
            o.capacity_ = 0;
        }
        return *this;
    }

    /** @brief Destructor. Destroys all elements and frees storage. */
    ~Array() {
        clear();
        free_storage();
    }

    /**
     * @brief Append an element by copy.
     * @param value Element to copy-construct at the back.
     */
    void push(const T& value) {
        ensure_capacity(size_ + 1);
        new (data_ + size_) T(value);
        ++size_;
    }

    /**
     * @brief Append an element by move.
     * @param value Element to move-construct at the back.
     */
    void push(T&& value) {
        ensure_capacity(size_ + 1);
        new (data_ + size_) T(std::move(value));
        ++size_;
    }

    /** @brief Remove and destroy the last element. Asserts non-empty. */
    void pop() {
        GE_ASSERT(size_ > 0);
        --size_;
        data_[size_].~T();
    }

    /**
     * @brief Indexed element access (mutable). Asserts bounds.
     * @param i Zero-based index.
     */
    T& operator[](usize i) {
        GE_ASSERT(i < size_);
        return data_[i];
    }

    /**
     * @brief Indexed element access (const). Asserts bounds.
     * @param i Zero-based index.
     */
    const T& operator[](usize i) const {
        GE_ASSERT(i < size_);
        return data_[i];
    }

    /**
     * @brief Pre-allocate storage for at least @p new_cap elements.
     * @param new_cap Desired minimum capacity.
     */
    void reserve(usize new_cap) {
        if (new_cap > capacity_) grow(new_cap);
    }

    /**
     * @brief Resize the array to @p new_size elements.
     *
     * New elements are value-initialised; excess elements are destroyed.
     * @param new_size Desired element count.
     */
    void resize(usize new_size) {
        if (new_size > size_) {
            ensure_capacity(new_size);
            for (usize i = size_; i < new_size; ++i)
                new (data_ + i) T{};
        } else {
            for (usize i = new_size; i < size_; ++i)
                data_[i].~T();
        }
        size_ = new_size;
    }

    /** @brief Destroy all elements without releasing storage. */
    void clear() {
        for (usize i = 0; i < size_; ++i)
            data_[i].~T();
        size_ = 0;
    }

    /** @brief Number of elements. */
    usize size()     const { return size_; }
    /** @brief Current storage capacity in elements. */
    usize capacity() const { return capacity_; }
    /** @brief Check whether the array is empty. */
    bool  empty()    const { return size_ == 0; }

    /** @brief Pointer to the underlying contiguous storage (mutable). */
    T*       data()       { return data_; }
    /** @brief Pointer to the underlying contiguous storage (const). */
    const T* data() const { return data_; }
    /** @brief Iterator to the first element. */
    T*       begin()       { return data_; }
    /** @brief Iterator past the last element. */
    T*       end()         { return data_ + size_; }
    /** @brief Const iterator to the first element. */
    const T* begin() const { return data_; }
    /** @brief Const iterator past the last element. */
    const T* end()   const { return data_ + size_; }

    /** @brief Reference to the last element (mutable). Asserts non-empty. */
    T&       back()       { GE_ASSERT(size_ > 0); return data_[size_ - 1]; }
    /** @brief Reference to the last element (const). Asserts non-empty. */
    const T& back() const { GE_ASSERT(size_ > 0); return data_[size_ - 1]; }

private:
    void ensure_capacity(usize needed) {
        if (needed > capacity_) {
            usize new_cap = capacity_ == 0 ? 8 : capacity_ * 2;
            while (new_cap < needed) new_cap *= 2;
            grow(new_cap);
        }
    }

    void grow(usize new_cap) {
        T* new_data = static_cast<T*>(alloc_.alloc(sizeof(T) * new_cap, alignof(T)));
        for (usize i = 0; i < size_; ++i) {
            new (new_data + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        free_storage();
        data_     = new_data;
        capacity_ = new_cap;
    }

    void free_storage() {
        if (data_) {
            alloc_.free(data_, sizeof(T) * capacity_);
            data_ = nullptr;
            capacity_ = 0;
        }
    }

    Allocator alloc_;
    T*    data_     = nullptr;
    usize size_     = 0;
    usize capacity_ = 0;
};

} // namespace ge
