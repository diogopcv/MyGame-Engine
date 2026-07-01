#pragma once
#include "core/types.h"
#include "core/string_id.h"
#include "core/assert.h"
#include "platform/window.h"

/**
 * @file input_map.h
 * @brief Named action/axis input mapping (Layer 3 — Subsystems).
 *
 * Decouples gameplay code from raw key codes: bind a StringId action/axis
 * name to one or more platform::KeyCode values, then resolve and query by
 * name. resolve() is a template over the key source so it is unit-testable
 * without a real OS window (see docs/phases/04-world-gameplay.md).
 */
namespace ge::input {

/** @brief A digital (pressed/released) action bound to a single key. */
struct ActionBinding {
    StringId          name;
    platform::KeyCode key;
};

/** @brief An analog axis in [-1, 1] bound to a positive/negative key pair. */
struct AxisBinding {
    StringId          name;
    platform::KeyCode positive;
    platform::KeyCode negative;
};

/**
 * @brief Registry of action/axis bindings plus their last-resolved state.
 *
 * Fixed-capacity (like CVarSystem) — binding counts are small and known at
 * setup time, so no dynamic allocation is needed.
 */
class InputMap {
public:
    /** @brief Bind a digital action name to a key. */
    void bind_action(StringId name, platform::KeyCode key) {
        GE_ASSERT(action_count_ < kMaxBindings);
        actions_[action_count_] = { name, key };
        action_state_[action_count_] = false;
        ++action_count_;
    }

    /** @brief Bind an analog axis name to a positive/negative key pair. */
    void bind_axis(StringId name, platform::KeyCode positive, platform::KeyCode negative) {
        GE_ASSERT(axis_count_ < kMaxBindings);
        axes_[axis_count_] = { name, positive, negative };
        axis_value_[axis_count_] = 0.0f;
        ++axis_count_;
    }

    /**
     * @brief Re-resolve every binding against a key source.
     * @tparam KeySource Any type exposing `bool key_down(platform::KeyCode) const`
     *                   (e.g. platform::Window, or a fake for unit tests).
     */
    template <typename KeySource>
    void resolve(const KeySource& keys) {
        for (u32 i = 0; i < action_count_; ++i)
            action_state_[i] = keys.key_down(actions_[i].key);

        for (u32 i = 0; i < axis_count_; ++i) {
            f32 pos = keys.key_down(axes_[i].positive) ? 1.0f : 0.0f;
            f32 neg = keys.key_down(axes_[i].negative) ? 1.0f : 0.0f;
            axis_value_[i] = pos - neg;
        }
    }

    /** @brief Whether the named action was down as of the last resolve(). */
    bool action_down(StringId name) const {
        for (u32 i = 0; i < action_count_; ++i)
            if (actions_[i].name == name) return action_state_[i];
        return false;
    }

    /** @brief Value in [-1, 1] of the named axis as of the last resolve(). */
    f32 axis(StringId name) const {
        for (u32 i = 0; i < axis_count_; ++i)
            if (axes_[i].name == name) return axis_value_[i];
        return 0.0f;
    }

private:
    static constexpr u32 kMaxBindings = 32;

    ActionBinding actions_[kMaxBindings]{};
    bool          action_state_[kMaxBindings]{};
    u32           action_count_ = 0;

    AxisBinding axes_[kMaxBindings]{};
    f32         axis_value_[kMaxBindings]{};
    u32         axis_count_ = 0;
};

} // namespace ge::input
