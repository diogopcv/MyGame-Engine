#pragma once
#include "core/types.h"
#include "core/allocator.h"
#include "core/assert.h"
#include <new>
#include <utility>

/**
 * @file hash_map.h
 * @brief Open-addressing hash map backed by an explicit Allocator (Layer 1 — Core).
 *
 * Uses linear probing with a 75 % load-factor threshold.  Keys are hashed via
 * FNV-1a (generic) or integer-specific bit-mixing specialisations.
 */
namespace ge {

/** @brief Internal hashing utilities. */
namespace detail {
    /**
     * @brief FNV-1a hash over a raw byte range.
     * @param data Pointer to the bytes.
     * @param len  Number of bytes.
     * @return 64-bit hash value.
     */
    inline u64 hash_bytes(const void* data, usize len) {
        const u8* p = static_cast<const u8*>(data);
        u64 h = 14695981039346656037ull;
        for (usize i = 0; i < len; ++i) {
            h ^= static_cast<u64>(p[i]);
            h *= 1099511628211ull;
        }
        return h;
    }

    /** @brief Hash an arbitrary key by hashing its raw bytes. */
    template <typename K> u64 hash_key(const K& key) {
        return hash_bytes(&key, sizeof(K));
    }

    /** @brief Integer bit-mixing specialisation for u32 keys. */
    template <> inline u64 hash_key<u32>(const u32& key) {
        u64 h = key;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = (h >> 16) ^ h;
        return h;
    }

    /** @brief Integer bit-mixing specialisation for u64 keys. */
    template <> inline u64 hash_key<u64>(const u64& key) {
        u64 h = key;
        h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
        h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
        h = h ^ (h >> 31);
        return h;
    }

    /** @brief Integer bit-mixing specialisation for i32 keys (delegates to u32). */
    template <> inline u64 hash_key<i32>(const i32& key) {
        return hash_key(static_cast<u32>(key));
    }
}

/**
 * @brief Open-addressing hash map with linear probing.
 * @tparam K Key type (must support `==` and be hashable via detail::hash_key).
 * @tparam V Value type.
 *
 * Non-copyable.  Grows automatically when the load factor exceeds 75 %.
 * All memory is obtained through the Allocator supplied at construction.
 */
template <typename K, typename V>
class HashMap {
public:
    /**
     * @brief Construct an empty map using the given allocator.
     * @param alloc Allocator for slot storage.
     */
    explicit HashMap(Allocator alloc) : alloc_(alloc) {}

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    /** @brief Destructor. Destroys all entries and frees slot storage. */
    ~HashMap() {
        clear();
        free_storage();
    }

    /**
     * @brief Insert or update a key-value pair.
     * @param key   Key to insert or look up.
     * @param value Value to associate with @p key.
     */
    void set(const K& key, const V& value) {
        // Grow on live + tombstone load so a probe chain always ends in an
        // empty slot (bounds probe length and prevents an all-occupied table).
        if ((count_ + tombstones_) * 4 >= capacity_ * 3) grow();

        u64 h = detail::hash_key(key);
        usize idx = static_cast<usize>(h) & (capacity_ - 1);

        // Remember the first tombstone we pass, but keep probing for an
        // existing entry: the key may live further down the chain. Inserting
        // at the tombstone before confirming absence would create a duplicate.
        constexpr usize kNoSlot = static_cast<usize>(-1);
        usize tombstone = kNoSlot;

        while (true) {
            if (slots_[idx].state == State::kEmpty) {
                usize target = (tombstone != kNoSlot) ? tombstone : idx;
                new (&slots_[target].key) K(key);
                new (&slots_[target].value) V(value);
                slots_[target].state = State::kOccupied;
                if (target == tombstone) --tombstones_;
                ++count_;
                return;
            }
            if (slots_[idx].state == State::kDeleted) {
                if (tombstone == kNoSlot) tombstone = idx;
            } else if (slots_[idx].key == key) {
                slots_[idx].value = value;
                return;
            }
            idx = (idx + 1) & (capacity_ - 1);
        }
    }

