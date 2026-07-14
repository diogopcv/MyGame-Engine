#include "core/types.h"
#include "core/log.h"
#include "core/arena.h"
#include "core/profiler.h"
#include "core/cvar.h"
#include "core/transform.h"
#include "platform/window.h"
#include "platform/timer.h"
#include "platform/file.h"
#include "gfx/renderer.h"
#include "gfx/camera.h"
#include "gfx/light.h"
#include "input/input_map.h"
#include "world/scene.h"
#include "world/components.h"
#include "physics/physics_world.h"
#include "physics/rigidbody.h"
#include "anim/animation_loader.h"
#include "anim/pose.h"

#include <filesystem>
#include <string>

namespace {

using namespace ge;

/// Resolve an asset path under assets/models/, relative to the executable.
std::string model_path(const char* name) {
    char exe_dir[512];
    if (platform::executable_dir(exe_dir, sizeof(exe_dir)) == 0) return {};
    // exe_dir is UTF-8; go through char8_t so accented paths survive.
    std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(exe_dir)));
    path = path / ".." / ".." / ".." / "assets" / "models" / name;
    return path.string();
}

/**
 * Fixed-update gameplay system (per ADR-0009: a plain function, not a
 * scheduler). Reads MoveForward/MoveRight axes plus a mouse-look delta and
 * drives the player's rigid body: horizontal velocity is set directly from
 * input (yaw-relative, so looking up/down doesn't tilt movement into the
 * ground/sky), while vertical velocity is left untouched for gravity and
 * the solver to own. Facing is also set directly from yaw rather than left
 * to the solver, since the player capsule's rotation is locked (Milestone
 * 5, docs/phases/05-physics.md) -- a physics-driven character still isn't
 * "driven by torques," it just can't tip over or be launched off course by
 * an unlucky impulse the way a thrown/dropped object could.
 *
 * Call before PhysicsWorld::step() each fixed tick.
 */
void system_move_player_physics(world::World& w, world::Entity player, physics::PhysicsWorld& phys,
                                 f32 move_fwd, f32 move_right, f32 mouse_dx, f32 mouse_dy) {
    auto* cc     = w.get_component<world::CharacterController>(player);
    auto* rb_ref = w.get_component<world::RigidBodyRef>(player);
    if (cc == nullptr || rb_ref == nullptr || rb_ref->body_id == world::kInvalidBodyId) return;

    cc->yaw   += mouse_dx * cc->look_sensitivity;
    cc->pitch += mouse_dy * cc->look_sensitivity;
    constexpr f32 kMaxPitch = 1.5533f; // ~89 degrees, matches gfx::camera_fly_update
    if (cc->pitch >  kMaxPitch) cc->pitch =  kMaxPitch;
    if (cc->pitch < -kMaxPitch) cc->pitch = -kMaxPitch;

    // Yaw-only facing for movement direction (pitch stays for the chase camera).
    gfx::Camera facing;
    facing.yaw = cc->yaw;
    vec3 fwd   = facing.forward();
    vec3 right = facing.right();

    vec3 move = fwd * move_fwd + right * move_right;
    f32 len_sq = move.x * move.x + move.y * move.y + move.z * move.z;
    if (len_sq > 0.0001f) move = normalize(move);

    physics::RigidBody& body = phys.body(rb_ref->body_id);
    body.linear_velocity.x = move.x * cc->move_speed;
    body.linear_velocity.z = move.z * cc->move_speed;
    body.orientation = quat_from_axis_angle({0, 1, 0}, cc->yaw);
}

/**
 * Fixed-update gameplay system: copies each physics-driven entity's
 * RigidBody position/orientation into its Transform, the same "physics is
 * the source of truth, Transform is derived" pattern PhysicsWorld itself
 * uses internally. Call after PhysicsWorld::step().
 */
void system_sync_transform_from_rigidbody(world::World& w, physics::PhysicsWorld& phys) {
    w.each<world::Transform, world::RigidBodyRef>(
        [&](world::Entity, world::Transform& t, world::RigidBodyRef& rb) {
            if (rb.body_id == world::kInvalidBodyId) return;
            const physics::RigidBody& body = phys.body(rb.body_id);
            t.position = body.position;
            t.rotation = body.orientation;
        });
}

} // namespace

