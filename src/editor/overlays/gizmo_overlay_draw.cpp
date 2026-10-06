#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <glm/glm.hpp>

#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "ui/editor_icons.h"
#include "ui/editor_widgets.h"
#include "overlays/wire_draw.h"
#include "ui/editor_style.h"
#include "system/visibility/visibility.h"
#include "system/animation/pose_buffer.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "core/math/rotation.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

constexpr ImU32 COLLIDER_COL = IM_COL32(80, 220, 120, 200);  // physics green

constexpr int COLLIDER_CAPSULE_SEGMENTS = 24;
constexpr ImU32 BOUNDS_COL   = IM_COL32(230, 200, 60, 160);  // mesh-bounds amber

constexpr ImU32 IRRADIANCE_VOLUME_COL = IM_COL32(235, 150, 77, 200);  // GI orange, against the probe's blue

constexpr ImU32 SKELETON_COL = IM_COL32(120, 190, 255, 230);  // rig blue
constexpr ImU32 JOINT_COL    = IM_COL32(255, 170, 60, 220);   // joint amber, against physics green

constexpr ImU32 AXIS_COLS[3] = {EditorStyle::AXIS_X_U32, EditorStyle::AXIS_Y_U32, EditorStyle::AXIS_Z_U32};

constexpr ImU32 DECAL_COL   = IM_COL32(200, 120, 220, 200);  // decal violet
constexpr ImU32 EMITTER_COL = IM_COL32(240, 200, 90, 200);   // particle amber

// Accent::AUDIO's magenta, nudged clear of DECAL_COL's violet.
constexpr ImU32 AUDIO_COL = IM_COL32(232, 62, 208, 220);  // audio magenta
// Faded: the outer falloff sphere, and anything not contributing (no clip, not the ear).
constexpr ImU32 AUDIO_COL_DIM = IM_COL32(232, 62, 208, 80);

// Past this a volume draws its box alone, or its dots would bury the viewport.
constexpr uint32_t MAX_DRAWN_PROBES = 4096;

// RAII viewport gizmo pass: caches view-projection and drawlist, pushes/pops the clip rect.
// valid() is false with no camera, nothing pushed; the caller must return.
struct ViewportOverlayScope {
    explicit ViewportOverlayScope(EditorContext& ec) {
        const FrameContext& ctx = ec.frame;
        if (!ctx.visibility || !ctx.visibility->hasCamera) return;
        vp     = ctx.visibility->camera.viewProjection;
        vpMin  = ec.viewportPos;
        vpSize = ec.viewportSize;
        dl     = ImGui::GetWindowDrawList();
        dl->PushClipRect(vpMin, ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y), true);
    }

    ~ViewportOverlayScope() {
        if (dl) dl->PopClipRect();
    }

    ViewportOverlayScope(const ViewportOverlayScope& other) = delete;
    ViewportOverlayScope& operator=(const ViewportOverlayScope& other) = delete;

    ViewportOverlayScope(ViewportOverlayScope && other) = delete;
    ViewportOverlayScope& operator=(ViewportOverlayScope && other) = delete;

    bool valid() const { return dl != nullptr; }

    void segment(
        const glm::vec3& a,
        const glm::vec3& b,
        ImU32 col,
        float thickness = EditorStyle::px(1.0f)
    ) const {
        wireSegment(dl, vp, a, b, vpMin, vpSize, col, thickness);
    }

    void arc(
        const glm::vec3& center,
        const glm::vec3& axisA,
        const glm::vec3& axisB,
        float radius,
        float from,
        float to,
        int segments,
        ImU32 col,
        float thickness = EditorStyle::px(1.0f)
    ) const {
        wireArc(dl, vp, center, axisA, axisB, radius, from, to, segments, vpMin, vpSize, col, thickness);
    }

    void circle(
        const glm::vec3& center,
        const glm::vec3& axisA,
        const glm::vec3& axisB,
        float radius,
        int segments,
        ImU32 col,
        float thickness = EditorStyle::px(1.0f)
    ) const {
        wireCircle(dl, vp, center, axisA, axisB, radius, segments, vpMin, vpSize, col, thickness);
    }

    void sphere(
        const glm::vec3& center,
        float radius,
        int segments,
        ImU32 col,
        float thickness = EditorStyle::px(1.0f)
    ) const {
        wireSphere(dl, vp, center, radius, segments, vpMin, vpSize, col, thickness);
    }

    void arrow(
        const glm::vec3& from,
        const glm::vec3& to,
        ImU32 col,
        float thickness,
        float headLen,
        float headWidth
    ) const {
        arrowLine(dl, vp, from, to, vpMin, vpSize, col, thickness, headLen, headWidth);
    }

    void box(
        const glm::vec3& pos,
        const glm::quat& rot,
        const glm::vec3& halfExtents,
        ImU32 col,
        float thickness = EditorStyle::px(1.5f)
    ) const {
        wireBox(dl, vp, pos, rot, halfExtents, vpMin, vpSize, col, thickness);
    }

    void capsule(
        const glm::vec3& center,
        const glm::quat& rot,
        float radius,
        float halfHeight,
        int segments,
        ImU32 col,
        float thickness = EditorStyle::px(1.5f)
    ) const {
        wireCapsule(dl, vp, center, rot, radius, halfHeight, segments, vpMin, vpSize, col, thickness);
    }

    /// A world point's screen position, or false when behind the eye.
    bool project(const glm::vec3& p, ImVec2& out) const {
        return projectToViewport(vp, p, vpMin, vpSize, out);
    }

    glm::mat4   vp{1.0f};
    ImVec2      vpMin{0, 0};
    ImVec2      vpSize{0, 0};
    ImDrawList* dl = nullptr;
};

