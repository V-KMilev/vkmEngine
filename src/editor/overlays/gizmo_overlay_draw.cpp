#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "framework/editor_common.h"
#include "overlays/wire_draw.h"
#include "system/visibility/visibility.h"
#include "system/camera/camera_controller_system.h"
#include "ecs/component/collider.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/irradiance_volume.h"
#include "ecs/component/decal.h"
#include "ecs/component/particle_emitter.h"
#include "ecs/component/world_transform.h"
#include "core/math/rotation.h"

namespace Engine {

namespace {

constexpr ImU32 COLLIDER_COL = IM_COL32(80, 220, 120, 200);  // physics green
constexpr ImU32 BOUNDS_COL   = IM_COL32(230, 200, 60, 160);  // mesh-bounds amber

constexpr ImU32 IRRADIANCE_VOLUME_COL = IM_COL32(235, 150, 77, 200);  // GI orange, against the probe's blue

constexpr ImU32 DECAL_COL   = IM_COL32(200, 120, 220, 200);  // decal violet
constexpr ImU32 EMITTER_COL = IM_COL32(240, 200, 90, 200);   // particle amber

// A dense volume would bury the viewport under thousands of dots, so past this
// the box alone has to speak for it.
constexpr uint32_t MAX_DRAWN_PROBES = 4096;

// RAII scope shared by every viewport gizmo pass: it caches the view-projection
// and drawlist, and pushes/pops the viewport clip rect. valid() is false when
// there is no camera to project through, in which case nothing was pushed and
// the caller must return early.
struct ViewportOverlayScope {
    explicit ViewportOverlayScope(EditorContext& ec) {
        const FrameContext& ctx = ec.frame;
        if (!ctx.visibility || !ctx.visibility->hasCamera) return;
        vp     = ctx.visibility->projection * ctx.visibility->view;
        vpMin  = ec.viewportPos;
        vpSize = ec.viewportSize;
        dl     = ImGui::GetWindowDrawList();
        dl->PushClipRect(vpMin, ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y), true);
    }

    ~ViewportOverlayScope() {
        if (dl) dl->PopClipRect();
    }

    ViewportOverlayScope(const ViewportOverlayScope&) = delete;
    ViewportOverlayScope& operator=(const ViewportOverlayScope&) = delete;

    bool valid() const { return dl != nullptr; }

    glm::mat4   vp{1.0f};
    ImVec2      vpMin{0, 0};
    ImVec2      vpSize{0, 0};
    ImDrawList* dl = nullptr;
};

} // namespace

void GizmoOverlay::drawLightGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const glm::mat4 vp     = scope.vp;
    const ImVec2    vpMin  = scope.vpMin;
    const ImVec2    vpSize = scope.vpSize;
    ImDrawList*     dl     = scope.dl;

    ec.frame.scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& tf) {
        if (!light.enabled) return;
        const bool selected = (ec.state.isSelected(id));
        const ImU32 col = selected
            ? EditorStyle::HIGHLIGHT_U32
            : IM_COL32(static_cast<int>(light.color.r * 220),
                       static_cast<int>(light.color.g * 220),
                       static_cast<int>(light.color.b * 220), 200);

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        const glm::quat rot = resolvedWorldRotation(ec.frame.scene, id, tf);
        const glm::vec3 dir = glm::normalize(Math::computeForward(rot));

        // Billboard icon at the entity origin so a light is always findable
        // even if the wireframe is tiny or pointed away. Drawn first so the
        // wireframe overlays it.
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
                glm::vec3 right, udir;
                orthoBasis(dir, right, udir);

                const glm::vec3 offsets[3] = {
                    glm::vec3(0.0f),
                    right *  spread + udir *  spread * 0.5f,
                    right * -spread + udir *  spread * 0.5f,
                };

                // Disc outline (perpendicular to dir) so the user can see the
                // light origin distinctly from the rays.
                wireCircle(dl, vp, pos, right, udir, discR, 16, vpMin, vpSize, col, 1.5f);

                for (const glm::vec3& off : offsets) {
                    const glm::vec3 start = pos + off;
                    arrowLine(dl, vp, start, start + dir * L, vpMin, vpSize,
                              col, 2.0f, 12.0f, 6.0f);
                }
                break;
            }
            case LightType::Point: {
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
                const glm::vec3 right = rot * glm::vec3(1, 0, 0) * ux;
                const glm::vec3 up    = rot * glm::vec3(0, 1, 0) * uy;

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
            case LightType::Count: break;
        }
    });
}

