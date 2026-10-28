#pragma once

#include <algorithm>

#include <imgui.h>
#include <glm/glm.hpp>

#include "core/math/bounds.h"

namespace Vkm::Engine {

/**
 * @brief Signed distance from a clip-space point to the near plane, positive in front of it.
 *
 * The engine's clip space puts the plane at z = -w, so this covers perspective and orthographic
 * alike. Testing w alone passes points nearer than the plane (tiny divisors blow up the
 * projection) and, under ortho, everything behind the camera.
 *
 * @param clip The point in clip space.
 * @return z + w: positive in front of the near plane, negative behind it.
 */
inline float nearPlaneSide(const glm::vec4& clip) {
    return clip.z + clip.w;
}

/**
 * @brief Map a clip-space point in front of the near plane to screen pixels in the viewport rect.
 *
 * NDC spans the viewport rect at any render scale, so it maps to vpMin + (0..vpSize) directly.
 *
 * @param clip   The point in clip space, in front of the near plane.
 * @param vpMin  Viewport rect's top-left, screen pixels.
 * @param vpSize Viewport rect's size, screen pixels.
 * @return Screen pixels.
 */
inline ImVec2 clipToViewport(const glm::vec4& clip, ImVec2 vpMin, ImVec2 vpSize) {
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return ImVec2(
        vpMin.x + (ndc.x * 0.5f + 0.5f) * vpSize.x,
        vpMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * vpSize.y
    );
}

/**
 * @brief Project a world point to screen pixels in the viewport rect.
 *
 * A point behind the near plane is dropped: there is no second end to clip against.
 *
 * @param vp     Viewport camera's view-projection.
 * @param p      World-space point.
 * @param vpMin  Viewport rect's top-left, screen pixels.
 * @param vpSize Viewport rect's size, screen pixels.
 * @param out    Screen pixels; untouched on false.
 * @return False when the point is behind the near plane.
 */
inline bool projectToViewport(
    const glm::mat4& vp,
    const glm::vec3& p,
    ImVec2 vpMin,
    ImVec2 vpSize,
    ImVec2& out
) {
    const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
    if (nearPlaneSide(clip) <= 0.0f) return false;
    out = clipToViewport(clip, vpMin, vpSize);
    return true;
}

/**
 * @brief The world ray under a screen point of the viewport rect - the inverse of projectToViewport.
 *
 * The origin is on the near plane, which keeps it right under ortho; see Math::rayThroughNdc.
 *
 * @param invViewProj Inverse of the viewport camera's view-projection.
 * @param point       Screen pixels.
 * @param vpMin       Viewport rect's top-left, screen pixels.
 * @param vpSize      Viewport rect's size, screen pixels.
 * @return The ray, with a unit direction.
 */
inline Math::Ray viewportRay(const glm::mat4& invViewProj, ImVec2 point, ImVec2 vpMin, ImVec2 vpSize) {
    const glm::vec2 ndc(
        ((point.x - vpMin.x) / std::max(1.0f, vpSize.x)) * 2.0f - 1.0f,
        1.0f - ((point.y - vpMin.y) / std::max(1.0f, vpSize.y)) * 2.0f
    );
    return Math::rayThroughNdc(invViewProj, ndc);
}

} // namespace Vkm::Engine