void drawMeshColliderWires(
    const ViewportOverlayScope& scope,
    const Collider& col,
    const glm::vec3& center,
    const glm::mat3& r,
    ImU32 color
) {
    // Stepped past this; low, as each triangle is three lines of four draw-list vertices.
    constexpr uint32_t MAX_DRAWN = 400;
    const uint32_t triangles = static_cast<uint32_t>(col.meshPoints.size() / 3);
    const uint32_t step = triangles > MAX_DRAWN ? triangles / MAX_DRAWN : 1;

    for (uint32_t t = 0; t < triangles; t += step) {
        const uint32_t base = t * 3;
        const glm::vec3 a = center + r * col.meshPoints[base + 0];
        const glm::vec3 b = center + r * col.meshPoints[base + 1];
        const glm::vec3 c = center + r * col.meshPoints[base + 2];
        scope.segment(a, b, color, EditorStyle::px(1.0f));
        scope.segment(b, c, color, EditorStyle::px(1.0f));
        scope.segment(c, a, color, EditorStyle::px(1.0f));
    }
}
} // namespace

void GizmoOverlay::markEntity(
    ImDrawList* dl,
    EditorIcon icon,
    EntityId id,
    ImVec2 screen,
    const glm::vec3& world,
    ImU32 col,
    float reach
) {
    drawEntityMarker(dl, icon, screen, col);

    // Stamped with the frame: an earlier frame's list holds markers not drawn this time.
    const int frame = ImGui::GetFrameCount();
    if (frame != m_markerFrame) {
        m_markers.clear();
        m_markerFrame = frame;
    }
    m_markers.push_back({id, screen, world, reach});
}

