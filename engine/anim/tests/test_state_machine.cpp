#include <doctest/doctest.h>
#include "core/arena.h"
#include "anim/state_machine.h"
#include <cmath>

using namespace ge;
using namespace ge::anim;

namespace {

constexpr f32 kEps = 1e-5f;

bool approx(f32 a, f32 b, f32 eps = kEps) { return std::abs(a - b) < eps; }
bool approx(vec3 a, vec3 b, f32 eps = kEps) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) && approx(a.z, b.z, eps);
}

// A 1-joint skeleton and translation-only clips with trivially predictable
// values, so every blend result is hand-computable:
//   idle -> constant (0,0,0); walk -> constant (10,0,0); run -> (20,0,0);
//   ramp -> t=0:(0,0,0) .. t=1:(1,0,0) linear (reads back the state clock).
struct SmFixture {
    Skeleton skel;
    AnimationClip idle, walk, run, ramp;
    Pose pose;

    void make_constant(AnimationClip& c, vec3 value, Allocator alloc) {
        c.init(1, alloc);
        c.duration = 1.0f;
        Vec3Track& t = c.alloc_translation_keys(0, 1);
        t.times[0] = 0.0f;
        t.values[0] = value;
    }

    void init(Allocator alloc) {
        skel.init(1, alloc);
        make_constant(idle, {0, 0, 0}, alloc);
        make_constant(walk, {10, 0, 0}, alloc);
        make_constant(run,  {20, 0, 0}, alloc);

        ramp.init(1, alloc);
        ramp.duration = 1.0f;
        Vec3Track& t = ramp.alloc_translation_keys(0, 2);
        t.times[0] = 0.0f; t.values[0] = {0, 0, 0};
        t.times[1] = 1.0f; t.values[1] = {1, 0, 0};

        pose.init(1, alloc);
    }

    AnimState single(const AnimationClip& c, bool looping = true) {
        AnimState s{};
        s.clips[0] = &c;
        s.clip_count = 1;
        s.looping = looping;
        return s;
    }

    vec3 out() const { return pose.joints[0].translation; }
};

} // namespace

