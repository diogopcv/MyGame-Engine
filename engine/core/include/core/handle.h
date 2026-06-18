#pragma once
#include "core/types.h"
#include "core/allocator.h"
#include "core/assert.h"
#include <new>

namespace ge {

template <typename Tag>
struct Handle {
    u32 index      = ~u32(0);
    u32 generation = 0;

    bool is_valid() const { return index != ~u32(0); }
    bool operator==(Handle o) const { return index == o.index && generation == o.generation; }
    bool operator!=(Handle o) const { return !(*this == o); }
};

template <typename T, typename Tag = T>
class HandleTable {
public:
    void init(u32 capacity, Allocator alloc) {
        alloc_      = alloc;
        capacity_   = capacity;
        count_      = 0;
        free_count_ = capacity;

        slots_ = static_cast<Slot*>(alloc_.alloc(sizeof(Slot) * capacity, alignof(Slot)));
        free_list_ = static_cast<u32*>(alloc_.alloc(sizeof(u32) * capacity, alignof(u32)));

        for (u32 i = 0; i < capacity; ++i) {
            slots_[i].generation = 0;
            slots_[i].alive = false;
            free_list_[i] = capacity - 1 - i;
        }
    }

    void shutdown() {
        for (u32 i = 0; i < capacity_; ++i) {
            if (slots_[i].alive)
                slots_[i].data.~T();
        }
        alloc_.free(free_list_, sizeof(u32) * capacity_);
        alloc_.free(slots_, sizeof(Slot) * capacity_);
        slots_     = nullptr;
        free_list_ = nullptr;
        capacity_  = 0;
        count_     = 0;
        free_count_= 0;
    }

    Handle<Tag> add(const T& item) {
        GE_ASSERT(free_count_ > 0);
        u32 idx = free_list_[--free_count_];
        new (&slots_[idx].data) T(item);
        slots_[idx].alive = true;
        ++count_;
        return { idx, slots_[idx].generation };
    }

    void remove(Handle<Tag> h) {
        GE_ASSERT(h.is_valid());
        GE_ASSERT(h.index < capacity_);
        Slot& s = slots_[h.index];
        GE_ASSERT(s.alive && s.generation == h.generation);
        s.data.~T();
        s.alive = false;
        ++s.generation;
        free_list_[free_count_++] = h.index;
        --count_;
    }

    T* get(Handle<Tag> h) {
        if (!h.is_valid() || h.index >= capacity_) return nullptr;
        Slot& s = slots_[h.index];
        if (!s.alive || s.generation != h.generation) return nullptr;
        return &s.data;
    }

    const T* get(Handle<Tag> h) const {
        return const_cast<HandleTable*>(this)->get(h);
    }

    u32 count()    const { return count_; }
    u32 capacity() const { return capacity_; }

private:
    struct Slot {
        T    data;
        u32  generation;
        bool alive;
    };

    Allocator alloc_;
    Slot* slots_      = nullptr;
    u32*  free_list_  = nullptr;
    u32   capacity_   = 0;
    u32   count_      = 0;
    u32   free_count_ = 0;
};

} // namespace ge
