#include "overlays/gizmo/entity_markers.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>

#include "core/frame_context.h"
#include "debug/draw/debug_draw.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/light/light.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "ui/style/editor_icons.h"
#include "ui/entity_labels.h"
#include "overlays/gizmo/click_target.h"
#include "view/viewport_projection.h"
#include "ui/style/editor_style.h"
#include "core/frame/visibility.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/effect/decal.h"
#include "ecs/component/light/irradiance_volume.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/light/reflection_probe.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_rect.h"
#include "core/host_chrome.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

// Entity marker disc: translucent, to lift the glyph off a bright sky without hiding geometry.
constexpr ImU32 MARKER_DISC_COL = IM_COL32(15, 15, 18, 180);

/**
 * @brief Draw the viewport's marker for an entity: a glyph on a dim disc.
 *
 * markEntity draws it and makes it pickable within entityMarkerHitRadius().
 *
 * @param dl Draw list to append to.
 * @param icon Glyph naming the entity's kind.
 * @param center Projected screen-space position.
 * @param col Glyph color; the disc is fixed.
 */

void drawEntityMarker(ImDrawList* dl, EditorIcon icon, ImVec2 center, ImU32 col) {
    dl->AddCircleFilled(center, entityMarkerHitRadius(), MARKER_DISC_COL, 16);
    drawEditorIcon(dl, icon, center, entityMarkerRadius() * 0.85f, col);
}

/**
 * @brief A marker pass's view, its clip rect pushed for its life; valid() is false with no camera: return.
 */
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
    return EditorStyle::paletteU32(ec.state.selection.contains(id) ? DebugPalette::HIGHLIGHT : color);
}

} // namespace

void EntityMarkers::markEntity(
    const EditorContext& ec,
    ImDrawList* dl,
    EditorIcon icon,
    EntityId id,
    ImVec2 screen,
    const glm::vec3& world,
    ImU32 col,
    float reach
) {
    // The Hierarchy's eye takes a marker away; its lock leaves it drawn but never picked.
    const Scene& scene = ec.frame.scene;
    if (!pickable(scene, id, ec.state.hiddenInView)) return;
    drawEntityMarker(dl, icon, screen, col);
    if (!pickable(scene, id, ec.state.lockedInView)) return;

    // Stamped with the frame: an earlier frame's list holds markers not drawn this time.
    const int frame = ImGui::GetFrameCount();
    if (frame != m_markerFrame) {
        m_markers.clear();
        m_markerFrame = frame;
    }
    m_markers.push_back({id, screen, world, reach});
}

void EntityMarkers::markLights(EditorContext& ec) {
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
        markEntity(ec, scope.dl, lightIcon(light.type), id, sp, pos, col, reach);
    });
}

void EntityMarkers::markProbes(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene& scene = ec.frame.scene;
    const DebugPalette::Color probe = DebugPalette::of(DebugCategory::Probes);
    const auto mark = [&](EntityId id, const Transform& tf, EditorIcon icon, DebugPalette::Color color) {
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (scope.project(pos, sp)) markEntity(ec, scope.dl, icon, id, sp, pos, markerColor(ec, id, color));
    };
    scene.forEach<ReflectionProbe, Transform>([&](EntityId id, const ReflectionProbe&, const Transform& tf) {
        mark(id, tf, EditorIcon::Probe, probe);
    });
    const auto markVolume = [&](EntityId id, const IrradianceVolume&, const Transform& tf) {
        mark(id, tf, EditorIcon::Volume, DebugPalette::IRRADIANCE_VOLUME);
    };
    scene.forEach<IrradianceVolume, Transform>(markVolume);
}

void EntityMarkers::markEffects(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene& scene = ec.frame.scene;
    const auto mark = [&](EntityId id, const Transform& tf, EditorIcon icon, DebugPalette::Color color) {
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (scope.project(pos, sp)) markEntity(ec, scope.dl, icon, id, sp, pos, markerColor(ec, id, color));
    };
    scene.forEach<Decal, Transform>([&](EntityId id, const Decal&, const Transform& tf) {
        mark(id, tf, EditorIcon::Decal, DebugPalette::DECAL);
    });
    const DebugPalette::Color emitter = DebugPalette::of(DebugCategory::Effects);
    scene.forEach<ParticleEmitter, Transform>([&](EntityId id, const ParticleEmitter&, const Transform& tf) {
        mark(id, tf, EditorIcon::Particle, emitter);
    });
}

void EntityMarkers::markAudio(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene& scene = ec.frame.scene;
    const ResourceManager& resources = ec.frame.resources;
    const DebugPalette::Color audio = DebugPalette::of(DebugCategory::Audio);

    scene.forEach<AudioSource, Transform>([&](EntityId id, const AudioSource& source, const Transform& tf) {
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (!scope.project(pos, sp)) return;
        // Faded while it has nothing to play.
        const bool  armed = source.clip && resources.isAlive(source.clip);
        const ImU32 col   = markerColor(ec, id, armed ? audio : DebugPalette::QUIET_AUDIO);
        markEntity(ec, scope.dl, source.spatial ? EditorIcon::Audio : EditorIcon::Audio2D, id, sp, pos, col);
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
        markEntity(ec, scope.dl, EditorIcon::Listener, id, sp, pos, col);
    });
}

void EntityMarkers::markCameras(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene& scene = ec.frame.scene;
    const DebugPalette::Color camera = DebugPalette::of(DebugCategory::Cameras);
    const EntityId eye = ec.frame.visibility->cameraEntity;
    scene.forEach<Camera, Transform>([&](EntityId id, const Camera&, const Transform& tf) {
        // The camera the frame renders through is the viewer.
        if (id == eye) return;
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        ImVec2 sp;
        if (scope.project(pos, sp)) {
            markEntity(ec, scope.dl, EditorIcon::Camera, id, sp, pos, markerColor(ec, id, camera));
        }
    });
}

void EntityMarkers::markJoints(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid()) return;

    const Scene& scene = ec.frame.scene;
    scene.forEach<Joint, Transform>([&](EntityId id, const Joint& joint, const Transform& tf) {
        const bool selected = ec.state.selection.contains(id)
            || (joint.connected && ec.state.selection.contains(joint.connected));
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
        if (scope.project(at, sp)) markEntity(ec, scope.dl, EditorIcon::Joint, id, sp, at, col);
    });
}

void EntityMarkers::drawUIRects(EditorContext& ec) {
    ViewportOverlayScope scope(ec);
    if (!scope.valid() || ec.state.selection.entities().empty()) return;

    // A UI element is no object: its rect, as UISystem resolved it in viewport pixels, scaled
    // to the panel's in case the two count pixels differently.
    const HostChrome::ViewportRect vp = ec.frame.chrome.viewportIn(ec.frame.window);
    if (vp.width == 0 || vp.height == 0) return;
    const ImVec2 toPanel(ec.viewportSize.x / vp.width, ec.viewportSize.y / vp.height);
    for (const EntityId id : ec.state.selection.entities()) {
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
        const DebugPalette::Color col = id == ec.state.selection.active()
            ? DebugPalette::HIGHLIGHT
            : DebugPalette::SECONDARY;
        scope.dl->AddRect(min, max, EditorStyle::paletteU32(col), 0.0f, 0, EditorStyle::px(1.5f));
    }
}

const std::vector<EntityMarkers::Marker>& EntityMarkers::drawn() const {
    static const std::vector<Marker> none;
    return m_markerFrame == ImGui::GetFrameCount() ? m_markers : none;
}

} // namespace Vkm::Engine
