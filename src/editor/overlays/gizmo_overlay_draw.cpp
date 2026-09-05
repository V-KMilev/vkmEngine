#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "framework/editor_common.h"
#include "overlays/wire_draw.h"
#include "ui/editor_style.h"
#include "system/visibility/visibility.h"
#include "system/animation/pose_buffer.h"
#include "system/camera/camera_controller_system.h"
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

// Ring resolution for a capsule collider. Coarse enough to stay one wireframe
// among many at a glance, fine enough that the radius reads as a radius.
constexpr int COLLIDER_CAPSULE_SEGMENTS = 24;
constexpr ImU32 BOUNDS_COL   = IM_COL32(230, 200, 60, 160);  // mesh-bounds amber

constexpr ImU32 IRRADIANCE_VOLUME_COL = IM_COL32(235, 150, 77, 200);  // GI orange, against the probe's blue

constexpr ImU32 SKELETON_COL = IM_COL32(120, 190, 255, 230);  // rig blue
constexpr ImU32 JOINT_COL    = IM_COL32(255, 170, 60, 220);   // joint amber, against physics green

// A bone's own X / Y / Z, in the editor's axis colours - so a bone triad reads
// against the navigation gizmo and the transform handles without a legend.
constexpr ImU32 AXIS_COLS[3] = {
    EditorStyle::AXIS_X_U32,
    EditorStyle::AXIS_Y_U32,
    EditorStyle::AXIS_Z_U32,
};

constexpr ImU32 DECAL_COL   = IM_COL32(200, 120, 220, 200);  // decal violet
constexpr ImU32 EMITTER_COL = IM_COL32(240, 200, 90, 200);   // particle amber

// Accent::Audio, the magenta the Inspector's two audio cards already wear, so
// the same subject reads the same on the card and in the viewport - pushed a
// little off the card's (224, 97, 204), most of it out of the green, to hold
// it clear of DECAL_COL's violet, which is its nearest neighbour in this file.
constexpr ImU32 AUDIO_COL = IM_COL32(232, 62, 208, 220);  // audio magenta
// Same hue, faded: the outer falloff sphere, and anything present but not
// contributing - a source with no clip, a listener that is not the ear.
constexpr ImU32 AUDIO_COL_DIM = IM_COL32(232, 62, 208, 80);

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

    // The wire primitives, with the four values this scope already holds bound
    // in. Passed through instead - `wireSphere(dl, vp, pos, r, 32, vpMin,
    // vpSize, col, 1.0f)` - they are four arguments of ceremony around two of
    // meaning, at thirty call sites, each of which has to unpack them first.
    void segment(const glm::vec3& a, const glm::vec3& b, ImU32 col,
                 float thickness = 1.0f) const {
        wireSegment(dl, vp, a, b, vpMin, vpSize, col, thickness);
    }

    void arc(const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB,
             float radius, float from, float to, int segments, ImU32 col,
             float thickness = 1.0f) const {
        wireArc(dl, vp, center, axisA, axisB, radius, from, to, segments,
                vpMin, vpSize, col, thickness);
    }

    void circle(const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB,
                float radius, int segments, ImU32 col, float thickness = 1.0f) const {
        wireCircle(dl, vp, center, axisA, axisB, radius, segments, vpMin, vpSize,
                   col, thickness);
    }

    void sphere(const glm::vec3& center, float radius, int segments, ImU32 col,
                float thickness = 1.0f) const {
        wireSphere(dl, vp, center, radius, segments, vpMin, vpSize, col, thickness);
    }

    void arrow(const glm::vec3& from, const glm::vec3& to, ImU32 col,
               float thickness, float headLen, float headWidth) const {
        arrowLine(dl, vp, from, to, vpMin, vpSize, col, thickness, headLen, headWidth);
    }

    void box(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& halfExtents,
             ImU32 col, float thickness = EditorStyle::px(1.5f)) const {
        wireBox(dl, vp, pos, rot, halfExtents, vpMin, vpSize, col, thickness);
    }

    void capsule(const glm::vec3& center, const glm::quat& rot, float radius,
                 float halfHeight, int segments, ImU32 col,
                 float thickness = EditorStyle::px(1.5f)) const {
        wireCapsule(dl, vp, center, rot, radius, halfHeight, segments, vpMin, vpSize,
                    col, thickness);
    }

    /// Where a world point lands on screen, or false when it is behind the eye.
    bool project(const glm::vec3& p, ImVec2& out) const {
        return projectToViewport(vp, p, vpMin, vpSize, out);
    }

    glm::mat4   vp{1.0f};
    ImVec2      vpMin{0, 0};
    ImVec2      vpSize{0, 0};
    ImDrawList* dl = nullptr;
};