void GizmoOverlay::drawLightGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    ec.frame.scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& tf) {
        if (!light.enabled) return;
        const bool selected = (ec.state.isSelected(id));
        const ImU32 col = selected
            ? EditorStyle::HIGHLIGHT_U32
            : IM_COL32(
                static_cast<int>(light.color.r * 220),
                static_cast<int>(light.color.g * 220),
                static_cast<int>(light.color.b * 220),
                200
            );

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        const glm::quat rot = resolvedWorldRotation(ec.frame.scene, id, tf);
        const glm::vec3 dir = glm::normalize(Math::computeForward(rot));

        // Drawn first so the wireframe overlays it.
        {
            ImVec2 sp;
            if (scope.project(pos, sp)) {
                const EditorIcon glyph = lightIcon(light.type);
                // A box scaled with the light's reach answers a click too; a directional's is fixed.
                const float reach = light.type == LightType::Directional
                    ? 0.5f : std::clamp(light.radius * 0.2f, 0.3f, 3.0f);
                markEntity(dl, glyph, id, sp, pos, col, reach);
            }
        }

        switch (light.type) {
            case LightType::Directional: {
                const float rayLength = 1.5f;
                const float spread    = 0.18f;      // lateral offset of side rays
                const float discR     = 0.10f;      // sun disc radius

                glm::vec3 right, udir;
                orthoBasis(dir, right, udir);

                const glm::vec3 offsets[3] = {
                    glm::vec3(0.0f),
                    right *  spread + udir *  spread * 0.5f,
                    right * -spread + udir *  spread * 0.5f,
                };

                scope.circle(pos, right, udir, discR, 16, col, EditorStyle::px(1.5f));

                for (const glm::vec3& off : offsets) {
                    const glm::vec3 start = pos + off;
                    scope.arrow(
                        start,
                        start + dir * rayLength,
                        col,
                        EditorStyle::px(2.0f),
                        EditorStyle::px(12.0f),
                        EditorStyle::px(6.0f)
                    );
                }
                break;
            }
            case LightType::Point: {
                const float r = std::max(0.05f, light.radius);
                scope.sphere(pos, r, 32, col);
                break;
            }
            case LightType::Spot: {
                // Cone: apex at pos, base circle of half-angle outerCone at radius.
                const float r = std::max(0.05f, light.radius);
                const float half = light.outerConeAngle;
                const float baseR = std::tan(half) * r;
                const glm::vec3 baseC = pos + dir * r;

                glm::vec3 tangent, bitangent;
                orthoBasis(dir, tangent, bitangent);

                scope.circle(baseC, tangent, bitangent, baseR, 32, col);

                ImVec2 apexSp;
                if (scope.project(pos, apexSp)) {
                    for (int k = 0; k < 4; ++k) {
                        const float t = k * glm::half_pi<float>();
                        const glm::vec3 p = baseC
                            + (tangent * std::cos(t) + bitangent * std::sin(t)) * baseR;
                        ImVec2 sp;
                        if (scope.project(p, sp))
                            dl->AddLine(apexSp, sp, col, EditorStyle::px(1.0f));
                    }
                }
                break;
            }
            case LightType::Rect:
            case LightType::Disk: {
                // axisU: local +X * width/2 (Rect) or * radius (Disk); axisV likewise on +Y,
                // matching render_view's GPU packing.
                const bool isRect = (light.type == LightType::Rect);
                const float ux = isRect ? light.areaWidth  * 0.5f : light.areaRadius;
                const float uy = isRect ? light.areaHeight * 0.5f : light.areaRadius;
                const glm::vec3 right = Math::computeRight(rot) * ux;
                const glm::vec3 up    = Math::computeUp(rot) * uy;

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
                        ok[i] = scope.project(corners[i], sp[i]);
                    }
                    for (int i = 0; i < 4; ++i) {
                        const int j = (i + 1) & 3;
                        if (ok[i] && ok[j]) dl->AddLine(sp[i], sp[j], col, EditorStyle::px(1.5f));
                    }
                } else {
                    // Disk: right/up already carry the radius, so unit radius here.
                    scope.circle(pos, right, up, 1.0f, 32, col, EditorStyle::px(1.5f));
                }

                // Emission arrow toward the lit hemisphere (+dir).
                scope.arrow(
                    pos,
                    pos + dir * 0.5f,
                    col,
                    EditorStyle::px(1.5f),
                    EditorStyle::px(8.5f),
                    EditorStyle::px(4.0f)
                );
                if (light.twoSided)
                    scope.arrow(
                        pos,
                        pos - dir * 0.5f,
                        col,
                        EditorStyle::px(1.5f),
                        EditorStyle::px(8.5f),
                        EditorStyle::px(4.0f)
                    );

                // Beyond this the light contributes nothing.
                const float rr = std::max(0.05f, light.radius);
                const ImU32 fade = selected
                    ? IM_COL32(255, 200, 80, 90)
                    : IM_COL32(
                        static_cast<int>(light.color.r * 200),
                        static_cast<int>(light.color.g * 200),
                        static_cast<int>(light.color.b * 200),
                        80
                    );
                scope.sphere(pos, rr, 24, fade);
                break;
            }
            case LightType::Count: break;
        }
    });
}

