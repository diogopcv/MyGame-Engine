#pragma once
#include "core/types.h"
#include "core/vec.h"
#include "core/mat4.h"

/**
 * @file camera.h
 * @brief Camera struct and fly-through controller (Layer 3 — Graphics).
 */
namespace ge::gfx {

/**
 * @brief Perspective camera with position and Euler angles.
 */
struct Camera {
    vec3 position  = {0, 1.5f, -3.0f};
    f32  yaw       = 0.0f;  ///< Radians around Y axis (left-handed: +Y up).
    f32  pitch     = 0.0f;  ///< Radians around X axis. Clamped to [-89, 89] degrees.
    f32  fov_y     = 1.0471975512f; ///< Vertical field of view in radians (~60 deg).
    f32  near_z    = 0.1f;
    f32  far_z     = 100.0f;

    /** @brief Compute the forward direction vector from yaw/pitch. */
    vec3 forward() const {
        f32 cy = cosf(yaw),   sy = sinf(yaw);
        f32 cp = cosf(pitch), sp = sinf(pitch);
        return {sy * cp, sp, cy * cp};
    }

    /** @brief Compute the right direction vector (perpendicular to forward and world up). */
    vec3 right() const {
        vec3 f = forward();
        vec3 up = {0, 1.0f, 0};
        return normalize(cross(up, f));
    }

    /** @brief Build the view matrix (left-handed look-at). */
    mat4 view_matrix() const {
        vec3 f = forward();
        vec3 target = position + f;
        return mat4_look_at_lh(position, target, {0, 1.0f, 0});
    }

    /** @brief Build the perspective projection matrix (left-handed, [0,1] depth). */
    mat4 proj_matrix(f32 aspect) const {
        return mat4_perspective_lh(fov_y, aspect, near_z, far_z);
    }
};

/**
 * @brief Update a camera from WASD + mouse input (fly-through controller).
 * @param cam       Camera to update.
 * @param dt        Delta time in seconds.
 * @param move_fwd  Forward/backward input (-1 to 1).
 * @param move_right Strafe left/right input (-1 to 1).
 * @param move_up   Up/down input (-1 to 1).
 * @param mouse_dx  Mouse horizontal delta in pixels.
 * @param mouse_dy  Mouse vertical delta in pixels.
 * @param speed     Movement speed in units/sec.
 * @param sensitivity Mouse look sensitivity (radians per pixel).
 */
inline void camera_fly_update(Camera& cam, f32 dt,
                              f32 move_fwd, f32 move_right, f32 move_up,
                              f32 mouse_dx, f32 mouse_dy,
                              f32 speed = 5.0f,
                              f32 sensitivity = 0.003f) {
    cam.yaw   += mouse_dx * sensitivity;
    cam.pitch += mouse_dy * sensitivity;

    constexpr f32 kMaxPitch = 1.5533f; // ~89 degrees
    if (cam.pitch >  kMaxPitch) cam.pitch =  kMaxPitch;
    if (cam.pitch < -kMaxPitch) cam.pitch = -kMaxPitch;

    vec3 fwd   = cam.forward();
    vec3 right = cam.right();
    vec3 up    = {0, 1.0f, 0};

    vec3 velocity = fwd * move_fwd + right * move_right + up * move_up;
    f32 len_sq = velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z;
    if (len_sq > 0.0001f) {
        velocity = normalize(velocity);
    }

    cam.position = cam.position + velocity * (speed * dt);
}

} // namespace ge::gfx
