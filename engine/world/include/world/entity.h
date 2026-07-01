#pragma once
#include "core/handle.h"

/**
 * @file entity.h
 * @brief Entity handle type (Layer 4 — Frameworks).
 *
 * An Entity is a generational index (see ADR-0008): index + generation,
 * reusing core/handle.h's Handle<Tag>. Entity *lifecycle* (create/destroy/
 * is_alive) reuses HandleTable<u8, EntityTag> — the stored byte is unused;
 * we only want its already-tested free-list + generation-check machinery.
 */
namespace ge::world {

/** @brief Phantom tag distinguishing Entity handles from other Handle<Tag> uses. */
struct EntityTag {};

/** @brief Generational entity identifier (index + generation). */
using Entity = ge::Handle<EntityTag>;

/** @brief Entity lifecycle allocator: free-list + generation counters, no payload. */
using EntityAllocator = ge::HandleTable<ge::u8, EntityTag>;

} // namespace ge::world