TEST_CASE("state machine: a single-clip state matches sampling the clip directly") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        SmFixture f; f.init(arena.allocator());
        AnimStateMachine sm; sm.init(&f.skel, arena.allocator());
        sm.add_state(f.single(f.ramp));

        // Two ticks accumulate the state clock: 0.25 + 0.25 -> sample at 0.5.
        sm.tick(0.25f, 0.0f, f.pose);
        REQUIRE(approx(f.out(), {0.25f, 0, 0}));
        sm.tick(0.25f, 0.0f, f.pose);
        REQUIRE(approx(f.out(), {0.5f, 0, 0}));
        CHECK_FALSE(sm.in_transition());

        sm.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("state machine: 1D blend state interpolates the bracketing clips by parameter") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        SmFixture f; f.init(arena.allocator());
        AnimStateMachine sm; sm.init(&f.skel, arena.allocator());

        AnimState locomotion{};
        locomotion.clips[0] = &f.idle; locomotion.anchors[0] = 0.0f;
        locomotion.clips[1] = &f.walk; locomotion.anchors[1] = 3.0f;
        locomotion.clips[2] = &f.run;  locomotion.anchors[2] = 9.0f;
        locomotion.clip_count = 3;
        sm.add_state(locomotion);

        // At each anchor, that clip fully owns the pose.
        sm.tick(0.01f, 0.0f, f.pose);
        REQUIRE(approx(f.out(), {0, 0, 0}));
        sm.tick(0.01f, 3.0f, f.pose);
        REQUIRE(approx(f.out(), {10, 0, 0}));
        sm.tick(0.01f, 9.0f, f.pose);
        REQUIRE(approx(f.out(), {20, 0, 0}));

        // Between anchors, linear in the parameter.
        sm.tick(0.01f, 1.5f, f.pose);   // halfway idle->walk
        REQUIRE(approx(f.out(), {5, 0, 0}));
        sm.tick(0.01f, 6.0f, f.pose);   // halfway walk->run
        REQUIRE(approx(f.out(), {15, 0, 0}));

        // Outside the range, clamps to the nearest anchor clip.
        sm.tick(0.01f, -5.0f, f.pose);
        REQUIRE(approx(f.out(), {0, 0, 0}));
        sm.tick(0.01f, 100.0f, f.pose);
        REQUIRE(approx(f.out(), {20, 0, 0}));

        sm.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("state machine: crossfade ramps the incoming pose weight from 0 to 1") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        SmFixture f; f.init(arena.allocator());
        AnimStateMachine sm; sm.init(&f.skel, arena.allocator());
        const u32 s_idle = sm.add_state(f.single(f.idle));
        const u32 s_walk = sm.add_state(f.single(f.walk));

        CHECK(sm.current_state() == s_idle);
        sm.tick(0.1f, 0.0f, f.pose);
        REQUIRE(approx(f.out(), {0, 0, 0}));

        sm.transition_to(s_walk, 0.5f);
        CHECK(sm.current_state() == s_walk);
        CHECK(sm.in_transition());

        // Fade progress 0.25 -> 25% walk; 0.5 -> 50%; complete -> 100% walk.
        sm.tick(0.125f, 0.0f, f.pose);
        REQUIRE(approx(f.out(), {2.5f, 0, 0}));
        sm.tick(0.125f, 0.0f, f.pose);
        REQUIRE(approx(f.out(), {5.0f, 0, 0}));
        sm.tick(0.25f, 0.0f, f.pose);
        REQUIRE(approx(f.out(), {10.0f, 0, 0}));
        CHECK_FALSE(sm.in_transition());

        // Weights always summed to 1: every intermediate value lay on the
        // straight line between the two poses (checked exactly above).

        sm.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("state machine: transitioning to the current state is a no-op") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        SmFixture f; f.init(arena.allocator());
        AnimStateMachine sm; sm.init(&f.skel, arena.allocator());
        const u32 s0 = sm.add_state(f.single(f.ramp));

        sm.tick(0.5f, 0.0f, f.pose);
        sm.transition_to(s0, 0.5f);
        CHECK_FALSE(sm.in_transition());
        // The state clock keeps running (no reset).
        sm.tick(0.25f, 0.0f, f.pose);
        REQUIRE(approx(f.out(), {0.75f, 0, 0}));

        sm.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("state machine: the incoming state's clock restarts from zero") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        SmFixture f; f.init(arena.allocator());
        AnimStateMachine sm; sm.init(&f.skel, arena.allocator());
        const u32 s_idle = sm.add_state(f.single(f.idle));
        const u32 s_ramp = sm.add_state(f.single(f.ramp, /*looping=*/false));
        (void)s_idle;

        // Let a lot of time pass in idle; a stale ramp clock would clamp at
        // the non-looping clip's end (1,0,0) instead of starting over.
        for (int i = 0; i < 10; ++i) sm.tick(0.2f, 0.0f, f.pose);

        sm.transition_to(s_ramp, 0.0f); // instant switch
        CHECK_FALSE(sm.in_transition());
        sm.tick(0.25f, 0.0f, f.pose);
        REQUIRE(approx(f.out(), {0.25f, 0, 0}));

        sm.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("state machine: crossfade into a 1D blend combines fade and blend weights") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        SmFixture f; f.init(arena.allocator());
        AnimStateMachine sm; sm.init(&f.skel, arena.allocator());
        const u32 s_idle = sm.add_state(f.single(f.idle));
        (void)s_idle;

        AnimState moving{};
        moving.clips[0] = &f.walk; moving.anchors[0] = 3.0f;
        moving.clips[1] = &f.run;  moving.anchors[1] = 9.0f;
        moving.clip_count = 2;
        const u32 s_moving = sm.add_state(moving);

        sm.tick(0.1f, 0.0f, f.pose);
        sm.transition_to(s_moving, 0.5f);

        // param 6 -> moving state pose = (15,0,0); fade 50% -> half of that.
        sm.tick(0.25f, 6.0f, f.pose);
        REQUIRE(approx(f.out(), {7.5f, 0, 0}));
        // Fade completes -> pure blended moving pose.
        sm.tick(0.25f, 6.0f, f.pose);
        REQUIRE(approx(f.out(), {15.0f, 0, 0}));

        sm.shutdown();
    }
    arena.shutdown();
}
