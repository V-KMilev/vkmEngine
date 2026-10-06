#pragma once

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/bounds.h"
#include "ui/editor_style.h"

namespace Vkm::Engine {

/**
 * @brief Signed distance from a clip-space point to the near plane, positive in front of it.
 *
 * GL clip space puts the plane at z = -w, so this covers perspective and orthographic
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
 * The 3D pass renders at viewport size, so NDC maps to vpMin + (0..vpSize) directly.
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
 * @brief Pull whichever end of a clip-space segment lies behind the near plane onto it.
 *
 * Clipping, not dropping, lets a wire near the camera run off the viewport edge rather
 * than stop in mid-air.
 *
 * @param a One end in clip space; moved onto the near plane if behind it.
 * @param b The other end, likewise.
 * @return False when both ends are behind it.
 */
inline bool clipToNearPlane(glm::vec4& a, glm::vec4& b) {
    const float da = nearPlaneSide(a);
    const float db = nearPlaneSide(b);
    if (da < 0.0f && db < 0.0f) return false;
    if (da < 0.0f)      a = a + (b - a) * (da / (da - db));
    else if (db < 0.0f) b = b + (a - b) * (db / (db - da));
    return true;
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

/**
 * @brief Build an orthonormal basis (outT, outB) in the plane perpendicular to a unit direction.
 *
 * References world-up, or world-right when dir is nearly vertical.
 *
 * @param dir  The plane's unit normal.
 * @param outT First basis vector.
 * @param outB Second, perpendicular to both.
 */
inline void orthoBasis(const glm::vec3& dir, glm::vec3& outT, glm::vec3& outB) {
    const glm::vec3 ref = std::abs(dir.y) < 0.99f
        ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    outT = glm::normalize(glm::cross(dir, ref));
    outB = glm::normalize(glm::cross(outT, dir));
}

/**
 * @brief Draw a world-space segment as a viewport line, cut at the near plane.
 *
 * @param dl        Draw list to append to.
 * @param vp        Viewport camera's view-projection.
 * @param a         One end, world space.
 * @param b         The other end, world space.
 * @param vpMin     Viewport rect's top-left, screen pixels.
 * @param vpSize    Viewport rect's size, screen pixels.
 * @param col       Line colour.
 * @param thickness Line width, screen pixels.
 */
inline void wireSegment(
    ImDrawList* dl,
    const glm::mat4& vp,
    const glm::vec3& a,
    const glm::vec3& b,
    ImVec2 vpMin,
    ImVec2 vpSize,
    ImU32 col,
    float thickness
) {
    glm::vec4 ca = vp * glm::vec4(a, 1.0f);
    glm::vec4 cb = vp * glm::vec4(b, 1.0f);
    if (!clipToNearPlane(ca, cb)) return;
    dl->AddLine(clipToViewport(ca, vpMin, vpSize), clipToViewport(cb, vpMin, vpSize), col, thickness);
}

/**
 * @brief Draw the arc center + radius * (cos t * axisA + sin t * axisB) over [from, to] radians.
 *
 * @param dl        Draw list to append to.
 * @param vp        Viewport camera's view-projection.
 * @param center    World-space centre.
 * @param axisA     Unit direction at t = 0.
 * @param axisB     Unit direction at t = pi/2.
 * @param radius    World units.
 * @param from      Start angle, radians.
 * @param to        End angle, radians.
 * @param segments  Segment count.
 * @param vpMin     Viewport rect's top-left, screen pixels.
 * @param vpSize    Viewport rect's size, screen pixels.
 * @param col       Line colour.
 * @param thickness Line width, screen pixels.
 */
inline void wireArc(
    ImDrawList* dl,
    const glm::mat4& vp,
    const glm::vec3& center,
    const glm::vec3& axisA,
    const glm::vec3& axisB,
    float radius,
    float from,
    float to,
    int segments,
    ImVec2 vpMin,
    ImVec2 vpSize,
    ImU32 col,
    float thickness
) {
    glm::vec4 prev{};
    for (int s = 0; s <= segments; ++s) {
        const float t = from + (to - from) * (static_cast<float>(s) / segments);
        const glm::vec3 p = center + (axisA * std::cos(t) + axisB * std::sin(t)) * radius;
        const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
        if (s > 0) {
            glm::vec4 a = prev;
            glm::vec4 b = clip;
            if (clipToNearPlane(a, b))
                dl->AddLine(
                    clipToViewport(a, vpMin, vpSize),
                    clipToViewport(b, vpMin, vpSize),
                    col,
                    thickness
                );
        }
        prev = clip;
    }
}

/**
 * @brief Draw a full circle in the plane spanned by axisA and axisB.
 *
 * @param dl        Draw list to append to.
 * @param vp        Viewport camera's view-projection.
 * @param center    World-space centre.
 * @param axisA     First unit axis of the plane.
 * @param axisB     Second, perpendicular to @p axisA.
 * @param radius    World units.
 * @param segments  Segment count.
 * @param vpMin     Viewport rect's top-left, screen pixels.
 * @param vpSize    Viewport rect's size, screen pixels.
 * @param col       Line colour.
 * @param thickness Line width, screen pixels.
 */
inline void wireCircle(
    ImDrawList* dl,
    const glm::mat4& vp,
    const glm::vec3& center,
    const glm::vec3& axisA,
    const glm::vec3& axisB,
    float radius,
    int segments,
    ImVec2 vpMin,
    ImVec2 vpSize,
    ImU32 col,
    float thickness
) {
    wireArc(
        dl,
        vp,
        center,
        axisA,
        axisB,
        radius,
        0.0f,
        glm::two_pi<float>(),
        segments,
        vpMin,
        vpSize,
        col,
        thickness
    );
}

/**
 * @brief Draw a sphere as three orthogonal great circles.
 *
 * @param dl        Draw list to append to.
 * @param vp        Viewport camera's view-projection.
 * @param center    World-space centre.
 * @param radius    World units.
 * @param segments  Segments per circle.
 * @param vpMin     Viewport rect's top-left, screen pixels.
 * @param vpSize    Viewport rect's size, screen pixels.
 * @param col       Line colour.
 * @param thickness Line width, screen pixels.
 */
inline void wireSphere(
    ImDrawList* dl,
    const glm::mat4& vp,
    const glm::vec3& center,
    float radius,
    int segments,
    ImVec2 vpMin,
    ImVec2 vpSize,
    ImU32 col,
    float thickness
) {
    const glm::vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
    wireCircle(dl, vp, center, X, Y, radius, segments, vpMin, vpSize, col, thickness);
    wireCircle(dl, vp, center, X, Z, radius, segments, vpMin, vpSize, col, thickness);
    wireCircle(dl, vp, center, Y, Z, radius, segments, vpMin, vpSize, col, thickness);
}

/**
 * @brief Draw a world-space arrow: a line plus a filled triangular head at the tip.
 *
 * Skipped when either end is behind the near plane: a head at a clipped tip points from
 * nowhere.
 *
 * @param dl        Draw list to append to.
 * @param vp        Viewport camera's view-projection.
 * @param from      Tail, world space.
 * @param to        Tip, world space.
 * @param vpMin     Viewport rect's top-left, screen pixels.
 * @param vpSize    Viewport rect's size, screen pixels.
 * @param col       Line and head colour.
 * @param thickness Line width, screen pixels.
 * @param headLen   Head length, screen pixels.
 * @param headWidth Half the head's width, screen pixels.
 */
inline void arrowLine(
    ImDrawList* dl,
    const glm::mat4& vp,
    const glm::vec3& from,
    const glm::vec3& to,
    ImVec2 vpMin,
    ImVec2 vpSize,
    ImU32 col,
    float thickness,
    float headLen,
    float headWidth
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
    dl->AddTriangleFilled(
        b,
        ImVec2(b.x - dv.x * headLen + perp.x * headWidth, b.y - dv.y * headLen + perp.y * headWidth),
        ImVec2(b.x - dv.x * headLen - perp.x * headWidth, b.y - dv.y * headLen - perp.y * headWidth),
        col
    );
}

/**
 * @brief Draw an oriented box as its 12 edges.
 *
 * No Transform scale is applied: pass the exact extents.
 *
 * @param dl        Draw list to append to.
 * @param vp        Viewport camera's view-projection.
 * @param pos       World-space centre.
 * @param rot       Orientation.
 * @param he        Half extents on the box's own axes, world units.
 * @param vpMin     Viewport rect's top-left, screen pixels.
 * @param vpSize    Viewport rect's size, screen pixels.
 * @param col       Line colour.
 * @param thickness Line width, screen pixels.
 */
inline void wireBox(
    ImDrawList* dl,
    const glm::mat4& vp,
    const glm::vec3& pos,
    const glm::quat& rot,
    const glm::vec3& he,
    ImVec2 vpMin,
    ImVec2 vpSize,
    ImU32 col,
    float thickness = EditorStyle::px(1.5f)
) {
    const glm::mat3 r = glm::mat3_cast(rot);
    glm::vec3 c[8];
    int k = 0;
    for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
    for (int sz = -1; sz <= 1; sz += 2)
        c[k++] = pos + r * glm::vec3(he.x * sx, he.y * sy, he.z * sz);

    // Corner bits: bit2 = x, bit1 = y, bit0 = z; an edge joins corners differing in one bit.
    static const int edges[12][2] = {
        {0,1}, {2,3}, {4,5}, {6,7},   // along z
        {0,2}, {1,3}, {4,6}, {5,7},   // along y
        {0,4}, {1,5}, {2,6}, {3,7},   // along x
    };
    for (const auto& e : edges)
        wireSegment(dl, vp, c[e[0]], c[e[1]], vpMin, vpSize, col, thickness);
}

/**
 * @brief Draw a capsule: a segment of 2*halfHeight along the rotation's local +Y, swept by radius.
 *
 * Drawn as two rings, four side lines and four cap arcs.
 *
 * @param dl         Draw list to append to.
 * @param vp         Viewport camera's view-projection.
 * @param center     World-space centre.
 * @param rot        Orientation; the segment runs along its local +Y.
 * @param radius     World units.
 * @param halfHeight Half the segment's length, world units.
 * @param segments   Segments per ring; a cap arc takes half.
 * @param vpMin      Viewport rect's top-left, screen pixels.
 * @param vpSize     Viewport rect's size, screen pixels.
 * @param col        Line colour.
 * @param thickness  Line width, screen pixels.
 */
inline void wireCapsule(
    ImDrawList* dl,
    const glm::mat4& vp,
    const glm::vec3& center,
    const glm::quat& rot,
    float radius,
    float halfHeight,
    int segments,
    ImVec2 vpMin,
    ImVec2 vpSize,
    ImU32 col,
    float thickness = EditorStyle::px(1.5f)
) {
    const glm::mat3 r = glm::mat3_cast(rot);
    const glm::vec3 u = r[0];
    const glm::vec3 axis = r[1];
    const glm::vec3 v = r[2];
    const glm::vec3 top = center + axis * halfHeight;
    const glm::vec3 bottom = center - axis * halfHeight;

    wireCircle(dl, vp, top,    u, v, radius, segments, vpMin, vpSize, col, thickness);
    wireCircle(dl, vp, bottom, u, v, radius, segments, vpMin, vpSize, col, thickness);

    const glm::vec3 sides[4] = { u * radius, u * -radius, v * radius, v * -radius };
    for (const glm::vec3& offset : sides)
        wireSegment(dl, vp, top + offset, bottom + offset, vpMin, vpSize, col, thickness);

    const int capSegments = glm::max(segments / 2, 2);
    const float half = glm::pi<float>();
    wireArc(dl, vp, top,    u,  axis, radius, 0.0f, half, capSegments, vpMin, vpSize, col, thickness);
    wireArc(dl, vp, top,    v,  axis, radius, 0.0f, half, capSegments, vpMin, vpSize, col, thickness);
    wireArc(dl, vp, bottom, u, -axis, radius, 0.0f, half, capSegments, vpMin, vpSize, col, thickness);
    wireArc(dl, vp, bottom, v, -axis, radius, 0.0f, half, capSegments, vpMin, vpSize, col, thickness);
}

} // namespace Vkm::Engine