void GizmoOverlay::drawProbeGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    const auto drawProbe = [&](EntityId id, const ReflectionProbe& probe, const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IM_COL32(77, 158, 235, 200);

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        const glm::vec3 e   = probe.halfExtents;

        scope.box(pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), e, col, EditorStyle::px(selected ? 2.0f : 1.5f));

        // The capture point.
        ImVec2 sp;
        if (scope.project(pos, sp))
            dl->AddCircleFilled(sp, EditorStyle::px(selected ? 4.0f : 3.0f), col);
    };
    ec.frame.scene.forEach<ReflectionProbe, Transform>(drawProbe);

    const auto drawVolume = [&](EntityId id, const IrradianceVolume& volume, const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IRRADIANCE_VOLUME_COL;

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);

        scope.box(
            pos,
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            volume.halfExtents,
            col,
            EditorStyle::px(selected ? 2.0f : 1.5f)
        );

        // The grid only for the selected volume.
        if (!selected) return;

        // Clamped (IrradianceVolume::clampedResolution): raw, 2048 x 2048 x 1024 is 2^32,
        // which wraps this uint32 product to zero and passes the guard.
        const glm::uvec3 res = IrradianceVolume::clampedResolution(volume);
        if (res.x * res.y * res.z > MAX_DRAWN_PROBES) return;

        const glm::vec3 boxMin  = pos - volume.halfExtents;
        const glm::vec3 boxSize = volume.halfExtents * 2.0f;
        const glm::vec3 resf(res);

        for (uint32_t z = 0; z < res.z; ++z) {
            for (uint32_t y = 0; y < res.y; ++y) {
                for (uint32_t x = 0; x < res.x; ++x) {
                    // Texel centres: where the baker captures.
                    const glm::vec3 t = (glm::vec3(x, y, z) + 0.5f) / resf;
                    ImVec2 pp;
                    if (scope.project(boxMin + boxSize * t, pp))
                        dl->AddCircleFilled(pp, EditorStyle::px(2.0f), col);
                }
            }
        }
    };
    ec.frame.scene.forEach<IrradianceVolume, Transform>(drawVolume);
}

void GizmoOverlay::drawEffectGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    ec.frame.scene.forEach<Decal, Transform>([&](EntityId id, const Decal&, const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : DECAL_COL;

        // The Transform's scale is the projection box (a unit cube), in world pose as the
        // decal pass projects.
        const Scene&    scene = ec.frame.scene;
        const glm::vec3 pos   = resolvedWorldPosition(scene, id, tf);
        const glm::quat rot   = resolvedWorldRotation(scene, id, tf);
        const glm::vec3 scale = resolvedWorldScale(scene, id, tf);
        scope.box(pos, rot, scale * 0.5f, col, EditorStyle::px(selected ? 2.0f : 1.5f));

        // Decals project along the entity's forward.
        const glm::vec3 fwd = Math::computeForward(rot);
        ImVec2 a, b;
        if (scope.project(pos, a) && scope.project(pos + fwd * (scale.z * 0.75f), b))
            dl->AddLine(a, b, col, EditorStyle::px(selected ? 2.0f : 1.5f));
    });

    const auto drawEmitter = [&](EntityId id, const ParticleEmitter& e, const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : EMITTER_COL;

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        ImVec2 sp;
        if (!scope.project(pos, sp)) return;
        dl->AddCircle(
            sp,
            EditorStyle::px(selected ? 6.0f : 5.0f),
            col,
            0,
            EditorStyle::px(selected ? 2.0f : 1.5f)
        );
        dl->AddCircleFilled(sp, EditorStyle::px(2.0f), col);

        const float speed = glm::length(e.velocity);
        if (speed > 1e-4f) {
            ImVec2 tip;
            if (scope.project(pos + (e.velocity / speed) * 0.75f, tip))
                dl->AddLine(sp, tip, col, EditorStyle::px(selected ? 2.0f : 1.5f));
        }
    };
    ec.frame.scene.forEach<ParticleEmitter, Transform>(drawEmitter);
}