void GizmoOverlay::drawProbeGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const glm::mat4 vp = scope.vp;
    ImDrawList*     dl = scope.dl;

    ec.frame.scene.forEach<ReflectionProbe, Transform>([&](EntityId id, const ReflectionProbe& probe, const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IM_COL32(77, 158, 235, 200);

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        const glm::vec3 e   = probe.halfExtents;

        // The world-axis-aligned influence box (wireBox with no rotation).
        wireBox(dl, vp, pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), e,
                ec.viewportPos, ec.viewportSize, col, selected ? 2.0f : 1.5f);

        // Centre marker: the point the probe captures the scene from.
        ImVec2 sp;
        if (projectToViewport(vp, pos, ec.viewportPos, ec.viewportSize, sp))
            dl->AddCircleFilled(sp, selected ? 4.0f : 3.0f, col);
    });

    ec.frame.scene.forEach<IrradianceVolume, Transform>([&](EntityId id, const IrradianceVolume& volume,
                                                            const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IRRADIANCE_VOLUME_COL;

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);

        wireBox(dl, vp, pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), volume.halfExtents,
                ec.viewportPos, ec.viewportSize, col, selected ? 2.0f : 1.5f);

        // The probe grid itself is only worth the clutter for the selected volume -
        // it is what tells you whether the resolution actually covers the geometry.
        if (!selected) return;

        const glm::uvec3 res(volume.resolutionX, volume.resolutionY, volume.resolutionZ);
        if (res.x * res.y * res.z > MAX_DRAWN_PROBES) return;

        const glm::vec3 boxMin  = pos - volume.halfExtents;
        const glm::vec3 boxSize = volume.halfExtents * 2.0f;
        const glm::vec3 resf(res);

        for (uint32_t z = 0; z < res.z; ++z) {
            for (uint32_t y = 0; y < res.y; ++y) {
                for (uint32_t x = 0; x < res.x; ++x) {
                    // Texel centres - the exact positions the baker captures from.
                    const glm::vec3 t = (glm::vec3(x, y, z) + 0.5f) / resf;
                    ImVec2 pp;
                    if (projectToViewport(vp, boxMin + boxSize * t, ec.viewportPos, ec.viewportSize, pp))
                        dl->AddCircleFilled(pp, 2.0f, col);
                }
            }
        }
    });
}

void GizmoOverlay::drawEffectGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const glm::mat4 vp = scope.vp;
    ImDrawList*     dl = scope.dl;

    ec.frame.scene.forEach<Decal, Transform>([&](EntityId id, const Decal&, const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : DECAL_COL;

        // The Transform's scale IS the projection box (a unit cube), so the
        // box gizmo is the decal's whole authoring model.
        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        wireBox(dl, vp, pos, tf.rotation, tf.scale * 0.5f,
                ec.viewportPos, ec.viewportSize, col, selected ? 2.0f : 1.5f);

        // Projection direction: decals project along the entity's forward.
        const glm::vec3 fwd = Math::computeForward(tf.rotation);
        ImVec2 a, b;
        if (projectToViewport(vp, pos, ec.viewportPos, ec.viewportSize, a) &&
            projectToViewport(vp, pos + fwd * (tf.scale.z * 0.75f), ec.viewportPos, ec.viewportSize, b))
            dl->AddLine(a, b, col, selected ? 2.0f : 1.5f);
    });

    ec.frame.scene.forEach<ParticleEmitter, Transform>([&](EntityId id, const ParticleEmitter& e,
                                                           const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : EMITTER_COL;

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        ImVec2 sp;
        if (!projectToViewport(vp, pos, ec.viewportPos, ec.viewportSize, sp)) return;
        dl->AddCircle(sp, selected ? 6.0f : 5.0f, col, 0, selected ? 2.0f : 1.5f);
        dl->AddCircleFilled(sp, 2.0f, col);

        // Initial-velocity direction, so the spray's aim reads at a glance.
        const float speed = glm::length(e.velocity);
        if (speed > 1e-4f) {
            ImVec2 tip;
            if (projectToViewport(vp, pos + (e.velocity / speed) * 0.75f,
                                  ec.viewportPos, ec.viewportSize, tip))
                dl->AddLine(sp, tip, col, selected ? 2.0f : 1.5f);
        }
    });
}

