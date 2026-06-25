#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "framework/editor_common.h"
#include "system/visibility/visibility.h"
#include "system/camera/camera_controller_system.h"
#include "ecs/component/collider.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/world_transform.h"
#include "core/math/rotation.h"

namespace Engine {

namespace {

constexpr ImU32 COLLIDER_COL = IM_COL32(80, 220, 120, 200);  // physics green
constexpr ImU32 BOUNDS_COL   = IM_COL32(230, 200, 60, 160);  // mesh-bounds amber

// Project a world point through the viewport's view+projection into
// screen coordinates inside the viewport child rect. Returns false when
// the point is behind the camera. The 3D pass renders at viewport size
// and the viewport child sits at vpMin onscreen, so NDC maps to
// (vpMin + (0..vpSize)) directly.
bool projectToViewport(
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

// World position of a (possibly parented) gizmo'd entity: the cached
// WorldTransform when present, the local Transform position otherwise.
glm::vec3 worldPosOf(const Scene& scene, EntityId id, const Transform& tf) {
    if (scene.has<WorldTransform>(id))
        return glm::vec3(scene.get<WorldTransform>(id).model[3]);
    return tf.position;
}

// Draw a world-space segment as a viewport line, dropping it when either end
// is behind the camera (same near-plane handling as the gizmo wireframes -
// good enough for a debug overlay).
void wireSegment(
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
void wireCircle(
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
void wireSphere(
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
void arrowLine(
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

// Box collider: 8 oriented corners, 12 edges. halfExtents are the physics
// extents - Transform scale is intentionally NOT applied because the solver
// ignores it too, so a collider that disagrees with the rendered mesh is
// visible here rather than hidden.
void wireBox(
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

} // namespace

void GizmoOverlay::drawLightGizmos(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    const glm::mat4 vp     = ctx.visibility->projection * ctx.visibility->view;
    const ImVec2    vpMin  = ec.viewportPos;
    const ImVec2    vpSize = ec.viewportSize;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(vpMin, ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y), true);

    ctx.scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& tf) {
        if (!light.enabled) return;
        const bool selected = (ec.state.selectedEntity == id);
        const ImU32 col = selected
            ? EditorStyle::HIGHLIGHT_U32
            : IM_COL32(static_cast<int>(light.color.r * 220),
                       static_cast<int>(light.color.g * 220),
                       static_cast<int>(light.color.b * 220), 200);

        const glm::vec3 pos = worldPosOf(ctx.scene, id, tf);
        const glm::vec3 dir = glm::normalize(Math::computeForward(tf.rotation));

        // Billboard icon at the entity origin so a light is always findable
        // even if the wireframe is tiny or pointed away. Drawn first so the
        // wireframe overlays it; for the user the dot reads as "the light
        // lives here".
        {
            ImVec2 sp;
            if (projectToViewport(vp, pos, vpMin, vpSize, sp)) {
                const float r = 8.0f;
                // Dim disc behind the glyph so the icon reads on any background.
                dl->AddCircleFilled(sp, r + 1.0f, IM_COL32(15, 15, 18, 180), 16);
                const EditorIcon glyph =
                    light.type == LightType::Directional ? EditorIcon::LightDir :
                    light.type == LightType::Point       ? EditorIcon::LightPoint :
                                                            EditorIcon::LightSpot;
                drawEditorIcon(dl, glyph, sp, r * 0.85f, col);
            }
        }

        switch (light.type) {
            case LightType::Directional: {
                // Sun gizmo: a small disc at the light origin plus three
                // parallel rays in the forward direction. The triangular
                // offset reads as "parallel rays" instead of a single arrow
                // (which always looked more like a spotlight).
                const float L      = 1.5f;          // ray length (world units)
                const float spread = 0.18f;         // lateral offset of side rays
                const float discR  = 0.10f;         // sun disc radius

                // Build an orthonormal basis in the plane perpendicular to dir
                // so the three rays are coplanar with that plane.
                const glm::vec3 up = std::abs(dir.y) < 0.99f
                    ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                const glm::vec3 right = glm::normalize(glm::cross(dir, up));
                const glm::vec3 udir  = glm::normalize(glm::cross(right, dir));

                const glm::vec3 offsets[3] = {
                    glm::vec3(0.0f),
                    right *  spread + udir *  spread * 0.5f,
                    right * -spread + udir *  spread * 0.5f,
                };

                // Disc outline (perpendicular to dir) so the user can see the
                // light origin distinctly from the rays.
                wireCircle(dl, vp, pos, right, udir, discR, 16, vpMin, vpSize, col, 1.5f);

                // Three parallel arrows from the disc plane forward.
                for (const glm::vec3& off : offsets) {
                    const glm::vec3 start = pos + off;
                    arrowLine(dl, vp, start, start + dir * L, vpMin, vpSize,
                              col, 2.0f, 12.0f, 6.0f);
                }
                break;
            }
            case LightType::Point: {
                // Three orthogonal great-circle wireframes at radius.
                const float r = std::max(0.05f, light.radius);
                wireSphere(dl, vp, pos, r, 32, vpMin, vpSize, col, 1.0f);
                break;
            }
            case LightType::Spot: {
                // Cone: apex at pos, base circle of half-angle outerCone at radius.
                const float r = std::max(0.05f, light.radius);
                const float half = light.outerConeAngle;
                const float baseR = std::tan(half) * r;
                const glm::vec3 baseC = pos + dir * r;

                // Basis perpendicular to dir.
                glm::vec3 tangent = std::abs(dir.y) < 0.99f
                    ? glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)))
                    : glm::normalize(glm::cross(dir, glm::vec3(1, 0, 0)));
                glm::vec3 bitangent = glm::cross(dir, tangent);

                // Base ring + 4 spokes from the apex to its quarter points.
                wireCircle(dl, vp, baseC, tangent, bitangent, baseR, 32,
                           vpMin, vpSize, col, 1.0f);

                ImVec2 apexSp;
                if (projectToViewport(vp, pos, vpMin, vpSize, apexSp)) {
                    for (int k = 0; k < 4; ++k) {
                        const float t = k * glm::half_pi<float>();
                        const glm::vec3 p = baseC
                            + (tangent * std::cos(t) + bitangent * std::sin(t)) * baseR;
                        ImVec2 sp;
                        if (projectToViewport(vp, p, vpMin, vpSize, sp))
                            dl->AddLine(apexSp, sp, col, 1.0f);
                    }
                }
                break;
            }
            case LightType::Rect:
            case LightType::Disk: {
                // Wireframe emitter outline. axisU = local +X * width/2 (Rect)
                // or +X * radius (Disk); axisV = local +Y similarly. Matches
                // render_view.cpp's GPU packing so the gizmo agrees with the
                // shaded result.
                const bool isRect = (light.type == LightType::Rect);
                const float ux = isRect ? light.areaWidth  * 0.5f : light.areaRadius;
                const float uy = isRect ? light.areaHeight * 0.5f : light.areaRadius;
                const glm::vec3 right = tf.rotation * glm::vec3(1, 0, 0) * ux;
                const glm::vec3 up    = tf.rotation * glm::vec3(0, 1, 0) * uy;

                if (isRect) {
                    const glm::vec3 corners[4] = {
                        pos - right - up,
                        pos + right - up,
                        pos + right + up,
                        pos - right + up,
                    };
                    ImVec2 sp[4];
                    bool   ok[4];
                    for (int i = 0; i < 4; ++i) {
                        ok[i] = projectToViewport(vp, corners[i], vpMin, vpSize, sp[i]);
                    }
                    for (int i = 0; i < 4; ++i) {
                        const int j = (i + 1) & 3;
                        if (ok[i] && ok[j]) dl->AddLine(sp[i], sp[j], col, 1.5f);
                    }
                } else {
                    // Disk: right/up already carry the radius, so unit radius here.
                    wireCircle(dl, vp, pos, right, up, 1.0f, 32, vpMin, vpSize, col, 1.5f);
                }

                // Emission arrow toward the lit hemisphere (+dir). Two-sided
                // emitters get a second arrow on the back so the user can see
                // the emission is bidirectional.
                arrowLine(dl, vp, pos, pos + dir * 0.5f, vpMin, vpSize, col, 1.5f, 8.5f, 4.0f);
                if (light.twoSided)
                    arrowLine(dl, vp, pos, pos - dir * 0.5f, vpMin, vpSize, col, 1.5f, 8.5f, 4.0f);

                // Attenuation-cutoff sphere: the distance beyond which the
                // light contributes nothing. Drawn dimmer / thinner than the
                // emitter outline so the silhouette reads as the actual
                // emitter shape and the sphere reads as a falloff hint
                // (matches how Point's 3 great-circle gizmo communicates the
                // same data).
                const float rr = std::max(0.05f, light.radius);
                const ImU32 fade = selected
                    ? IM_COL32(255, 200, 80, 90)
                    : IM_COL32(static_cast<int>(light.color.r * 200),
                               static_cast<int>(light.color.g * 200),
                               static_cast<int>(light.color.b * 200), 80);
                wireSphere(dl, vp, pos, rr, 24, vpMin, vpSize, fade, 1.0f);
                break;
            }
        }
    });