void GizmoOverlay::drawAudioGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    Scene&                 scene     = ec.frame.scene;
    const ResourceManager& resources = ec.frame.resources;

    // Joined on Transform, unlike AudioSystem's reconcile: a poseless source plays at the
    // origin, a place the author never picked.
    scene.forEach<AudioSource, Transform>([&](EntityId id, const AudioSource& source, const Transform& tf) {
        const bool selected = ec.state.isSelected(id);
        const bool armed = source.clip && resources.isAlive(source.clip);
        const ImU32 col  = selected ? EditorStyle::HIGHLIGHT_U32 : armed ? AUDIO_COL : AUDIO_COL_DIM;

        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);

        ImVec2 sp;
        if (scope.project(pos, sp)) {
            markEntity(dl, source.spatial ? EditorIcon::Audio : EditorIcon::Audio2D, id, sp, pos, col);
            if (source.playing)
                dl->AddCircle(
                    sp,
                    entityMarkerHitRadius() + EditorStyle::px(2.0f),
                    col,
                    0,
                    EditorStyle::px(1.5f)
                );
        }

        if (!selected || !source.spatial) return;

        // Not clamped: maxDistance <= minDistance disables attenuation, which the inverted
        // spheres show.
        scope.sphere(pos, std::max(0.05f, source.minDistance), 24, AUDIO_COL);
        scope.sphere(pos, std::max(0.05f, source.maxDistance), 24, AUDIO_COL_DIM);
    });

    const EntityId ear = findActiveListener(scene);
    const EntityId eye = ec.frame.visibility->cameraEntity;

    scene.forEach<AudioListener, Transform>([&](EntityId id, const AudioListener&, const Transform& tf) {
        // The camera the frame renders through is the viewer, as in drawCameraGizmos.
        if (id == eye) return;

        const bool  selected = ec.state.isSelected(id);
        // Full only for findActiveListener's pick; the others are inert.
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : (id == ear) ? AUDIO_COL : AUDIO_COL_DIM;

        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        const glm::quat rot = resolvedWorldRotation(scene, id, tf);

        ImVec2 sp;
        if (scope.project(pos, sp))
            markEntity(dl, EditorIcon::Listener, id, sp, pos, col);

        // Facing decides which speaker a source lands in; forward is -Z.
        scope.arrow(
            pos,
            pos + Math::computeForward(rot) * 0.8f,
            col,
            EditorStyle::px(1.5f),
            EditorStyle::px(8.5f),
            EditorStyle::px(4.0f)
        );
    });
}

void GizmoOverlay::drawCameraGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    const EntityId eye = ec.frame.visibility->cameraEntity;

    ec.frame.scene.forEach<Camera, Transform>([&](EntityId id, const Camera& cam, const Transform& tf) {
        // The camera the frame renders through is the viewer.
        if (id == eye) return;

        const bool selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IM_COL32(120, 200, 220, 220);
        // Dimmer infill edges, so the apex, far rectangle and up-tab read as the silhouette.
        const ImU32 colDim = selected
            ? IM_COL32(255, 200, 80, 130)
            : IM_COL32(120, 200, 220, 140);

        // Clip-space corners through the inverted view-projection. The viewport's aspect
        // stands in while the camera derives its own.
        Transform pose = tf;
        pose.position = resolvedWorldPosition(ec.frame.scene, id, tf);
        pose.rotation = resolvedWorldRotation(ec.frame.scene, id, tf);
        const glm::vec3& pos = pose.position;

        const float viewportAspect = ec.viewportSize.y > 1.0f
            ? ec.viewportSize.x / ec.viewportSize.y : 16.0f / 9.0f;
        const glm::mat4 toWorld = glm::inverse(
            Camera::computeProjection(cam, viewportAspect) * Transform::computeView(pose)
        );
        const auto corner = [&](float x, float y, float z) {
            const glm::vec4 world = toWorld * glm::vec4(x, y, z, 1.0f);
            return glm::vec3(world) / world.w;
        };
        // Top-right, top-left, bottom-left, bottom-right.
        const glm::vec3 nearCorners[4] = {
            corner( 1.0f,  1.0f, -1.0f),
            corner(-1.0f,  1.0f, -1.0f),
            corner(-1.0f, -1.0f, -1.0f),
            corner( 1.0f, -1.0f, -1.0f),
        };
        const glm::vec3 farCorners[4] = {
            corner( 1.0f,  1.0f, 1.0f),
            corner(-1.0f,  1.0f, 1.0f),
            corner(-1.0f, -1.0f, 1.0f),
            corner( 1.0f, -1.0f, 1.0f),
        };

        ImVec2 apexSp;
        bool haveApex = scope.project(pos, apexSp);

        ImVec2 nearSp[4]{};
        bool haveNear[4] = {};
        ImVec2 farSp[4]{};
        bool haveFar[4] = {};
        for (int i = 0; i < 4; ++i) {
            haveNear[i] = scope.project(nearCorners[i], nearSp[i]);
            haveFar[i]  = scope.project(farCorners[i], farSp[i]);
        }

        if (cam.projection == ProjectionType::Perspective && haveApex) {
            for (int i = 0; i < 4; ++i) {
                if (haveNear[i]) dl->AddLine(apexSp, nearSp[i], colDim, EditorStyle::px(1.0f));
            }
        }

        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            if (haveNear[i] && haveNear[j])
                dl->AddLine(nearSp[i], nearSp[j], colDim, EditorStyle::px(1.0f));
        }

        for (int i = 0; i < 4; ++i) {
            if (haveNear[i] && haveFar[i])
                dl->AddLine(nearSp[i], farSp[i], col, EditorStyle::px(1.0f));
        }

        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            if (haveFar[i] && haveFar[j])
                dl->AddLine(farSp[i], farSp[j], col, EditorStyle::px(1.0f));
        }

        // An up-tab on the far face shows the camera's roll.
        if (haveFar[0] && haveFar[1]) {
            const ImVec2 mid((farSp[0].x + farSp[1].x) * 0.5f, (farSp[0].y + farSp[1].y) * 0.5f);
            const float tabHeight = EditorStyle::px(8.0f);
            const float tabHalf   = EditorStyle::px(5.0f);
            dl->AddTriangleFilled(
                ImVec2(mid.x, mid.y - tabHeight),
                ImVec2(mid.x - tabHalf, mid.y),
                ImVec2(mid.x + tabHalf, mid.y),
                col
            );
        }

        if (haveApex) markEntity(dl, EditorIcon::Camera, id, apexSp, pos, col);
    });
}