// A mesh collider drawn as its own triangles. Exact rather than approximate,
// because the whole reason to reach for this shape is that no box describes the
// geometry - a bounding wireframe would show the thing it is not.
void drawMeshColliderWires(const ViewportOverlayScope& scope,
                           const Collider& col, const ColliderPart& part,
                           const glm::vec3& center, const glm::mat3& r, ImU32 color) {
    const uint32_t last = part.meshFirst + part.meshCount;
    if (last > col.meshPoints.size()) return;

    // A level's collision mesh is tens of thousands of triangles, so past this it
    // is stepped through: the shape stays legible and the cost stays flat. Low,
    // because each triangle is three lines and each line four draw-list vertices.
    constexpr uint32_t MAX_DRAWN = 400;
    const uint32_t triangles = part.meshCount / 3;
    const uint32_t step = triangles > MAX_DRAWN ? triangles / MAX_DRAWN : 1;

    for (uint32_t t = 0; t < triangles; t += step) {
        const uint32_t base = part.meshFirst + t * 3;
        const glm::vec3 a = center + r * col.meshPoints[base + 0];
        const glm::vec3 b = center + r * col.meshPoints[base + 1];
        const glm::vec3 c = center + r * col.meshPoints[base + 2];
        scope.segment(a, b, color, 1.0f);
        scope.segment(b, c, color, 1.0f);
        scope.segment(c, a, color, 1.0f);
    }
}
} // namespace