    dl->PopClipRect();
}

void GizmoOverlay::drawProbeGizmos(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    const glm::mat4 vp = ctx.visibility->projection * ctx.visibility->view;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ec.viewportPos,
        ImVec2(ec.viewportPos.x + ec.viewportSize.x,
               ec.viewportPos.y + ec.viewportSize.y), true);

    ctx.scene.forEach<ReflectionProbe, Transform>([&](EntityId id, const ReflectionProbe& probe, const Transform& tf) {
        const bool  selected = (ec.state.selectedEntity == id);
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IM_COL32(77, 158, 235, 200);

        const glm::vec3 pos = worldPosOf(ctx.scene, id, tf);
        const glm::vec3 e   = probe.halfExtents;

        // The world-axis-aligned influence box (wireBox with no rotation).
        wireBox(dl, vp, pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), e,
                ec.viewportPos, ec.viewportSize, col, selected ? 2.0f : 1.5f);

        // Centre marker: the point the probe captures the scene from.
        ImVec2 sp;
        if (projectToViewport(vp, pos, ec.viewportPos, ec.viewportSize, sp))
            dl->AddCircleFilled(sp, selected ? 4.0f : 3.0f, col);
    });

    dl->PopClipRect();
}

void GizmoOverlay::drawCameraGizmos(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    const glm::mat4 vp = ctx.visibility->projection * ctx.visibility->view;
    const EntityId activeCamId = ec.cameraController.getCameraEntity().getID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ec.viewportPos,
        ImVec2(ec.viewportPos.x + ec.viewportSize.x,
               ec.viewportPos.y + ec.viewportSize.y), true);

    ctx.scene.forEach<Camera, Transform>([&](EntityId id, const Camera& cam, const Transform& tf) {
        // The active editor camera *is* the viewer - drawing a frustum
        // there would put a gizmo inside the user's eye. Skip it.
        if (id == activeCamId) return;

        const bool selected = (ec.state.selectedEntity == id);
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IM_COL32(120, 200, 220, 220);
        // Dimmer fill for the near/far plane "infill" edges so the apex,
        // far rectangle, and the up-tab read as the primary silhouette.
        const ImU32 colDim = selected
            ? IM_COL32(255, 200, 80, 130)
            : IM_COL32(120, 200, 220, 140);

        const glm::vec3 pos   = worldPosOf(ctx.scene, id, tf);
        const glm::vec3 fwd   = glm::normalize(Math::computeForward(tf.rotation));
        const glm::vec3 right = glm::normalize(glm::cross(fwd,
            std::abs(fwd.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
        const glm::vec3 up    = glm::normalize(glm::cross(right, fwd));

        // Use the camera's actual near and far so the gizmo reflects what
        // the camera really clips. Clamp the minimums so degenerate values
        // don't produce zero-extent rectangles. The drawn lines extend off
        // viewport for cameras with large zFar - that's correct; the
        // projection clipping in projectToViewport handles it.
        const float zNear = std::max(0.001f, cam.zNear);
        const float zFar  = std::max(zNear + 0.001f, cam.zFar);

        const float aspect = ec.viewportSize.y > 1.0f
            ? ec.viewportSize.x / ec.viewportSize.y : 16.0f / 9.0f;

        // Half-extents at each plane. Perspective fans out with depth;
        // ortho stays the same size at both planes (its rect is fixed by
        // orthoHeight).
        float halfHnear, halfWnear, halfHfar, halfWfar;
        if (cam.projection == ProjectionType::Perspective) {
            const float t = std::tan(cam.fovY * 0.5f);
            halfHnear = t * zNear;  halfWnear = halfHnear * aspect;
            halfHfar  = t * zFar;   halfWfar  = halfHfar  * aspect;
        } else {
            halfHnear = halfHfar = cam.orthoHeight * 0.5f;
            halfWnear = halfWfar = halfHnear * aspect;
        }

        const glm::vec3 cNear = pos + fwd * zNear;
        const glm::vec3 cFar  = pos + fwd * zFar;
        const glm::vec3 nearCorners[4] = {
            cNear + right *  halfWnear + up *  halfHnear,  // top-right
            cNear + right * -halfWnear + up *  halfHnear,  // top-left
            cNear + right * -halfWnear + up * -halfHnear,  // bottom-left
            cNear + right *  halfWnear + up * -halfHnear,  // bottom-right
        };
        const glm::vec3 farCorners[4] = {
            cFar + right *  halfWfar + up *  halfHfar,
            cFar + right * -halfWfar + up *  halfHfar,
            cFar + right * -halfWfar + up * -halfHfar,
            cFar + right *  halfWfar + up * -halfHfar,
        };

        ImVec2 apexSp;
        bool haveApex = projectToViewport(vp, pos, ec.viewportPos, ec.viewportSize, apexSp);

        ImVec2 nearSp[4]{};
        bool haveNear[4] = {};
        ImVec2 farSp[4]{};
        bool haveFar[4] = {};
        for (int i = 0; i < 4; ++i) {
            haveNear[i] = projectToViewport(vp, nearCorners[i],
                ec.viewportPos, ec.viewportSize, nearSp[i]);
            haveFar[i]  = projectToViewport(vp, farCorners[i],
                ec.viewportPos, ec.viewportSize, farSp[i]);
        }

        // Perspective: spokes from apex to near corners (gives the "FOV
        // converges here" cue). Ortho skips them - the parallel near/far
        // edges already say "ortho".
        if (cam.projection == ProjectionType::Perspective && haveApex) {
            for (int i = 0; i < 4; ++i) {
                if (haveNear[i]) dl->AddLine(apexSp, nearSp[i], colDim, 1.0f);
            }
        }

        // Near rectangle (dim - it's small and close to the apex, so it
        // reads as supporting detail).
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            if (haveNear[i] && haveNear[j])
                dl->AddLine(nearSp[i], nearSp[j], colDim, 1.0f);
        }

        // 4 edges connecting near and far corners (the frustum sides).
        for (int i = 0; i < 4; ++i) {
            if (haveNear[i] && haveFar[i])
                dl->AddLine(nearSp[i], farSp[i], col, 1.0f);
        }

        // Far rectangle (primary silhouette of the camera's reach).
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            if (haveFar[i] && haveFar[j])
                dl->AddLine(farSp[i], farSp[j], col, 1.0f);
        }

        // "Up" indicator on the far face: small triangular tab on the top
        // edge so the camera's roll is visible at a glance.
        if (haveFar[0] && haveFar[1]) {
            const ImVec2 mid((farSp[0].x + farSp[1].x) * 0.5f,
                             (farSp[0].y + farSp[1].y) * 0.5f);
            const ImVec2 tab(mid.x, mid.y - 8.0f);
            dl->AddTriangleFilled(tab,
                ImVec2(mid.x - 5.0f, mid.y),
                ImVec2(mid.x + 5.0f, mid.y), col);
        }

        // Billboard camera icon at the position.
        if (haveApex) {
            const float r = 8.0f;
            dl->AddCircleFilled(apexSp, r + 1.0f, IM_COL32(15, 15, 18, 180), 16);
            drawEditorIcon(dl, EditorIcon::Camera, apexSp, r * 0.85f, col);
        }
    });

    dl->PopClipRect();
}

