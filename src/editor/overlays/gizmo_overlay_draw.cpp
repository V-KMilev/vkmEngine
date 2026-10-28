#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>

#include "core/system.h"
#include "debug/debug_draw.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "ui/editor_icons.h"
#include "ui/entity_labels.h"
#include "overlays/viewport_projection.h"
#include "ui/editor_style.h"
#include "system/visibility/visibility.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/planar_reflector.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/ui/ui_element.h"
#include "core/host_chrome.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

// RAII viewport marker pass: caches view-projection and drawlist, pushes/pops the clip rect.
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

    /// A world point's screen position, or false when behind the eye.
    bool project(const glm::vec3& p, ImVec2& out) const {
        return projectToViewport(vp, p, vpMin, vpSize, out);
    }

    glm::mat4   vp{1.0f};
    ImVec2      vpMin{0, 0};
    ImVec2      vpSize{0, 0};
    ImDrawList* dl = nullptr;
};

/// @p color, or the highlight for a selected entity: as DebugViewSystem draws its shape.
ImU32 markerColor(const EditorContext& ec, EntityId id, DebugPalette::Color color) {
    return EditorStyle::paletteU32(ec.state.isSelected(id) ? DebugPalette::HIGHLIGHT : color);
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

void GizmoOverlay::showWorldGizmos(EditorContext& ec, bool shown) const {
    DebugDraw* debug = ec.frame.debug;
    if (!debug) return;

    const GizmoVisibility& show = ec.state.gizmos;
    // Gameplay's own lines and the editor's tools draw in General, shown whether the editor is or not.
    debug->setShown(DebugCategory::General, true);
    debug->setShown(DebugCategory::Lights, shown && show.lights);
    debug->setShown(DebugCategory::Cameras, shown && show.cameras);
    debug->setShown(DebugCategory::Probes, shown && show.probes);
    debug->setShown(DebugCategory::Effects, shown && show.effects);
    debug->setShown(DebugCategory::Audio, shown && show.audio);
    debug->setShown(DebugCategory::Acoustics, shown && show.acoustics);
    debug->setShown(DebugCategory::Colliders, shown && show.colliders);
    debug->setShown(DebugCategory::Contacts, shown && show.contacts);
    debug->setShown(DebugCategory::Bounds, shown && show.bounds);
    debug->setShown(DebugCategory::Skeletons, shown && show.skeletons);
    debug->setShown(DebugCategory::Selection, shown);

    DebugFocus focus;
    if (shown) {
        focus.entities = ec.state.selection;
        focus.primary  = ec.state.selectedEntity;
    }
    focus.detailForFocusOnly = true;
    debug->setFocus(std::move(focus));
    // Widths in the editor's design pixels, which scale with its font.
    debug->setPointScale(ImGui::GetFontSize() / EditorStyle::REFERENCE_FONT_SIZE);
}

void GizmoOverlay::markLights(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene& scene = ec.frame.scene;
    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& tf) {
        if (!light.enabled) return;
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (!scope.project(pos, sp)) return;
        // A box scaled with the light's reach answers a click too; a directional's is fixed.
        const bool  sun   = light.type == LightType::Directional;
        const float reach = sun ? 0.5f : std::clamp(light.radius * 0.2f, 0.3f, 3.0f);
        const ImU32 col   = markerColor(ec, id, DebugPalette::ofLight(light.color));
        markEntity(scope.dl, lightIcon(light.type), id, sp, pos, col, reach);
    });
}

void GizmoOverlay::markProbes(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene&              scene = ec.frame.scene;
    const DebugPalette::Color probe = DebugPalette::of(DebugCategory::Probes);
    const auto mark = [&](EntityId id, const Transform& tf, EditorIcon icon, DebugPalette::Color color) {
        // At the capture point.
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (scope.project(pos, sp)) markEntity(scope.dl, icon, id, sp, pos, markerColor(ec, id, color));
    };
    scene.forEach<ReflectionProbe, Transform>([&](EntityId id, const ReflectionProbe&, const Transform& tf) {
        mark(id, tf, EditorIcon::Probe, probe);
    });
    scene.forEach<PlanarReflector, Transform>([&](EntityId id, const PlanarReflector&, const Transform& tf) {
        mark(id, tf, EditorIcon::Probe, probe);
    });
    const auto markVolume = [&](EntityId id, const IrradianceVolume&, const Transform& tf) {
        mark(id, tf, EditorIcon::Volume, DebugPalette::IRRADIANCE_VOLUME);
    };
    scene.forEach<IrradianceVolume, Transform>(markVolume);
}

void GizmoOverlay::markEffects(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene& scene = ec.frame.scene;
    const auto mark = [&](EntityId id, const Transform& tf, EditorIcon icon, DebugPalette::Color color) {
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (scope.project(pos, sp)) markEntity(scope.dl, icon, id, sp, pos, markerColor(ec, id, color));
    };
    scene.forEach<Decal, Transform>([&](EntityId id, const Decal&, const Transform& tf) {
        mark(id, tf, EditorIcon::Decal, DebugPalette::DECAL);
    });
    const DebugPalette::Color emitter = DebugPalette::of(DebugCategory::Effects);
    scene.forEach<ParticleEmitter, Transform>([&](EntityId id, const ParticleEmitter&, const Transform& tf) {
        mark(id, tf, EditorIcon::Particle, emitter);
    });
}

