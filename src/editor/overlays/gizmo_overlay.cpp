#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <limits>
#include <memory>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <glm/glm.hpp>

#include "command/component_edit.h"
#include "editor_actions.h"
#include "core/system.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "ui/editor_icons.h"
#include "command/editor_commands.h"
#include "system/ui/ui_draw_data.h"
#include "system/visibility/visibility.h"
#include "core/math/bounds.h"
#include "core/math/rotation.h"
#include "resource/resource_manager.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/hierarchy_operations.h"
#include "overlays/mesh_pick.h"
#include "overlays/wire_draw.h"

namespace Vkm::Engine {

void GizmoOverlay::finishDrag(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    auto changedOf = [&](EntityId id, const Transform& bef, const Transform*& after) {
        after = ctx.scene.tryGet<Transform>(id);
        if (!after) return false;
        return bef.position != after->position
            || bef.rotation != after->rotation
            || bef.scale    != after->scale;
    };

    // The step, not the push: a multi-entity drag collects its steps into one CompositeCommand.
    auto stepFor = [&](
        EntityId id,
        const Transform& bef,
        const Transform& after
    ) -> std::unique_ptr<Command> {
        return editStep<Transform>(ctx.scene, ctx.resources, id, bef, after, "Transform");
    };

    if (!m_dragSelection.empty()) {
        // Even a single root: if the active entity descends from it, only the root moved.
        auto batch = std::make_unique<CompositeCommand>(
            m_dragSelection.size() > 1 ? "Transform Selection" : "Transform"
        );
        for (const auto& [id, bef] : m_dragSelection) {
            const Transform* after = nullptr;
            if (changedOf(id, bef, after)) batch->add(stepFor(id, bef, *after));
        }
        if (!batch->empty()) state.commands.push(std::move(batch));
    } else {
        const Transform* after = nullptr;
        if (changedOf(m_dragEntity, m_dragStartTransform, after)) {
            state.commands.push(stepFor(m_dragEntity, m_dragStartTransform, *after));
        }
    }
    m_gizmo.endDrag();
    m_dragActive             = false;
    m_dragActiveIsDescendant = false;
    m_dragEntity             = {};
    m_dragSelection.clear();
}

void GizmoOverlay::drawTransformGizmo(EditorContext& ec) {
    FrameContext& ctx     = ec.frame;
    EditorState&  state   = ec.state;
    ImVec2 vpMin          = ec.viewportPos;
    float  vpWidth        = ec.viewportSize.x;
    float  vpHeight       = ec.viewportSize.y;

    // Not the camera the frame renders through (the drag is measured in its view), not
    // the Select tool, and not while the game has the viewport's clicks.
    const bool canManipulate = state.tool != EditorTool::Select
        && !ec.input.gameHasViewport()
        && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)
        && ctx.visibility && ctx.visibility->hasCamera
        && ctx.scene.has<Transform>(state.selectedEntity)
        && state.selectedEntity != ctx.visibility->cameraEntity;

    // Before the draw guard: a shortcut can switch tool or selection mid-drag.
    if (m_dragActive && (!canManipulate || !m_gizmo.isUsing())) finishDrag(ec);
    if (!canManipulate) {
        // Skipping manipulate() would leave a stale handle hover reported.
        m_gizmo.cancelInteraction();
        return;
    }

    auto& transform = ctx.scene.get<Transform>(state.selectedEntity);

    // A parented entity is manipulated in world space and converted back to local.
    const Hierarchy* node = ctx.scene.tryGet<Hierarchy>(state.selectedEntity);
    bool hasParent = node && node->parent;

    glm::mat4 parentWorld = glm::mat4(1.0f);
    if (hasParent) {
        parentWorld = HierarchyOperations::computeWorldMatrix(ctx.scene, state.selectedEntity);
        glm::mat4 localModel = Transform::computeModelMatrix(transform);
        parentWorld = parentWorld * glm::inverse(localModel);
    }

    glm::mat4 model = hasParent
        ? parentWorld * Transform::computeModelMatrix(transform)
        : Transform::computeModelMatrix(transform);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const bool snap = state.prefs.snapEnabled || ImGui::GetIO().KeyCtrl;
    const GizmoSnap snapping{
        state.prefs.snapTranslate,
        glm::radians(state.prefs.snapRotate),
        state.prefs.snapScale
    };
    m_gizmo.setSnap(snap ? snapping : GizmoSnap{});