void GizmoOverlay::drawColliderGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    // As physics evaluates it: world position and rotation, no scale.
    ec.frame.scene.forEach<Collider, Transform>([&](EntityId id, const Collider& col, const Transform& tf) {
        if (!col.enabled) return;   // inert colliders don't collide, so don't draw them
        const bool   selected = (ec.state.isSelected(id));
        const ImU32  color    = selected ? EditorStyle::HIGHLIGHT_U32 : COLLIDER_COL;
        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        const glm::quat rot = resolvedWorldRotation(ec.frame.scene, id, tf);
        const glm::mat3 r   = glm::mat3_cast(rot);
        for (const ColliderPart& part : col.parts) {
            const glm::vec3 center = pos + r * part.center;
            switch (part.shape) {
                case ColliderShape::Capsule:
                    scope.capsule(
                        center,
                        rot,
                        part.radius,
                        part.halfHeight,
                        COLLIDER_CAPSULE_SEGMENTS,
                        color
                    );
                    break;

                case ColliderShape::Mesh:
                    drawMeshColliderWires(scope, col, center, r, color);
                    break;

                case ColliderShape::Box:
                    scope.box(center, rot, part.halfExtents, color);
                    break;

                case ColliderShape::Count:
                    break;
            }
        }
    });
}

void GizmoOverlay::drawJointGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    Scene& scene = ec.frame.scene;
    scene.forEach<Joint, Transform>([&](EntityId id, const Joint& joint, const Transform& tf) {
        const bool selected = ec.state.isSelected(id)
            || (joint.connected && ec.state.isSelected(joint.connected));
        const ImU32 color = selected ? EditorStyle::HIGHLIGHT_U32 : JOINT_COL;

        // As the solver reads them: each in its body's frame, position and rotation only.
        const glm::vec3 anchorA = resolvedWorldPosition(scene, id, tf)
            + resolvedWorldRotation(scene, id, tf) * joint.anchor;

        ImVec2 spA;
        const bool onA = scope.project(anchorA, spA);

        // With no connected entity it holds to a world point: no rope to draw.
        const Transform* ct = scene.tryGet<Transform>(joint.connected);
        if (!ct) {
            if (onA) markEntity(dl, EditorIcon::Joint, id, spA, anchorA, color);
            return;
        }

        const glm::vec3 anchorB = resolvedWorldPosition(scene, joint.connected, *ct)
            + resolvedWorldRotation(scene, joint.connected, *ct) * joint.connectedAnchor;

        ImVec2 spB;
        const bool onB = scope.project(anchorB, spB);
        if (onA && onB) {
            dl->AddLine(spA, spB, color, EditorStyle::px(1.5f));
            dl->AddCircleFilled(spA, EditorStyle::px(3.0f), color);
            dl->AddCircleFilled(spB, EditorStyle::px(3.0f), color);
        }

        const glm::vec3 midpoint = (anchorA + anchorB) * 0.5f;
        ImVec2 mid;
        if (scope.project(midpoint, mid)) {
            markEntity(dl, EditorIcon::Joint, id, mid, midpoint, color);
        }
    });
}