void GizmoOverlay::drawLightGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

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

        // Billboard marker at the entity origin so a light is always findable
        // even if the wireframe is tiny or pointed away. Drawn first so the
        // wireframe overlays it.
        {
            ImVec2 sp;
            if (scope.project(pos, sp)) {
                const EditorIcon glyph =
                    light.type == LightType::Directional ? EditorIcon::LightDir :
                    light.type == LightType::Point       ? EditorIcon::LightPoint :
                                                           EditorIcon::LightSpot;
                drawEntityMarker(dl, glyph, sp, col);
            }
        }

        switch (light.type) {
            case LightType::Directional: {
                // A small disc at the light origin plus three parallel rays
                // forward; the triangular offset reads as parallel rays rather
                // than as the single arrow a spotlight wears.
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
                scope.circle(pos, right, udir, discR, 16, col,
                           EditorStyle::px(1.5f));

                for (const glm::vec3& off : offsets) {
                    const glm::vec3 start = pos + off;
                    scope.arrow(start, start + dir * L, col,
                              EditorStyle::px(2.0f), EditorStyle::px(12.0f),
                              EditorStyle::px(6.0f));
                }
                break;
            }
            case LightType::Point: {
                const float r = std::max(0.05f, light.radius);
                scope.sphere(pos, r, 32, col, 1.0f);
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
                scope.circle(baseC, tangent, bitangent, baseR, 32, col, 1.0f);

                ImVec2 apexSp;
                if (scope.project(pos, apexSp)) {
                    for (int k = 0; k < 4; ++k) {
                        const float t = k * glm::half_pi<float>();
                        const glm::vec3 p = baseC
                            + (tangent * std::cos(t) + bitangent * std::sin(t)) * baseR;
                        ImVec2 sp;
                        if (scope.project(p, sp))
                            dl->AddLine(apexSp, sp, col, 1.0f);
                    }
                }
                break;
            }
            case LightType::Rect:
            case LightType::Disk: {
                // axisU is local +X * width/2 for a Rect or * radius for a Disk,
                // axisV the same on +Y - matching render_view's GPU packing, so
                // the gizmo agrees with the shaded result.
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
                        ok[i] = scope.project(corners[i], sp[i]);
                    }
                    for (int i = 0; i < 4; ++i) {
                        const int j = (i + 1) & 3;
                        if (ok[i] && ok[j]) dl->AddLine(sp[i], sp[j], col, EditorStyle::px(1.5f));
                    }
                } else {
                    // Disk: right/up already carry the radius, so unit radius here.
                    scope.circle(pos, right, up, 1.0f, 32, col, 1.5f);
                }

                // Emission arrow toward the lit hemisphere (+dir). Two-sided
                // emitters get a second arrow on the back so the user can see
                // the emission is bidirectional.
                scope.arrow(pos, pos + dir * 0.5f, col, EditorStyle::px(1.5f),
                  EditorStyle::px(8.5f), EditorStyle::px(4.0f));
                if (light.twoSided)
                    scope.arrow(pos, pos - dir * 0.5f, col, EditorStyle::px(1.5f),
                  EditorStyle::px(8.5f), EditorStyle::px(4.0f));

                // The distance beyond which the light contributes nothing, drawn
                // dimmer and thinner than the emitter outline so the silhouette
                // reads as the shape and the sphere as a falloff hint.
                const float rr = std::max(0.05f, light.radius);
                const ImU32 fade = selected
                    ? IM_COL32(255, 200, 80, 90)
                    : IM_COL32(static_cast<int>(light.color.r * 200),
                               static_cast<int>(light.color.g * 200),
                               static_cast<int>(light.color.b * 200), 80);
                scope.sphere(pos, rr, 24, fade, 1.0f);
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

    ec.frame.scene.forEach<ReflectionProbe, Transform>([&](EntityId id, const ReflectionProbe& probe, const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IM_COL32(77, 158, 235, 200);

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        const glm::vec3 e   = probe.halfExtents;

        // The world-axis-aligned influence box (wireBox with no rotation).
        scope.box(pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), e, col,
                EditorStyle::px(selected ? 2.0f : 1.5f));

        // Centre marker: the point the probe captures the scene from.
        ImVec2 sp;
        if (scope.project(pos, sp))
            dl->AddCircleFilled(sp, EditorStyle::px(selected ? 4.0f : 3.0f), col);
    });

    ec.frame.scene.forEach<IrradianceVolume, Transform>([&](EntityId id, const IrradianceVolume& volume,
                                                            const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IRRADIANCE_VOLUME_COL;

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);

        scope.box(pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), volume.halfExtents, col,
                EditorStyle::px(selected ? 2.0f : 1.5f));

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
                    if (scope.project(boxMin + boxSize * t, pp))
                        dl->AddCircleFilled(pp, EditorStyle::px(2.0f), col);
                }
            }
        }
    });
}