void GizmoOverlay::drawCameraGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const glm::mat4 vp = scope.vp;
    ImDrawList*     dl = scope.dl;
    const EntityId activeCamId = ec.cameraController.getCameraEntity();

    ec.frame.scene.forEach<Camera, Transform>([&](EntityId id, const Camera& cam, const Transform& tf) {
        // The active editor camera *is* the viewer - drawing a frustum
        // there would put a gizmo inside the user's eye. Skip it.
        if (id == activeCamId) return;

        const bool selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IM_COL32(120, 200, 220, 220);
        // Dimmer fill for the near/far plane "infill" edges so the apex,
        // far rectangle, and the up-tab read as the primary silhouette.
        const ImU32 colDim = selected
            ? IM_COL32(255, 200, 80, 130)
            : IM_COL32(120, 200, 220, 140);

        const glm::vec3 pos   = resolvedWorldPosition(ec.frame.scene, id, tf);
        const glm::vec3 fwd   = glm::normalize(Math::computeForward(tf.rotation));
        glm::vec3 right, up;
        orthoBasis(fwd, right, up);

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

        if (haveApex) {
            const float r = 8.0f;
            dl->AddCircleFilled(apexSp, r + 1.0f, IM_COL32(15, 15, 18, 180), 16);
            drawEditorIcon(dl, EditorIcon::Camera, apexSp, r * 0.85f, col);
        }
    });
}

void GizmoOverlay::drawColliderGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const glm::mat4 vp     = scope.vp;
    const ImVec2    vpMin  = scope.vpMin;
    const ImVec2    vpSize = scope.vpSize;
    ImDrawList*     dl     = scope.dl;

    // Physics evaluates a collider in the entity's WORLD frame - position +
    // rotation, no scale (see PhysicsSystem, which reads the WorldTransform for
    // a parented body). Draw it the same way so the wireframe is exactly what
    // the solver collides against.
    ec.frame.scene.forEach<Collider, Transform>([&](EntityId id, const Collider& col, const Transform& tf) {
        if (!col.enabled) return;   // inert colliders don't collide, so don't draw them
        const bool   selected = (ec.state.isSelected(id));
        const ImU32  color    = selected ? EditorStyle::HIGHLIGHT_U32 : COLLIDER_COL;
        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        const glm::quat rot = resolvedWorldRotation(ec.frame.scene, id, tf);
        const glm::mat3 r   = glm::mat3_cast(rot);
        for (const ColliderBox& part : col.parts)
            wireBox(dl, vp, pos + r * part.center, rot,
                    part.halfExtents, vpMin, vpSize, color);
    });
}

void GizmoOverlay::drawBoundsGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const glm::mat4 vp     = scope.vp;
    const ImVec2    vpMin  = scope.vpMin;
    const ImVec2    vpSize = scope.vpSize;
    ImDrawList*     dl     = scope.dl;

    // World-space AABB of every visible entity, already computed by the
    // visibility pass. This used to be an engine render pass; it's an editor
    // overlay now (an axis-aligned box is wireBox with no rotation).
    for (const VisibleEntity& e : ec.frame.visibility->entries) {
        if (e.worldMin == e.worldMax) continue;
        const glm::vec3 center = (e.worldMin + e.worldMax) * 0.5f;
        const glm::vec3 he     = (e.worldMax - e.worldMin) * 0.5f;
        wireBox(dl, vp, center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), he, vpMin, vpSize, BOUNDS_COL);
    }
}

void GizmoOverlay::drawSelectionOutline(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    if (ec.state.selection.empty()) return;

    const glm::mat4 vp     = scope.vp;
    const ImVec2    vpMin  = scope.vpMin;
    const ImVec2    vpSize = scope.vpSize;
    ImDrawList*     dl     = scope.dl;

    // Outline every selected entity's world AABB; the active one gets the
    // full highlight, the rest a dimmer tint. Only mesh entities are in the
    // visible set; lights / probes / cameras highlight their own gizmos.
    const ImU32 secondary = IM_COL32(255, 210, 50, 130);
    for (const VisibleEntity& e : ec.frame.visibility->entries) {
        if (e.worldMin == e.worldMax || !ec.state.isSelected(e.id)) continue;
        const glm::vec3 center = (e.worldMin + e.worldMax) * 0.5f;
        const glm::vec3 he     = (e.worldMax - e.worldMin) * 0.5f;
        const ImU32 col = (e.id == ec.state.selectedEntity)
            ? EditorStyle::HIGHLIGHT_U32 : secondary;
        wireBox(dl, vp, center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), he, vpMin, vpSize, col);
    }
}

} // namespace Engine