void GizmoOverlay::markAudio(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene&              scene     = ec.frame.scene;
    const ResourceManager&    resources = ec.frame.resources;
    const DebugPalette::Color audio     = DebugPalette::of(DebugCategory::Audio);

    scene.forEach<AudioSource, Transform>([&](EntityId id, const AudioSource& source, const Transform& tf) {
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (!scope.project(pos, sp)) return;
        // Faded while it has nothing to play.
        const bool  armed = source.clip && resources.isAlive(source.clip);
        const ImU32 col   = markerColor(ec, id, armed ? audio : DebugPalette::QUIET_AUDIO);
        markEntity(scope.dl, source.spatial ? EditorIcon::Audio : EditorIcon::Audio2D, id, sp, pos, col);
        if (source.playing) {
            const float ring = entityMarkerHitRadius() + EditorStyle::px(2.0f);
            scope.dl->AddCircle(sp, ring, col, 0, EditorStyle::px(1.5f));
        }
    });

    const EntityId ear = findActiveListener(scene);
    const EntityId eye = ec.frame.visibility->cameraEntity;
    scene.forEach<AudioListener, Transform>([&](EntityId id, const AudioListener&, const Transform& tf) {
        // The camera the frame renders through is the viewer, as in markCameras.
        if (id == eye) return;
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (!scope.project(pos, sp)) return;
        // Full only for findActiveListener's pick; the others are inert.
        const ImU32 col = markerColor(ec, id, id == ear ? audio : DebugPalette::QUIET_AUDIO);
        markEntity(scope.dl, EditorIcon::Listener, id, sp, pos, col);
    });
}

void GizmoOverlay::markCameras(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene&              scene  = ec.frame.scene;
    const DebugPalette::Color camera = DebugPalette::of(DebugCategory::Cameras);
    const EntityId            eye    = ec.frame.visibility->cameraEntity;
    scene.forEach<Camera, Transform>([&](EntityId id, const Camera&, const Transform& tf) {
        // The camera the frame renders through is the viewer.
        if (id == eye) return;
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (scope.project(pos, sp)) {
            markEntity(scope.dl, EditorIcon::Camera, id, sp, pos, markerColor(ec, id, camera));
        }
    });
}

void GizmoOverlay::markJoints(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene& scene = ec.frame.scene;
    scene.forEach<Joint, Transform>([&](EntityId id, const Joint& joint, const Transform& tf) {
        const bool selected = ec.state.isSelected(id)
            || (joint.connected && ec.state.isSelected(joint.connected));
        const ImU32 col = EditorStyle::paletteU32(selected ? DebugPalette::HIGHLIGHT : DebugPalette::JOINT);

        // As the solver reads them: each in its body's frame, position and rotation only. Marked
        // halfway along the rope, or at the one anchor of a joint that is off (Joint::connected).
        glm::vec3 at = resolvedWorldPosition(scene, id, tf)
            + resolvedWorldRotation(scene, id, tf) * joint.anchor;
        const Transform* other = joint.connected != id ? scene.tryGet<Transform>(joint.connected) : nullptr;
        if (other) {
            const glm::vec3 b = resolvedWorldPosition(scene, joint.connected, *other)
                + resolvedWorldRotation(scene, joint.connected, *other) * joint.connectedAnchor;
            at = (at + b) * 0.5f;
        }
        ImVec2 sp;
        if (scope.project(at, sp)) markEntity(scope.dl, EditorIcon::Joint, id, sp, at, col);
    });
}

void GizmoOverlay::drawUIRects(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid() || ec.state.selection.empty()) return;

    // A UI element is no object: its rect, as UISystem resolved it in viewport pixels, scaled
    // to the panel's in case the two count pixels differently.
    const HostChrome::ViewportRect vp = ec.frame.chrome.viewport(ec.frame.window);
    if (vp.width == 0 || vp.height == 0) return;
    const ImVec2 toPanel(ec.viewportSize.x / vp.width, ec.viewportSize.y / vp.height);
    for (const EntityId id : ec.state.selection) {
        const UIElement* element = ec.frame.scene.tryGet<UIElement>(id);
        if (!element || element->screenRect.size.x <= 0.0f || element->screenRect.size.y <= 0.0f) continue;
        const UIRect& rect = element->screenRect;
        const ImVec2  min(
            ec.viewportPos.x + rect.pos.x * toPanel.x,
            ec.viewportPos.y + rect.pos.y * toPanel.y
        );
        const ImVec2  max(
            ec.viewportPos.x + rect.max().x * toPanel.x,
            ec.viewportPos.y + rect.max().y * toPanel.y
        );
        const DebugPalette::Color col = id == ec.state.selectedEntity
            ? DebugPalette::HIGHLIGHT
            : DebugPalette::SECONDARY;
        scope.dl->AddRect(min, max, EditorStyle::paletteU32(col), 0.0f, 0, EditorStyle::px(1.5f));
    }
}

} // namespace Vkm::Engine