int main() {
    using namespace ge;

    platform::time_init();
    GE_INFO("Booting {} ...", "MyEngine");

    profiler().init();
    cvar_system().init();

    cvar_system().register_bool("r_vsync", true, "enable vsync");
    cvar_system().register_int("r_width", 1280, "render width");
    cvar_system().register_int("r_height", 720, "render height");

    Arena perm;
    perm.init(GE_MEGABYTES(64));
    GE_INFO("Permanent arena reserved: {} MB", perm.capacity() / (1024 * 1024));

    platform::Window window;
    platform::WindowDesc desc{};
    desc.title  = "MyEngine - PBR Renderer";
    desc.width  = 1280;
    desc.height = 720;
    if (!window.create(desc)) {
        GE_ERROR("Failed to create window");
        return 1;
    }
    GE_INFO("Window created ({}x{})", window.width(), window.height());

    ge::gfx::Renderer renderer;
    ge::gfx::RendererDesc renderer_desc{};
    renderer_desc.hwnd   = window.native_handle();
    renderer_desc.width  = window.width();
    renderer_desc.height = window.height();
#ifdef _DEBUG
    renderer_desc.enable_debug_layer = true;
#else
    renderer_desc.enable_debug_layer = false;
#endif
    if (!renderer.init(renderer_desc)) {
        GE_ERROR("Failed to initialize the DX12 renderer");
        window.destroy();
        return 1;
    }
    renderer.set_clear_color(0.05f, 0.1f, 0.2f, 1.0f);

    // Load models
    gfx::ModelId helmet = gfx::kInvalidModelId;
    {
        const std::string path = model_path("DamagedHelmet.glb");
        helmet = renderer.load_model(path.c_str(), perm.allocator());
        if (helmet != gfx::kInvalidModelId) {
            GE_INFO("Loaded model: {}", path);
        } else {
            GE_WARN("Could not load model — using built-in cube");
        }
    }

    // Phase 6 (Milestone 3): a skinned character. The mesh loads through the
    // renderer (ge_gfx), the skeleton + clips through ge_anim's own parse of
    // the same .glb; the two meet only through the bone palette handed to
    // submit_skinned() as a bare mat4* (sibling-isolation contract,
    // docs/phases/06-animation.md).
    gfx::ModelId   fox_model = gfx::kInvalidModelId;
    anim::AnimationSet fox_anim{};
    anim::Pose     fox_pose{};
    mat4*          fox_model_space = nullptr;
    mat4*          fox_palette     = nullptr;
    i32            fox_clip        = -1;
    f32            fox_anim_time   = 0.0f;
    {
        const std::string path = model_path("Fox.glb");
        fox_model = renderer.load_model(path.c_str(), perm.allocator());
        if (fox_model != gfx::kInvalidModelId &&
            anim::load_animation_gltf(path.c_str(), perm.allocator(), fox_anim)) {
            fox_pose.init(fox_anim.skeleton.joint_count, perm.allocator());
            fox_model_space = perm.allocator().alloc_n<mat4>(fox_anim.skeleton.joint_count);
            fox_palette     = perm.allocator().alloc_n<mat4>(fox_anim.skeleton.joint_count);
            fox_clip        = fox_anim.find_clip("Walk");
            if (fox_clip < 0 && fox_anim.clip_count > 0) fox_clip = 0;
            // Prime the palette so the first frames (before any fixed tick)
            // don't submit uninitialized matrices.
            if (fox_clip >= 0) {
                anim::sample(fox_anim.clips[fox_clip], fox_anim.skeleton, 0.0f, true, fox_pose);
                anim::pose_to_model_space(fox_anim.skeleton, fox_pose, fox_model_space);
                anim::pose_to_palette(fox_anim.skeleton, fox_model_space, fox_palette);
            }
            GE_INFO("Loaded skinned character: {} ({} joints, {} clips)", path,
                    fox_anim.skeleton.joint_count, fox_anim.clip_count);
        } else {
            GE_WARN("Could not load Fox.glb — no skinned character this run");
            fox_model = gfx::kInvalidModelId;
        }
    }

    // Phase 4: the DamagedHelmet is now a controllable entity, not a static
    // prop — a Scene owns the World; the "player" entity's Transform drives
    // both the model's world matrix and a chase camera behind it.
    // Phase 5 (Milestone 5): the player's Transform is no longer written
    // directly by input -- it's derived each fixed step from a PhysicsWorld
    // rigid body (a rotation-locked capsule), via
    // system_move_player_physics() + system_sync_transform_from_rigidbody().
    world::Scene scene;
    scene.init(64, perm.allocator());

    physics::PhysicsWorld phys;
    phys.init(16, perm.allocator());

    // Static ground the character falls onto and walks around on. No visual
    // representation yet (the renderer only knows how to draw the loaded
    // glTF model) -- purely a physics volume; see docs/phases/05-physics.md.
    phys.add_body(physics::make_box_body({0, -1, 0}, {20, 1, 20}, quat_identity(), 0.0f));

    constexpr f32 kPlayerRadius     = 0.4f;
    constexpr f32 kPlayerHalfHeight = 0.5f; // capsule tip-to-tip height: 2*(0.4+0.5) = 1.8
    u32 player_body_id = phys.add_body(
        physics::make_capsule_body({0, 3, 0}, quat_identity(), kPlayerRadius, kPlayerHalfHeight, 1.0f));
    phys.body(player_body_id).friction = 0.5f;

    world::Entity player = scene.create_entity("player");
    scene.world().add_component<world::Transform>(player, world::Transform{});
    scene.world().add_component<world::RigidBodyRef>(player, world::RigidBodyRef{player_body_id});

    world::CharacterController player_controller{};
    player_controller.pitch = -0.05f; // slight downward look, matching the old fixed camera framing
    scene.world().add_component<world::CharacterController>(player, player_controller);

    input::InputMap input_map;
    input_map.bind_axis(string_id("MoveForward"), platform::KeyCode::W, platform::KeyCode::S);
    input_map.bind_axis(string_id("MoveRight"), platform::KeyCode::D, platform::KeyCode::A);
    input_map.bind_action(string_id("Sprint"), platform::KeyCode::LShift);
    input_map.bind_action(string_id("Quit"), platform::KeyCode::Escape);

    // Set up lighting
    ge::gfx::DirectionalLight sun;
    sun.direction = {0.5f, 1.0f, -0.3f};
    sun.color     = {1.0f, 1.0f, 0.95f};
    sun.intensity = 1.0f;
    renderer.set_sun(sun);

    ge::gfx::PointLight point_lights[2];
    point_lights[0].position  = {2.0f, 1.0f, -1.0f};
    point_lights[0].radius    = 8.0f;
    point_lights[0].color     = {0.3f, 0.5f, 1.0f};
    point_lights[0].intensity = 1.5f;

    point_lights[1].position  = {-2.0f, 0.5f, 1.0f};
    point_lights[1].radius    = 6.0f;
    point_lights[1].color     = {1.0f, 0.3f, 0.2f};
    point_lights[1].intensity = 1.2f;
    renderer.set_point_lights(point_lights, 2);

    bool mouse_captured = false;

    const f64 fixed_dt    = 1.0 / 60.0;
    f64       accumulator = 0.0;
    f64       last        = platform::now_seconds();

    f64 fps_timer = 0.0;
    u32 frames    = 0;

    while (!window.should_close()) {
        profiler().begin_frame();

        const f64 now        = platform::now_seconds();
        f64       frame_time = now - last;
        last = now;

        if (frame_time > 0.25) frame_time = 0.25;
        accumulator += frame_time;

        window.pump_events();
        renderer.resize(window.width(), window.height());
        input_map.resolve(window);

        // Toggle mouse capture with right click
        if (window.mouse_button_down(platform::MouseButton::Right) && !mouse_captured) {
            mouse_captured = true;
            window.set_mouse_capture(true);
        } else if (!window.mouse_button_down(platform::MouseButton::Right) && mouse_captured) {
            mouse_captured = false;
            window.set_mouse_capture(false);
        }

        if (input_map.action_down(string_id("Quit"))) {
            break;
        }

        f32 move_fwd   = input_map.axis(string_id("MoveForward"));
        f32 move_right = input_map.axis(string_id("MoveRight"));
        f32 speed_mult = input_map.action_down(string_id("Sprint")) ? 3.0f : 1.0f;

        while (accumulator >= fixed_dt) {
            f32 mdx = 0.0f, mdy = 0.0f;
            if (mouse_captured) {
                window.mouse_delta(mdx, mdy);
            }

            world::CharacterController* cc = scene.world().get_component<world::CharacterController>(player);
            f32 saved_speed = cc->move_speed;
            cc->move_speed *= speed_mult;
            system_move_player_physics(scene.world(), player, phys, move_fwd, move_right, mdx, mdy);
            cc->move_speed = saved_speed;

            phys.step(static_cast<f32>(fixed_dt));
            system_sync_transform_from_rigidbody(scene.world(), phys);

            // Animation ticks on the fixed step (same accumulator as physics,
            // per the phase doc) — sample the looping clip and rebuild the
            // bone palette; the render submit below uses the latest one.
            if (fox_model != gfx::kInvalidModelId && fox_clip >= 0) {
                fox_anim_time += static_cast<f32>(fixed_dt);
                anim::sample(fox_anim.clips[fox_clip], fox_anim.skeleton,
                             fox_anim_time, true, fox_pose);
                anim::pose_to_model_space(fox_anim.skeleton, fox_pose, fox_model_space);
                anim::pose_to_palette(fox_anim.skeleton, fox_model_space, fox_palette);
            }

            accumulator -= fixed_dt;
        }

        // Chase camera: derived from the player entity each frame, not
        // persistent state of its own (per docs/phases/04-world-gameplay.md).
        {
            world::Transform* pt = scene.world().get_component<world::Transform>(player);
            world::CharacterController* cc = scene.world().get_component<world::CharacterController>(player);

            gfx::Camera follow_cam;
            follow_cam.yaw   = cc->yaw;
            follow_cam.pitch = cc->pitch;
            vec3 fwd = follow_cam.forward();
            follow_cam.position = pt->position - fwd * 3.5f + vec3{0, 1.0f, 0};
            renderer.set_camera(follow_cam);
        }

        {
            GE_PROFILE_SCOPE("render");
            renderer.begin_frame(static_cast<float>(frame_time));
            if (helmet != gfx::kInvalidModelId) {
                world::Transform* pt = scene.world().get_component<world::Transform>(player);
                renderer.submit(helmet, trs(pt->position, pt->rotation, pt->scale));
            }
            if (fox_model != gfx::kInvalidModelId && fox_clip >= 0) {
                // The Fox asset is authored in centimetres (~75 units tall);
                // scale it into the scene and stand it on the ground plane.
                const mat4 fox_world = mat4_scale({0.025f, 0.025f, 0.025f}) *
                                       mat4_translation({2.5f, 0.0f, 2.5f});
                renderer.submit_skinned(fox_model, fox_world, fox_palette,
                                        fox_anim.skeleton.joint_count);
            }
            renderer.end_frame();
        }

        profiler().end_frame();

        ++frames;
        fps_timer += frame_time;
        if (fps_timer >= 1.0) {
            f64 avg_ms = profiler().last_frame_time() * 1000.0;
            const vec3& player_pos = scene.world().get_component<world::Transform>(player)->position;
            GE_INFO("FPS: {} | frame: {:.2f} ms | player.y: {:.2f}", frames, avg_ms, player_pos.y);
            frames = 0;
            fps_timer -= 1.0;
        }
    }

    GE_INFO("Shutting down ...");
    fox_pose.shutdown();
    fox_anim.shutdown();
    scene.shutdown();
    phys.shutdown();
    renderer.shutdown();
    window.destroy();
    perm.shutdown();
    cvar_system().shutdown();
    profiler().shutdown();
    GE_INFO("Clean exit.");
    return 0;
}