void GizmoOverlay::drawEffectGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    ec.frame.scene.forEach<Decal, Transform>([&](EntityId id, const Decal&, const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : DECAL_COL;

        // The Transform's scale IS the projection box (a unit cube), so the
        // box gizmo is the decal's whole authoring model.
        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        scope.box(pos, tf.rotation, tf.scale * 0.5f, col,
                EditorStyle::px(selected ? 2.0f : 1.5f));

        // Projection direction: decals project along the entity's forward.
        const glm::vec3 fwd = Math::computeForward(tf.rotation);
        ImVec2 a, b;
        if (scope.project(pos, a) &&
            scope.project(pos + fwd * (tf.scale.z * 0.75f), b))
            dl->AddLine(a, b, col, EditorStyle::px(selected ? 2.0f : 1.5f));
    });

    ec.frame.scene.forEach<ParticleEmitter, Transform>([&](EntityId id, const ParticleEmitter& e,
                                                           const Transform& tf) {
        const bool  selected = (ec.state.isSelected(id));
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : EMITTER_COL;

        const glm::vec3 pos = resolvedWorldPosition(ec.frame.scene, id, tf);
        ImVec2 sp;
        if (!scope.project(pos, sp)) return;
        dl->AddCircle(sp, EditorStyle::px(selected ? 6.0f : 5.0f), col, 0,
                      EditorStyle::px(selected ? 2.0f : 1.5f));
        dl->AddCircleFilled(sp, EditorStyle::px(2.0f), col);

        // Initial-velocity direction, so the spray's aim reads at a glance.
        const float speed = glm::length(e.velocity);
        if (speed > 1e-4f) {
            ImVec2 tip;
            if (scope.project(pos + (e.velocity / speed) * 0.75f, tip))
                dl->AddLine(sp, tip, col, EditorStyle::px(selected ? 2.0f : 1.5f));
        }
    });
}

void GizmoOverlay::drawAudioGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    Scene&                 scene     = ec.frame.scene;
    const ResourceManager& resources = ec.frame.resources;

    // Joined on Transform, unlike AudioSystem's reconcile: a source with no pose
    // is heard at the world origin, and a marker there would sit on a place the
    // author never picked. The Inspector names that case where it is fixed.
    scene.forEach<AudioSource, Transform>([&](EntityId id, const AudioSource& source,
                                              const Transform& tf) {
        const bool selected = ec.state.isSelected(id);
        // A source with no clip is not a quiet source, it is a broken one, and
        // the two are indistinguishable in a viewport unless one draws dimmer.
        const bool armed = source.clip && resources.isAlive(source.clip);
        const ImU32 col  = selected ? EditorStyle::HIGHLIGHT_U32
                         : armed    ? AUDIO_COL
                                    : AUDIO_COL_DIM;

        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);

        ImVec2 sp;
        if (scope.project(pos, sp)) {
            // A 2D source is drawn where its Transform is, but the mixer ignores
            // that pose - and the radiating arcs are exactly what the two kinds
            // differ by, so keeping or dropping them is the pair.
            drawEntityMarker(dl, source.spatial ? EditorIcon::Audio : EditorIcon::Audio2D,
                             sp, col);
            // The one thing here that changes while nobody is editing. A one-shot
            // is over within a frame or two, so what the ring reports on in
            // practice is loops and beds.
            if (source.playing)
                dl->AddCircle(sp, entityMarkerHitRadius() + EditorStyle::px(2.0f), col, 0,
                              EditorStyle::px(1.5f));
        }

        // Tuning is done to the selected entity, and maxDistance defaults to 50,
        // so drawing every source's would bury the viewport under 100-unit
        // wireframes - the same reason a probe grid stays on the selection.
        if (!selected || !source.spatial) return;

        // Deliberately not clamped against each other: a maxDistance at or under
        // minDistance disables attenuation, and the outer sphere drawn inside the
        // inner one is that fact. Clamping would lie about what the mixer does.
        scope.sphere(pos, std::max(0.05f, source.minDistance), 24, AUDIO_COL, 1.0f);
        scope.sphere(pos, std::max(0.05f, source.maxDistance), 24, AUDIO_COL_DIM, 1.0f);
    });

    const EntityId ear      = findActiveListener(scene);
    const EntityId flownCam = ec.cameraController.getCameraEntity();

    scene.forEach<AudioListener, Transform>([&](EntityId id, const AudioListener&,
                                                const Transform& tf) {
        // The flown editor camera *is* the viewer - the same reason
        // drawCameraGizmos skips it. A marker there sits inside the user's eye.
        if (id == flownCam) return;

        const bool  selected = ec.state.isSelected(id);
        // Full only for the one findActiveListener picked. Every other
        // listener is inert until that one goes, which is what the card says
        // in words and this says without being opened.
        const ImU32 col = selected    ? EditorStyle::HIGHLIGHT_U32
                        : (id == ear) ? AUDIO_COL
                                      : AUDIO_COL_DIM;

        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        const glm::quat rot = resolvedWorldRotation(scene, id, tf);

        ImVec2 sp;
        if (scope.project(pos, sp))
            drawEntityMarker(dl, EditorIcon::Listener, sp, col);

        // Which way the ear faces decides which speaker a source lands in, and
        // forward here is +Z. That is the engine's one convention whose wrong
        // answer looks plausible instead of failing, so the arrow is the check.
        scope.arrow(pos, pos + Math::computeForward(rot) * 0.8f, col, EditorStyle::px(1.5f),
                  EditorStyle::px(8.5f), EditorStyle::px(4.0f));
    });
}

