#pragma once

// Shared wireframe-draw primitives for the editor viewport overlays.
//
// The viewport gizmos (lights, probes, cameras, colliders, bounds) all draw
// world-space wireframes into the ImGui viewport overlay draw list. They share
// one projection convention and a small set of primitives (circle, sphere,
// box, arrow). Those live here so each overlay routes through the same math
// instead of re-deriving it. The math is intentionally minimal and identical
// across callers - this is plain debug-overlay drawing, not a render path.

#include <cmath>

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Vkm::Engine {

// Project a world point through the viewport's view+projection into screen
// coordinates inside the viewport child rect. Returns false when the point is
// behind the camera. The 3D pass renders at viewport size and the viewport
// child sits at vpMin onscreen, so NDC maps to (vpMin + (0..vpSize)) directly.
inline bool projectToViewport(
    const glm::mat4& vp,
    const glm::vec3& p,
    ImVec2 vpMin,
    ImVec2 vpSize,
    ImVec2& out
) {
    const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
    if (clip.w <= 1e-5f) return false;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    out = ImVec2(vpMin.x + (ndc.x * 0.5f + 0.5f) * vpSize.x,
                 vpMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * vpSize.y);
    return true;
}

// Build an orthonormal basis (outT, outB) in the plane perpendicular to a unit
// direction. Picks world-up as the reference unless dir is nearly vertical, in
// which case it falls back to world-right so the cross products stay stable.
inline void orthoBasis(const glm::vec3& dir, glm::vec3& outT, glm::vec3& outB) {
    const glm::vec3 ref = std::abs(dir.y) < 0.99f
        ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    outT = glm::normalize(glm::cross(dir, ref));
    outB = glm::normalize(glm::cross(outT, dir));
}

// Draw a world-space segment as a viewport line, dropping it when either end
// is behind the camera (same near-plane handling as the gizmo wireframes -
// good enough for a debug overlay).
inline void wireSegment(
    ImDrawList* dl, const glm::mat4& vp,
    const glm::vec3& a, const glm::vec3& b,
    ImVec2 vpMin, ImVec2 vpSize, ImU32 col, float thickness
) {
    ImVec2 sa, sb;
    if (projectToViewport(vp, a, vpMin, vpSize, sa)
     && projectToViewport(vp, b, vpMin, vpSize, sb))
        dl->AddLine(sa, sb, col, thickness);
}

// World-space circle center + radius * (cos t * axisA + sin t * axisB),
// drawn as connected segments that break where a point falls behind the
// camera. Every ring/disc gizmo routes through here.
inline void wireCircle(
    ImDrawList* dl, const glm::mat4& vp,
    const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB,
    float radius, int segments,
    ImVec2 vpMin, ImVec2 vpSize, ImU32 col, float thickness
) {
    ImVec2 prev{};
    bool havePrev = false;
    for (int s = 0; s <= segments; ++s) {
        const float t = (static_cast<float>(s) / segments) * glm::two_pi<float>();
        const glm::vec3 p = center + (axisA * std::cos(t) + axisB * std::sin(t)) * radius;
        ImVec2 sp;
        if (projectToViewport(vp, p, vpMin, vpSize, sp)) {
            if (havePrev) dl->AddLine(prev, sp, col, thickness);
            prev = sp;
            havePrev = true;
        } else {
            havePrev = false;
        }
    }
}

// Three orthogonal great circles - the classic "radius sphere" gizmo.
inline void wireSphere(
    ImDrawList* dl, const glm::mat4& vp,
    const glm::vec3& center, float radius, int segments,
    ImVec2 vpMin, ImVec2 vpSize, ImU32 col, float thickness
) {
    const glm::vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
    wireCircle(dl, vp, center, X, Y, radius, segments, vpMin, vpSize, col, thickness);
    wireCircle(dl, vp, center, X, Z, radius, segments, vpMin, vpSize, col, thickness);
    wireCircle(dl, vp, center, Y, Z, radius, segments, vpMin, vpSize, col, thickness);
}

// World-space arrow: a line plus a filled triangular head at the tip.
// Skipped entirely when either end is behind the camera.
inline void arrowLine(
    ImDrawList* dl, const glm::mat4& vp,
    const glm::vec3& from, const glm::vec3& to,
    ImVec2 vpMin, ImVec2 vpSize, ImU32 col,
    float thickness, float headLen, float headWidth
) {
    ImVec2 a, b;
    if (!projectToViewport(vp, from, vpMin, vpSize, a)) return;
    if (!projectToViewport(vp, to,   vpMin, vpSize, b)) return;
    dl->AddLine(a, b, col, thickness);

    ImVec2 dv(b.x - a.x, b.y - a.y);
    const float len = std::sqrt(dv.x * dv.x + dv.y * dv.y);
    if (len <= 1.0f) return;
    dv.x /= len;
    dv.y /= len;
    const ImVec2 perp(-dv.y, dv.x);
    dl->AddTriangleFilled(b,
        ImVec2(b.x - dv.x * headLen + perp.x * headWidth,
               b.y - dv.y * headLen + perp.y * headWidth),
        ImVec2(b.x - dv.x * headLen - perp.x * headWidth,
               b.y - dv.y * headLen - perp.y * headWidth),
        col);
}

// Box: 8 oriented corners, 12 edges. halfExtents are the box extents -
// Transform scale is intentionally NOT applied because callers (collider /
// probe / AABB overlays) pass the exact extents they want drawn, so a box that
// disagrees with the rendered mesh stays visible here rather than hidden.
inline void wireBox(
    ImDrawList* dl, const glm::mat4& vp,
    const glm::vec3& pos, const glm::quat& rot, const glm::vec3& he,
    ImVec2 vpMin, ImVec2 vpSize, ImU32 col, float thickness = 1.5f
) {
    const glm::mat3 r = glm::mat3_cast(rot);
    glm::vec3 c[8];
    int k = 0;
    for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
    for (int sz = -1; sz <= 1; sz += 2)
        c[k++] = pos + r * glm::vec3(he.x * sx, he.y * sy, he.z * sz);

    // Corner index bits: bit2 = x, bit1 = y, bit0 = z. An edge joins two
    // corners that differ in exactly one bit.
    static const int edges[12][2] = {
        {0,1}, {2,3}, {4,5}, {6,7},   // along z
        {0,2}, {1,3}, {4,6}, {5,7},   // along y
        {0,4}, {1,5}, {2,6}, {3,7},   // along x
    };
    for (const auto& e : edges)
        wireSegment(dl, vp, c[e[0]], c[e[1]], vpMin, vpSize, col, thickness);
}

} // namespace Vkm::Engine
