#pragma once
#include "core/types.h"
#include "core/allocator.h"
#include "core/assert.h"
#include <new>
#include <utility>

namespace ge {

template <typename T>
class Array {
public:
    explicit Array(Allocator alloc) : alloc_(alloc) {}

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    Array(Array&& o) noexcept
        : alloc_(o.alloc_), data_(o.data_), size_(o.size_), capacity_(o.capacity_) {
        o.data_ = nullptr;
        o.size_ = 0;
        o.capacity_ = 0;
    }

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

    ~Array() {
        clear();
        free_storage();
    }

    void push(const T& value) {
        ensure_capacity(size_ + 1);
        new (data_ + size_) T(value);
        ++size_;
    }

    void push(T&& value) {
        ensure_capacity(size_ + 1);
        new (data_ + size_) T(std::move(value));
        ++size_;
    }

    void pop() {
        GE_ASSERT(size_ > 0);
        --size_;
        data_[size_].~T();
    }

    T& operator[](usize i) {
        GE_ASSERT(i < size_);
        return data_[i];
    }

    const T& operator[](usize i) const {
        GE_ASSERT(i < size_);
        return data_[i];
    }

    void reserve(usize new_cap) {
        if (new_cap > capacity_) grow(new_cap);
    }

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

    void clear() {
        for (usize i = 0; i < size_; ++i)
            data_[i].~T();
        size_ = 0;
    }

    usize size()     const { return size_; }
    usize capacity() const { return capacity_; }
    bool  empty()    const { return size_ == 0; }

    T*       data()       { return data_; }
    const T* data() const { return data_; }
    T*       begin()       { return data_; }
    T*       end()         { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end()   const { return data_ + size_; }

    T&       back()       { GE_ASSERT(size_ > 0); return data_[size_ - 1]; }
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
