// Phase 6 Milestone 5 simulation baselines (docs/phases/06-animation.md):
// the root-motion pipeline end to end — authored clip -> extract_root_motion
// per fixed tick -> desired velocity on a Phase 5 capsule body -> physics
// step — checked against the analytically expected motion. Deterministic:
// no randomness, fixed timestep, fixed tick count.

#include <doctest/doctest.h>
#include "core/arena.h"
#include "anim/animation_loader.h"
#include "anim/root_motion.h"
#include "physics/physics_world.h"
#include "physics/rigidbody.h"
#include "platform/file.h"

#include <cmath>
#include <filesystem>
#include <string>

using namespace ge;

namespace {

/// Resolve a model path relative to the repo's assets folder (same pattern as
/// the anim/gfx tests), or an empty string if missing.
std::string find_model(const char* name) {
    char exe_dir[512];
    if (ge::platform::executable_dir(exe_dir, sizeof(exe_dir)) == 0) return {};
    std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(exe_dir)));
    path = path / ".." / ".." / ".." / "assets" / "models" / name;
    if (!std::filesystem::exists(path)) return {};
    return path.string();
}

// Mirror of the sandbox player: a dynamic capsule on the Phase 5 static
// ground box. Friction is zeroed on the moving bodies so the analytic
// expectation is tight (the sandbox's friction only scrubs a few percent).
struct CharacterScene {
    physics::PhysicsWorld phys;
    u32 capsule_id = 0;

    void init(Allocator alloc) {
        phys.init(8, alloc);
        u32 ground = phys.add_body(
            physics::make_box_body({0, -1, 0}, {20, 1, 20}, quat_identity(), 0.0f));
        phys.body(ground).friction = 0.0f;
        // Same shape as the sandbox player, starting at its resting height.
        capsule_id = phys.add_body(
            physics::make_capsule_body({0, 0.9f, 0}, quat_identity(), 0.4f, 0.5f, 1.0f));
        phys.body(capsule_id).friction = 0.0f;
    }

    /// Drive the capsule with the "move" clip's root motion for @p seconds.
    void run(const anim::AnimationClip& clip, const anim::Skeleton& skel, f32 seconds) {
        const f32 dt = 1.0f / 60.0f;
        const int ticks = static_cast<int>(seconds / dt + 0.5f);
        f32 t = 0.0f;
        for (int i = 0; i < ticks; ++i) {
            const anim::RootMotionDelta d =
                anim::extract_root_motion(clip, skel, t, t + dt, /*looping=*/true);
            t += dt;
            // The same feed as system_move_player_physics: horizontal velocity
            // set directly, vertical left to gravity and the solver.
            physics::RigidBody& body = phys.body(capsule_id);
            body.linear_velocity.x = d.translation.x / dt;
            body.linear_velocity.z = d.translation.z / dt;
            phys.step(dt);
        }
    }
};

} // namespace

// The DoD baseline: after T seconds of the looping "move" clip (authored at
// 2 u/s in +x), the character's world position matches the clip analytically
// — and it never stopped colliding with the ground box under gravity.
TEST_CASE("baseline: root-motion-driven capsule matches the authored clip's motion") {
    const std::string path = find_model("skinned_strip.glb");
    REQUIRE_MESSAGE(!path.empty(), "skinned_strip.glb missing — run tools/gen_skinned_glb.exe");

    Arena arena; arena.init(GE_MEGABYTES(1));
    {
        anim::AnimationSet set;
        REQUIRE(anim::load_animation_gltf(path.c_str(), arena.allocator(), set));
        const i32 move = set.find_clip("move");
        REQUIRE(move >= 0);

        CharacterScene scene;
        scene.init(arena.allocator());
        scene.run(set.clips[move], set.skeleton, 4.0f);

        // 4 s x 2 u/s = 8 units of travel, straight down +x.
        const physics::RigidBody& body = scene.phys.body(scene.capsule_id);
        CHECK(std::abs(body.position.x - 8.0f) < 0.05f);
        CHECK(std::abs(body.position.z) < 0.01f);
        // Still resting on the ground box (unsupported it would be ~78 units
        // down after 4 s of gravity).
        CHECK(body.position.y > 0.8f);
        CHECK(body.position.y < 1.0f);

        // The rendered pose is the stripped one: the root's horizontal track
        // is zeroed while its animated channels elsewhere survive.
        anim::Pose pose; pose.init(set.skeleton.joint_count, arena.allocator());
        anim::sample(set.clips[move], set.skeleton, 0.5f, true, pose);
        CHECK(std::abs(pose.joints[0].translation.x - 1.0f) < 1e-5f); // authored: mid-travel
        anim::strip_root_motion(set.skeleton, pose);
        CHECK(std::abs(pose.joints[0].translation.x) < 1e-6f);        // stripped: at rest
        CHECK(std::abs(pose.joints[0].translation.z) < 1e-6f);

        pose.shutdown();
        scene.phys.shutdown();
        set.shutdown();
    }
    arena.shutdown();
}

// Physics stays the authority: a wall in the clip's path stops the character
// even though the animation keeps feeding forward velocity — root motion
// requests movement, it never teleports the body.
TEST_CASE("baseline: a wall stops the root-motion-driven capsule") {
    const std::string path = find_model("skinned_strip.glb");
    REQUIRE_MESSAGE(!path.empty(), "skinned_strip.glb missing — run tools/gen_skinned_glb.exe");

    Arena arena; arena.init(GE_MEGABYTES(1));
    {
        anim::AnimationSet set;
        REQUIRE(anim::load_animation_gltf(path.c_str(), arena.allocator(), set));
        const i32 move = set.find_clip("move");
        REQUIRE(move >= 0);

        CharacterScene scene;
        scene.init(arena.allocator());
        // Static wall whose near face is at x = 3.4: the capsule (radius 0.4)
        // can reach x ~= 3.0 at most, far short of the unobstructed 8.
        scene.phys.add_body(
            physics::make_box_body({3.9f, 1.5f, 0}, {0.5f, 1.5f, 3.0f}, quat_identity(), 0.0f));
        scene.run(set.clips[move], set.skeleton, 4.0f);

        const physics::RigidBody& body = scene.phys.body(scene.capsule_id);
        CHECK(body.position.x > 2.6f);  // it did travel up to the wall...
        CHECK(body.position.x < 3.1f);  // ...and the wall held it there
        CHECK(body.position.y > 0.8f);  // still standing on the ground
        CHECK(body.position.y < 1.0f);

        scene.phys.shutdown();
        set.shutdown();
    }
    arena.shutdown();
}
