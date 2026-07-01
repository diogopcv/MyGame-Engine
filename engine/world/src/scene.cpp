#include "world/scene.h"
#include "core/serialize.h"
#include "platform/file.h"
#include <new>

namespace ge::world {

void Scene::init(u32 max_entities, Allocator alloc) {
    alloc_        = alloc;
    max_entities_ = max_entities;
    world_.init(max_entities, alloc_);

    entities_ = alloc_.alloc_t<Array<Entity>>();
    new (entities_) Array<Entity>(alloc_);

    names_ = alloc_.alloc_t<HashMap<StringId, Entity>>();
    new (names_) HashMap<StringId, Entity>(alloc_);
}

void Scene::shutdown() {
    world_.shutdown();

    entities_->~Array();
    alloc_.free(entities_, 0);
    entities_ = nullptr;

    names_->~HashMap();
    alloc_.free(names_, 0);
    names_ = nullptr;
}

Entity Scene::create_entity(const char* name) {
    Entity e = world_.create_entity();
    entities_->push(e);
    if (name != nullptr)
        names_->set(string_id(name), e);
    return e;
}

void Scene::destroy_entity(Entity e) {
    world_.destroy_entity(e);
    for (usize i = 0; i < entities_->size(); ++i) {
        if ((*entities_)[i] == e) {
            (*entities_)[i] = entities_->back();
            entities_->pop();
            break;
        }
    }
}

Entity Scene::find(StringId name) const {
    if (Entity* e = names_->get(name)) return *e;
    return {};
}

void Scene::clear_roster() {
    // Destroy back-to-front: World::destroy_entity doesn't touch the roster,
    // so no index shifts to worry about here (unlike destroy_entity() above).
    for (usize i = entities_->size(); i > 0; --i)
        world_.destroy_entity((*entities_)[i - 1]);
    entities_->clear();
    names_->clear();
}

bool Scene::save(const char* path) const {
    BinaryWriter w(alloc_);
    w.write_u32(static_cast<u32>(entities_->size()));

    for (usize i = 0; i < entities_->size(); ++i) {
        Entity e = (*entities_)[i];

        u64 name_hash = 0;
        names_->for_each([&](const StringId& n, const Entity& ne) {
            if (ne == e) name_hash = n.hash;
        });
        w.write_u64(name_hash);

        const Transform* t = const_cast<World&>(world_).get_component<Transform>(e);
        w.write_u8(t != nullptr ? 1 : 0);
        if (t != nullptr)
            serialize(w, t, *Transform::ge_type_info());
    }

    return platform::file_write_all(path, w.data(), w.size());
}

bool Scene::load(const char* path) {
    platform::FileData fd = platform::file_read_all(path);
    if (fd.data == nullptr) return false;

    BinaryReader r(static_cast<const u8*>(fd.data), fd.size);

    u32 count = 0;
    if (!r.read_u32(count)) { platform::file_data_free(fd); return false; }

    clear_roster();

    bool ok = true;
    for (u32 i = 0; i < count && ok; ++i) {
        u64 name_hash = 0;
        if (!r.read_u64(name_hash)) { ok = false; break; }

        Entity e = world_.create_entity();
        entities_->push(e);
        if (name_hash != 0) names_->set(StringId{name_hash}, e);

        u8 has_transform = 0;
        if (!r.read_u8(has_transform)) { ok = false; break; }

        if (has_transform != 0) {
            Transform t{};
            if (!deserialize(r, &t, *Transform::ge_type_info())) { ok = false; break; }
            world_.add_component<Transform>(e, t);
        }
    }

    platform::file_data_free(fd);

    if (!ok) clear_roster();
    return ok;
}

} // namespace ge::world