void GizmoOverlay::drawSkeletonGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    const PoseBuffer* poses = ec.frame.poses;
    if (!poses) return;

    const ResourceManager& resources = ec.frame.resources;
    const std::vector<glm::mat4>& global = poses->global();

    // Reused across rigs: one allocation per frame, not per character.
    std::vector<glm::mat4> world;
    std::vector<ImVec2>    screen;
    std::vector<uint8_t>   onScreen;

    const auto drawRig = [&](EntityId id, const Animator& animator, const Transform& tf) {
        const PoseSlice* slice = poses->sliceOf(id);
        if (!slice || slice->count == 0) return;
        if (!animator.skeleton || !resources.isAlive(animator.skeleton)) return;
        const SkeletonAsset& skeleton = resources.get(animator.skeleton);
        if (skeleton.bones.size() != slice->count) return;

        // The pose is in rig model space; the rig's world matrix (as for skinned vertices) places it.
        const glm::mat4 model = resolvedWorldMatrix(ec.frame.scene, id, tf);
        const bool  selected = ec.state.isSelected(id);
        const ImU32 col      = selected ? EditorStyle::HIGHLIGHT_U32 : SKELETON_COL;

        world.resize(slice->count);
        screen.resize(slice->count);
        onScreen.resize(slice->count);

        glm::vec3 boneMin(std::numeric_limits<float>::max());
        glm::vec3 boneMax(std::numeric_limits<float>::lowest());
        for (uint32_t b = 0; b < slice->count; ++b) {
            world[b] = model * global[slice->first + b];
            const glm::vec3 origin(world[b][3]);
            boneMin = glm::min(boneMin, origin);
            boneMax = glm::max(boneMax, origin);
            onScreen[b] = scope.project(origin, screen[b]) ? 1 : 0;
        }

        for (uint32_t b = 0; b < slice->count; ++b) {
            if (!onScreen[b]) continue;
            const int32_t parent = skeleton.bones[b].parent;
            if (parent >= 0 && onScreen[parent]) {
                dl->AddLine(screen[parent], screen[b], col, EditorStyle::px(1.5f));
            }
            // A dot too: leaf and root bones have no segment of their own.
            dl->AddCircleFilled(screen[b], EditorStyle::px(2.5f), col, 6);
        }

        // Axes show facing, which composition or bind-inverse mistakes corrupt. Selected rig
        // only, or the triads bury the viewport.
        if (!selected) return;
        const glm::vec3 extent = boneMax - boneMin;
        const float axisLength = std::max(0.05f * std::max({extent.x, extent.y, extent.z}), 1e-3f);
        for (uint32_t b = 0; b < slice->count; ++b) {
            const glm::vec3 origin(world[b][3]);
            for (int axis = 0; axis < 3; ++axis) {
                const glm::vec3 dir = glm::vec3(world[b][axis]);
                const float len = glm::length(dir);
                if (len <= glm::epsilon<float>()) continue;
                scope.segment(
                    origin,
                    origin + dir * (axisLength / len),
                    AXIS_COLS[axis],
                    EditorStyle::px(1.5f)
                );
            }
        }
    };
    ec.frame.scene.forEach<Animator, Transform>(drawRig);
}

void GizmoOverlay::drawBoundsGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const RenderObjects& objects = ec.frame.visibility->objects;
    for (const uint32_t object : objects.visible) {
        const Math::AABB& world = objects.bounds[object];
        if (world.min == world.max) continue;
        const glm::vec3 center = (world.min + world.max) * 0.5f;
        const glm::vec3 he     = (world.max - world.min) * 0.5f;
        scope.box(center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), he, BOUNDS_COL);
    }
}

void GizmoOverlay::drawSelectionOutline(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    if (ec.state.selection.empty()) return;

    const ImU32 secondary = IM_COL32(255, 210, 50, 130);
    const Visibility& visibility = *ec.frame.visibility;
    for (const uint32_t object : visibility.objects.visible) {
        const Math::AABB& world = visibility.objects.bounds[object];
        const EntityId    id    = visibility.entities[object];
        if (world.min == world.max || !ec.state.isSelected(id)) continue;
        const glm::vec3 center = (world.min + world.max) * 0.5f;
        const glm::vec3 he     = (world.max - world.min) * 0.5f;
        const ImU32 col = (id == ec.state.selectedEntity)
            ? EditorStyle::HIGHLIGHT_U32 : secondary;
        scope.box(center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), he, col);
    }
}

} // namespace Vkm::Engine