    if (m_gizmo.isUsing() && !m_dragActive) {
        m_dragStartTransform    = transform;
        m_dragEntity            = state.selectedEntity;
        m_dragStartParentWorld  = parentWorld;
        m_dragActive            = true;

        m_dragSelection.clear();
        const EntityId eye = ctx.visibility->cameraEntity;
        for (EntityId id : state.selection) {
            const Transform* at = ctx.scene.tryGet<Transform>(id);
            if (!at || id == eye) continue;
            if (EditorActions::hasSelectedAncestor(ctx.scene, state.selection, id)) continue;
            m_dragSelection.emplace_back(id, *at);
        }
        m_dragActiveIsDescendant = !m_dragSelection.empty()
            && EditorActions::hasSelectedAncestor(ctx.scene, state.selection, state.selectedEntity);
    }
    const bool moved = m_gizmo.manipulate(
        drawList,
        ctx.visibility->camera.view,
        ctx.visibility->camera.projection,
        operationFor(state.tool),
        state.gizmoMode,
        model,
        vpMin,
        vpWidth,
        vpHeight,
        ec.input.gizmoMayHover()
    );
    if (moved) {

        if (state.tool == EditorTool::Rotate) {
            // Apply the delta to the start rotation: no matrix decomposition, no quaternion flips.
            glm::quat deltaRot = m_gizmo.getDragRotation();

            if (hasParent) {
                // To local through the parent's rotation alone: quat_cast of a scaled basis is not one.
                glm::quat parentRot = Math::worldRotationOf(parentWorld);
                glm::quat invParentRot = glm::inverse(parentRot);
                deltaRot = invParentRot * deltaRot * parentRot;
            }

            transform.rotation = glm::normalize(deltaRot * m_dragStartTransform.rotation);
        } else {
            if (hasParent) {
                model = glm::inverse(parentWorld) * model;
            }

            // Each tool writes only its field: re-deriving scale from column lengths un-mirrors.
            if (state.tool == EditorTool::Translate) {
                transform.position = glm::vec3(model[3]);
            } else {
                const glm::vec3 scale(
                    glm::length(glm::vec3(model[0])),
                    glm::length(glm::vec3(model[1])),
                    glm::length(glm::vec3(model[2]))
                );

                // Column lengths are unsigned and the drag factor positive, so restore the start's signs.
                transform.scale = scale * glm::sign(m_dragStartTransform.scale);
            }
        }

        // From each drag-start snapshot, never incrementally, so error does not accumulate.
        if (!m_dragSelection.empty()) {
            // Now through this frame's basis, which derived it; the start through the pinned one.
            const glm::vec3 worldNow  = glm::vec3(parentWorld * glm::vec4(transform.position, 1.0f));
            const glm::vec3 worldThen = glm::vec3(
                m_dragStartParentWorld * glm::vec4(m_dragStartTransform.position, 1.0f)
            );
            const glm::vec3 worldDelta = worldNow - worldThen;
            const glm::quat worldRot = m_gizmo.getDragRotation();

            // World terms and pinned, likewise: this loop scales a selected ancestor's basis,
            // which a local ratio would divide out.
            const auto basisScale = [](const glm::mat4& basis) {
                return glm::vec3(
                    glm::length(glm::vec3(basis[0])),
                    glm::length(glm::vec3(basis[1])),
                    glm::length(glm::vec3(basis[2]))
                );
            };
            const glm::vec3 worldScale = basisScale(parentWorld) * transform.scale;
            const glm::vec3 startScale = basisScale(m_dragStartParentWorld) * m_dragStartTransform.scale;
            const glm::vec3 ratio(
                startScale.x != 0.0f ? worldScale.x / startScale.x : 1.0f,
                startScale.y != 0.0f ? worldScale.y / startScale.y : 1.0f,
                startScale.z != 0.0f ? worldScale.z / startScale.z : 1.0f
            );

            for (const auto& [id, start] : m_dragSelection) {
                if (id == state.selectedEntity) continue;
                Transform* held = ctx.scene.tryGet<Transform>(id);
                if (!held) continue;
                Transform& t = *held;

                glm::mat4 pw(1.0f);
                const Hierarchy* link = ctx.scene.tryGet<Hierarchy>(id);
                if (link && link->parent) {
                    pw = HierarchyOperations::computeWorldMatrix(ctx.scene, id)
                       * glm::inverse(Transform::computeModelMatrix(t));
                }

                if (state.tool == EditorTool::Translate) {
                    const glm::vec3 worldStart = glm::vec3(pw * glm::vec4(start.position, 1.0f));
                    t.position = glm::vec3(glm::inverse(pw) * glm::vec4(worldStart + worldDelta, 1.0f));
                } else if (state.tool == EditorTool::Rotate) {
                    const glm::quat parentRot = Math::worldRotationOf(pw);
                    const glm::quat localRot  = glm::inverse(parentRot) * worldRot * parentRot;
                    t.rotation = glm::normalize(localRot * start.rotation);
                } else if (state.tool == EditorTool::Scale) {
                    t.scale = start.scale * ratio;
                }
            }

            // A moving ancestor carries it; undo the gizmo's direct write so it does not move twice.
            if (m_dragActiveIsDescendant) transform = m_dragStartTransform;
        }

        state.markSceneDirty();
    }
}