    /**
     * @brief Look up a value by key.
     * @param key Key to search for.
     * @return Pointer to the value, or `nullptr` if not found.
     */
    V* get(const K& key) {
        if (capacity_ == 0) return nullptr;
        u64 h = detail::hash_key(key);
        usize idx = static_cast<usize>(h) & (capacity_ - 1);

        while (true) {
            if (slots_[idx].state == State::kEmpty) return nullptr;
            if (slots_[idx].state == State::kOccupied && slots_[idx].key == key)
                return &slots_[idx].value;
            idx = (idx + 1) & (capacity_ - 1);
        }
    }

    /** @copydoc get(const K&) */
    const V* get(const K& key) const {
        return const_cast<HashMap*>(this)->get(key);
    }

    /**
     * @brief Remove an entry by key.
     * @param key Key to remove.
     * @return `true` if the key was found and removed.
     */
    bool remove(const K& key) {
        if (capacity_ == 0) return false;
        u64 h = detail::hash_key(key);
        usize idx = static_cast<usize>(h) & (capacity_ - 1);

        while (true) {
            if (slots_[idx].state == State::kEmpty) return false;
            if (slots_[idx].state == State::kOccupied && slots_[idx].key == key) {
                slots_[idx].key.~K();
                slots_[idx].value.~V();
                slots_[idx].state = State::kDeleted;
                --count_;
                ++tombstones_;
                return true;
            }
            idx = (idx + 1) & (capacity_ - 1);
        }
    }

    /** @brief Number of entries in the map. */
    usize size()  const { return count_; }
    /** @brief Check whether the map is empty. */
    bool  empty() const { return count_ == 0; }

    /**
     * @brief Iterate over all entries (mutable).
     * @tparam Fn Callable with signature `void(const K& key, V& value)`.
     * @param  fn Function to invoke for each occupied slot.
     */
    template <typename Fn>
    void for_each(Fn&& fn) {
        for (usize i = 0; i < capacity_; ++i) {
            if (slots_[i].state == State::kOccupied)
                fn(slots_[i].key, slots_[i].value);
        }
    }

    /**
     * @brief Iterate over all entries (const).
     * @tparam Fn Callable with signature `void(const K& key, const V& value)`.
     * @param  fn Function to invoke for each occupied slot.
     */
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (usize i = 0; i < capacity_; ++i) {
            if (slots_[i].state == State::kOccupied)
                fn(slots_[i].key, slots_[i].value);
        }
    }

    /** @brief Remove all entries without releasing slot storage. */
    void clear() {
        for (usize i = 0; i < capacity_; ++i) {
            if (slots_[i].state == State::kOccupied) {
                slots_[i].key.~K();
                slots_[i].value.~V();
                slots_[i].state = State::kEmpty;
            } else {
                slots_[i].state = State::kEmpty;
            }
        }
        count_      = 0;
        tombstones_ = 0;
    }

private:
    enum class State : u8 { kEmpty = 0, kOccupied, kDeleted };

    struct Slot {
        K     key;
        V     value;
        State state = State::kEmpty;
    };

    void grow() {
        // Size from the live element count, not the old capacity: a rehash
        // triggered purely by tombstone buildup keeps the same capacity (just
        // sweeping the tombstones away), while genuine fullness still doubles.
        usize needed  = (count_ + 1) * 2;
        usize new_cap = capacity_ == 0 ? 16 : capacity_;
        while (new_cap < needed) new_cap *= 2;

        Slot* new_slots = static_cast<Slot*>(
            alloc_.alloc(sizeof(Slot) * new_cap, alignof(Slot)));
        for (usize i = 0; i < new_cap; ++i)
            new_slots[i].state = State::kEmpty;

        Slot* old_slots  = slots_;
        usize old_cap    = capacity_;

        slots_      = new_slots;
        capacity_   = new_cap;
        count_      = 0;
        tombstones_ = 0;

        for (usize i = 0; i < old_cap; ++i) {
            if (old_slots[i].state == State::kOccupied) {
                set(old_slots[i].key, old_slots[i].value);
                old_slots[i].key.~K();
                old_slots[i].value.~V();
            }
        }

        if (old_slots)
            alloc_.free(old_slots, sizeof(Slot) * old_cap);
    }

    void free_storage() {
        if (slots_) {
            alloc_.free(slots_, sizeof(Slot) * capacity_);
            slots_    = nullptr;
            capacity_ = 0;
        }
    }

    Allocator alloc_;
    Slot* slots_      = nullptr;
    usize capacity_   = 0;
    usize count_      = 0;
    usize tombstones_ = 0;
};

} // namespace ge
