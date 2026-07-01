#pragma once
#include "core/allocator.h"
#include "core/array.h"
#include "core/hash_map.h"
#include "core/string_id.h"
#include "world/world.h"
#include "world/components.h"

/**
 * @file scene.h
 * @brief A World plus a named-entity registry and save/load (Layer 4 — Frameworks).
 *
 * Scene is the sole creator of entities in the World it owns, so its own
 * entity roster is exactly "every live entity" — no enumeration API needs to
 * be added to World itself for this. See docs/phases/04-world-gameplay.md.
 */
namespace ge::world {

/**
 * @brief Owns a World, a name -> Entity lookup, and binary save/load.
 *
 * Phase 4 registers exactly one serialized component type (Transform) — see
 * the design doc for why this isn't a generic component-type registry yet.
 */
class Scene {
public:
    /**
     * @brief Prepare the underlying World and roster storage.
     * @param max_entities Maximum number of live entities.
     * @param alloc        Allocator for all Scene/World storage.
     */
    void init(u32 max_entities, Allocator alloc);

    /** @brief Destroy every entity and release all storage. */
    void shutdown();

    /**
     * @brief Create a new entity, optionally registering it under a lookup name.
     * @param name Optional name for find(); pass nullptr for an anonymous entity.
     */
    Entity create_entity(const char* name = nullptr);

    /** @brief Destroy @p e and remove it from the roster/name registry. */
    void destroy_entity(Entity e);

    /** @brief Look up a named entity. @return An invalid Entity if not found. */
    Entity find(StringId name) const;

    /** @brief The underlying ECS World. */
    World&       world()       { return world_; }
    const World& world() const { return world_; }

    /**
     * @brief Serialize every roster entity's Transform (if present) to a file.
     * @return `true` on success.
     */
    bool save(const char* path) const;

    /**
     * @brief Reset this Scene and load entities/Transforms from a file
     *        previously written by save().
     * @return `true` on success; the Scene is left empty on failure.
     */
    bool load(const char* path);

private:
    Allocator            alloc_;
    u32                  max_entities_ = 0;
    World                world_;
    Array<Entity>*        entities_ = nullptr; ///< Roster; Scene is the sole creator.
    HashMap<StringId, Entity>* names_ = nullptr;

    void clear_roster();
};

} // namespace ge::world