void GizmoOverlay::drawCameraGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

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

        // The camera's actual near and far, so the gizmo shows what it really
        // clips; the minimums are clamped only to keep a degenerate value from
        // producing a zero-extent rectangle. A large zFar draws off viewport.
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
        bool haveApex = scope.project(pos, apexSp);

        ImVec2 nearSp[4]{};
        bool haveNear[4] = {};
        ImVec2 farSp[4]{};
        bool haveFar[4] = {};
        for (int i = 0; i < 4; ++i) {
            haveNear[i] = scope.project(nearCorners[i], nearSp[i]);
            haveFar[i]  = scope.project(farCorners[i], farSp[i]);
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
            const ImVec2 tab(mid.x, mid.y - EditorStyle::px(8.0f));
            dl->AddTriangleFilled(tab,
                ImVec2(mid.x - 5.0f, mid.y),
                ImVec2(mid.x + 5.0f, mid.y), col);
        }

        if (haveApex) drawEntityMarker(dl, EditorIcon::Camera, apexSp, col);
    });
}

void GizmoOverlay::drawColliderGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    // Physics evaluates a collider in the entity's world frame - position and
    // rotation, no scale - so the wireframe is drawn the same way and is exactly
    // what the solver collides against.
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
                    scope.capsule(center, rot, part.radius, part.halfHeight,
                                COLLIDER_CAPSULE_SEGMENTS, color);
                    break;

                case ColliderShape::Mesh:
                    drawMeshColliderWires(scope, col, part, center, r, color);
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

        // The anchors as the solver reads them: each in its own body's frame,
        // position and rotation only.
        const glm::vec3 anchorA = resolvedWorldPosition(scene, id, tf)
            + resolvedWorldRotation(scene, id, tf) * joint.anchor;

        ImVec2 spA;
        const bool onA = scope.project(anchorA, spA);

        // A joint whose connected entity is gone or empty holds to a world
        // point; there is nothing to draw a rope to, so the anchor stands alone.
        const bool tethered = joint.connected
                           && scene.isAlive(joint.connected)
                           && scene.has<Transform>(joint.connected);
        if (!tethered) {
            if (onA) drawEntityMarker(dl, EditorIcon::Joint, spA, color);
            return;
        }

        const Transform& ct = scene.get<Transform>(joint.connected);
        const glm::vec3 anchorB =
            resolvedWorldPosition(scene, joint.connected, ct)
            + resolvedWorldRotation(scene, joint.connected, ct) * joint.connectedAnchor;

        ImVec2 spB;
        const bool onB = scope.project(anchorB, spB);
        if (onA && onB) {
            dl->AddLine(spA, spB, color, EditorStyle::px(1.5f));
            dl->AddCircleFilled(spA, EditorStyle::px(3.0f), color);
            dl->AddCircleFilled(spB, EditorStyle::px(3.0f), color);
        }

        // The glyph sits at the midpoint, where it reads as the relationship
        // rather than as either body.
        ImVec2 mid;
        if (scope.project((anchorA + anchorB) * 0.5f, mid)) {
            drawEntityMarker(dl, EditorIcon::Joint, mid, color);
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

    // Reused across rigs, so the overlay allocates once per frame rather than
    // once per character.
    std::vector<glm::mat4> world;
    std::vector<ImVec2>    screen;
    std::vector<uint8_t>   onScreen;

    ec.frame.scene.forEach<Animator, Transform>(
            [&](EntityId id, const Animator& animator, const Transform& tf) {
        const PoseSlice* slice = poses->sliceOf(id.slot());
        if (!slice || slice->count == 0) return;
        if (!animator.skeleton || !resources.isAlive(animator.skeleton)) return;
        const SkeletonAsset& skeleton = resources.get(animator.skeleton);
        if (skeleton.bones.size() != slice->count) return;

        // The pose is in the rig's model space, so the rig entity's world
        // matrix is what puts it in the world - the same matrix that will
        // multiply the skinned vertices.
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
            if (parent >= 0 && onScreen[parent]) dl->AddLine(screen[parent], screen[b], col, 1.5f);
            // A joint dot as well as the segments, because a leaf bone and a
            // root have no segment of their own to be seen by.
            dl->AddCircleFilled(screen[b], 2.5f, col, 6);
        }

        // Segments show where the joints are; only axes show which way they face,
        // which is what a composition or bind-inverse mistake corrupts. On the
        // selected rig alone - a hundred triads would bury the viewport.
        if (!selected) return;
        const glm::vec3 extent = boneMax - boneMin;
        const float axisLength = std::max(0.05f * std::max({extent.x, extent.y, extent.z}), 1e-3f);
        for (uint32_t b = 0; b < slice->count; ++b) {
            const glm::vec3 origin(world[b][3]);
            for (int axis = 0; axis < 3; ++axis) {
                const glm::vec3 dir = glm::vec3(world[b][axis]);
                const float len = glm::length(dir);
                if (len <= glm::epsilon<float>()) continue;
                scope.segment(origin, origin + dir * (axisLength / len), AXIS_COLS[axis], 1.5f);
            }
        }
    });
}