void GizmoOverlay::drawColliderGizmos(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    const glm::mat4 vp     = ctx.visibility->projection * ctx.visibility->view;
    const ImVec2    vpMin  = ec.viewportPos;
    const ImVec2    vpSize = ec.viewportSize;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(vpMin, ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y), true);

    // Physics evaluates a collider in the entity's Transform frame - position +
    // rotation, no scale (see PhysicsSystem). Draw it the same way so the
    // wireframe is exactly what the solver collides against.
    ctx.scene.forEach<Collider, Transform>([&](EntityId id, const Collider& col, const Transform& tf) {
        const bool   selected = (ec.state.selectedEntity == id);
        const ImU32  color    = selected ? EditorStyle::HIGHLIGHT_U32 : COLLIDER_COL;
        const glm::mat3 r = glm::mat3_cast(tf.rotation);
        for (const ColliderBox& part : col.parts)
            wireBox(dl, vp, tf.position + r * part.center, tf.rotation,
                    part.halfExtents, vpMin, vpSize, color);
    });

    dl->PopClipRect();
}

void GizmoOverlay::drawBoundsGizmos(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    const glm::mat4 vp     = ctx.visibility->projection * ctx.visibility->view;
    const ImVec2    vpMin  = ec.viewportPos;
    const ImVec2    vpSize = ec.viewportSize;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(vpMin, ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y), true);

    // World-space AABB of every visible entity, already computed by the
    // visibility pass. This used to be an engine render pass; it's an editor
    // overlay now (an axis-aligned box is wireBox with no rotation).
    for (const VisibleEntity& e : ctx.visibility->entries) {
        if (e.worldMin == e.worldMax) continue;
        const glm::vec3 center = (e.worldMin + e.worldMax) * 0.5f;
        const glm::vec3 he     = (e.worldMax - e.worldMin) * 0.5f;
        wireBox(dl, vp, center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), he, vpMin, vpSize, BOUNDS_COL);
    }

    dl->PopClipRect();
}

void GizmoOverlay::drawSelectionOutline(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    const EntityId sel = ec.state.selectedEntity;
    if (!sel || !ctx.scene.isAlive(sel)) return;

    const glm::mat4 vp     = ctx.visibility->projection * ctx.visibility->view;
    const ImVec2    vpMin  = ec.viewportPos;
    const ImVec2    vpSize = ec.viewportSize;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(vpMin, ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y), true);

    // Outline the selected entity's world AABB. Only mesh entities are in the
    // visible set; lights / probes / cameras highlight their own gizmos instead.
    for (const VisibleEntity& e : ctx.visibility->entries) {
        if (e.id != sel || e.worldMin == e.worldMax) continue;
        const glm::vec3 center = (e.worldMin + e.worldMax) * 0.5f;
        const glm::vec3 he     = (e.worldMax - e.worldMin) * 0.5f;
        wireBox(dl, vp, center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), he, vpMin, vpSize, EditorStyle::HIGHLIGHT_U32);
        break;
    }

    dl->PopClipRect();
}

} // namespace Engine