void GizmoOverlay::handleViewportPick(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (!ec.input.clickPicks()) return;
    // This frame's handle, which ownership (decided from last frame) cannot know.
    if (m_gizmo.isOver() || m_gizmo.isUsing()) return;

    // The game's UI is over the scene, so a click on it selects the element.
    if (ec.input.pointer == PointerOwner::GameUI) {
        if (ctx.ui) state.clickSelect(ctx.ui->pointerTarget);
        return;
    }

    // Through the viewport rect the 3D pass renders into, not the window.
    const ImVec2 mp = ImGui::GetMousePos();
    const Math::Ray ray = viewportRay(
        ctx.visibility->camera.invViewProj,
        mp,
        ec.viewportPos,
        ec.viewportSize
    );
    const glm::vec3& rayOrigin = ray.origin;
    const glm::vec3 invDir(1.0f / ray.direction.x, 1.0f / ray.direction.y, 1.0f / ray.direction.z);

    // The visible set only. A box is a candidate (a room's holds everything), so triangles
    // decide - except for a posed mesh, whose stored triangles are in bind pose.
    EntityId hitEntity{};
    float nearestT = std::numeric_limits<float>::max();

    const Visibility& visibility = *ctx.visibility;
    for (const uint32_t object : visibility.objects.visible) {
        float t;
        if (!Math::rayIntersectsAABB(rayOrigin, invDir, visibility.objects.bounds[object], t)) continue;

        const MeshAsset* mesh = ctx.resources.tryGet(visibility.objects.draws[object].mesh);
        if (mesh && mesh->skin.empty() && !mesh->indices.empty()
            && !rayHitsMesh(ray, visibility.objects.models[object], *mesh, t)) {
            continue;
        }
        if (t < nearestT) {
            nearestT  = t;
            hitEntity = visibility.entities[object];
        }
    }

    // Markers, tested in screen space at their drawn radius (any distance, ortho too); ranked
    // against meshes by world distance.
    if (m_markerFrame == ImGui::GetFrameCount()) {
        const float hitR = entityMarkerHitRadius();
        for (const Marker& marker : m_markers) {
            const float dx = mp.x - marker.screen.x;
            const float dy = mp.y - marker.screen.y;
            float t = std::numeric_limits<float>::max();
            // Depth along the ray, as a box hit is ranked.
            if (dx * dx + dy * dy <= hitR * hitR) t = glm::dot(marker.world - rayOrigin, ray.direction);

            float boxT;
            const glm::vec3  reach(marker.reach);
            const Math::AABB box{marker.world - reach, marker.world + reach};
            if (marker.reach > 0.0f && Math::rayIntersectsAABB(rayOrigin, invDir, box, boxT)) {
                t = std::min(t, boxT);
            }
            if (t < nearestT) {
                nearestT  = t;
                hitEntity = marker.id;
            }
        }
    }

    state.clickSelect(hitEntity);
}

} // namespace Vkm::Engine