void GizmoOverlay::drawBoundsGizmos(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    // World-space AABB of every visible entity, already computed by the
    // visibility pass (an axis-aligned box is wireBox with no rotation).
    for (const VisibleEntity& e : ec.frame.visibility->entries) {
        if (e.world.min == e.world.max) continue;
        const glm::vec3 center = (e.world.min + e.world.max) * 0.5f;
        const glm::vec3 he     = (e.world.max - e.world.min) * 0.5f;
        scope.box(center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), he, BOUNDS_COL);
    }
}

void GizmoOverlay::drawSelectionOutline(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    ImDrawList* dl = scope.dl;

    if (ec.state.selection.empty()) return;

    // Outline every selected entity's world AABB; the active one gets the
    // full highlight, the rest a dimmer tint. Only mesh entities are in the
    // visible set; lights / probes / cameras highlight their own gizmos.
    const ImU32 secondary = IM_COL32(255, 210, 50, 130);
    for (const VisibleEntity& e : ec.frame.visibility->entries) {
        if (e.world.min == e.world.max || !ec.state.isSelected(e.id)) continue;
        const glm::vec3 center = (e.world.min + e.world.max) * 0.5f;
        const glm::vec3 he     = (e.world.max - e.world.min) * 0.5f;
        const ImU32 col = (e.id == ec.state.selectedEntity)
            ? EditorStyle::HIGHLIGHT_U32 : secondary;
        scope.box(center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), he, col);
    }
}

} // namespace Vkm::Engine
