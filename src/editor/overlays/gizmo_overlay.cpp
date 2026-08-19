#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include <glm/gtc/quaternion.hpp>

#include "framework/component_edit.h"
#include "framework/editor_actions.h"
#include "framework/editor_common.h"
#include "framework/editor_commands.h"
#include "system/visibility/visibility.h"
#include "core/math/bounds.h"
#include "system/camera/camera_controller_system.h"
#include "resource/resource_manager.h"
#include "ecs/component/world_transform.h"

namespace Engine {

void GizmoOverlay::finishDrag(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    auto changedOf = [&](EntityId id, const Transform& bef, const Transform*& after) {
        if (!id || !ctx.scene.isAlive(id) || !ctx.scene.has<Transform>(id)) return false;
        after = &ctx.scene.get<Transform>(id);
        return bef.position != after->position
            || bef.rotation != after->rotation
            || bef.scale    != after->scale;
    };

    // The step, not the push: the drag marks the scene dirty as it goes and a
    // multi-entity drag collects its steps into one CompositeCommand.
    auto stepFor = [&](EntityId id, const Transform& bef,
                       const Transform& after) -> std::unique_ptr<Command> {
        return editStep<Transform>(ctx.scene, ctx.resources, id, bef, after, "Transform");
    };

    if (m_dragSelection.size() > 1) {
        // One history entry for the whole selection's motion.
        auto batch = std::make_unique<CompositeCommand>("Transform Selection");
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

    // The flown camera is excluded because it *is* the viewport eye: a transform
    // gizmo on it would fight the fly controller (both write its Transform every
    // frame). It can still be selected - Camera params stay editable in the
    // Inspector.
    const bool canManipulate =
           state.gizmoOperation != GizmoOperation::Select   // Select is pick-only, no handles
        && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)
        && ctx.visibility && ctx.visibility->hasCamera
        && ctx.scene.has<Transform>(state.selectedEntity)
        && state.selectedEntity != ec.cameraController.getCameraEntity();

    // Drag-end is decided before the draw guard, not after it: a shortcut can
    // switch tool or selection mid-drag, and whether the drag is over is not
    // the same question as whether the handles are drawn this frame. Only push
    // if the transform actually changed (a no-op click on the gizmo does not
    // deserve an undo entry).
    if (m_dragActive && (!canManipulate || !m_gizmo.isUsing())) finishDrag(ec);
    if (!canManipulate) return;

    // The 3D pass now renders into viewport-sized FBOs and composite
    // blits to the viewport sub-rect of the backbuffer (gl_composite_pass).
    // So the visibility projection (built with the viewport's aspect)
    // matches the rendered image 1:1 - no remap matrix needed.
    const glm::mat4 subProj = ctx.visibility->projection;

    auto& transform = ctx.scene.get<Transform>(state.selectedEntity);

    // For parented entities, manipulate in world space and convert back to local
    bool hasParent = ctx.scene.has<Hierarchy>(state.selectedEntity)
                  && ctx.scene.get<Hierarchy>(state.selectedEntity).parent;

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

    // Configure gizmo snap (rotation is handled inside the gizmo to avoid gimbal lock)
    bool snap = state.snapEnabled || ImGui::GetIO().KeyCtrl;
    m_gizmo.setSnapAngle(snap ? glm::radians(state.snapRotate) : 0.0f);

    // Snapshot the FULL transform when a drag begins so on drag-end we can
    // push one TransformChangeCommand covering the whole motion (instead of
    // one per frame). Rotation drags also re-use this as their start basis.
    if (m_gizmo.isUsing() && !m_dragActive) {
        m_dragStartTransform = transform;
        m_dragEntity         = state.selectedEntity;
        m_dragActive         = true;

        // Snapshot every selected transform so the drag moves the whole set
        // and drag-end can build one batch undo.
        //
        // Roots of the selection only. An entity whose ancestor is also selected
        // already inherits that ancestor's motion through the hierarchy; moving
        // it again on its own applied the delta twice, so dragging a parent and
        // its child together sent the child twice as far.
        m_dragSelection.clear();
        const EntityId flown = ec.cameraController.getCameraEntity();
        for (EntityId id : state.selection) {
            if (!ctx.scene.isAlive(id) || !ctx.scene.has<Transform>(id)) continue;
            if (id == flown) continue;
            if (EditorActions::hasSelectedAncestor(ctx.scene, state.selection, id)) continue;
            m_dragSelection.emplace_back(id, ctx.scene.get<Transform>(id));
        }
        m_dragActiveIsDescendant =
            m_dragSelection.size() > 1
            && EditorActions::hasSelectedAncestor(ctx.scene, state.selection, state.selectedEntity);
    }
    if (m_gizmo.manipulate(drawList, ctx.visibility->view, subProj,
                            state.gizmoOperation, state.gizmoMode, model,
                            vpMin, vpWidth, vpHeight)) {

        if (state.gizmoOperation == GizmoOperation::Rotate) {
            // For rotation: apply delta quaternion directly to start rotation
            // This completely bypasses matrix decomposition and avoids quaternion flips
            glm::quat deltaRot = m_gizmo.getDragRotation();

            if (hasParent) {
                // Convert world-space rotation delta to local space
                glm::quat parentRot = glm::quat_cast(glm::mat3(parentWorld));
                glm::quat invParentRot = glm::inverse(parentRot);
                deltaRot = invParentRot * deltaRot * parentRot;
            }

            transform.rotation = glm::normalize(deltaRot * m_dragStartTransform.rotation);
        } else {
            // For translate/scale: decompose is safe (no quaternion boundary issues)
            if (hasParent) {
                model = glm::inverse(parentWorld) * model;
            }

            glm::vec3 pos = glm::vec3(model[3]);
            glm::vec3 scale;
            scale.x = glm::length(glm::vec3(model[0]));
            scale.y = glm::length(glm::vec3(model[1]));
            scale.z = glm::length(glm::vec3(model[2]));

            if (snap) {
                auto snapValue = [](float v, float step) {
                    return std::round(v / step) * step;
                };
                if (state.gizmoOperation == GizmoOperation::Translate) {
                    float s = state.snapTranslate;
                    pos.x = snapValue(pos.x, s);
                    pos.y = snapValue(pos.y, s);
                    pos.z = snapValue(pos.z, s);
                } else if (state.gizmoOperation == GizmoOperation::Scale) {
                    float s = state.snapScale;
                    scale.x = snapValue(scale.x, s);
                    scale.y = snapValue(scale.y, s);
                    scale.z = snapValue(scale.z, s);
                }
            }

            transform.position = pos;
            transform.scale    = scale;
        }

        // Apply the active entity's delta to the rest of the selection, each
        // from its own drag-start snapshot (never incrementally, so error
        // does not accumulate). Translation is a world-space delta converted
        // into each entity's parent space; rotation applies in place (no
        // orbit around a shared pivot); scale is a component-wise ratio.
        if (m_dragSelection.size() > 1) {
            const glm::vec3 worldDelta =
                glm::vec3(parentWorld * glm::vec4(transform.position, 1.0f))
              - glm::vec3(parentWorld * glm::vec4(m_dragStartTransform.position, 1.0f));
            const glm::quat worldRot = m_gizmo.getDragRotation();
            const glm::vec3 startScale = m_dragStartTransform.scale;
            const glm::vec3 ratio(
                startScale.x != 0.0f ? transform.scale.x / startScale.x : 1.0f,
                startScale.y != 0.0f ? transform.scale.y / startScale.y : 1.0f,
                startScale.z != 0.0f ? transform.scale.z / startScale.z : 1.0f);

            for (const auto& [id, start] : m_dragSelection) {
                if (id == state.selectedEntity) continue;
                if (!ctx.scene.isAlive(id) || !ctx.scene.has<Transform>(id)) continue;
                Transform& t = ctx.scene.get<Transform>(id);

                glm::mat4 pw(1.0f);
                if (ctx.scene.has<Hierarchy>(id) && ctx.scene.get<Hierarchy>(id).parent) {
                    pw = HierarchyOperations::computeWorldMatrix(ctx.scene, id)
                       * glm::inverse(Transform::computeModelMatrix(t));
                }

                if (state.gizmoOperation == GizmoOperation::Translate) {
                    const glm::vec3 worldStart =
                        glm::vec3(pw * glm::vec4(start.position, 1.0f));
                    t.position = glm::vec3(glm::inverse(pw)
                                 * glm::vec4(worldStart + worldDelta, 1.0f));
                } else if (state.gizmoOperation == GizmoOperation::Rotate) {
                    const glm::quat parentRot = glm::quat_cast(glm::mat3(pw));
                    const glm::quat localRot  =
                        glm::inverse(parentRot) * worldRot * parentRot;
                    t.rotation = glm::normalize(localRot * start.rotation);
                } else if (state.gizmoOperation == GizmoOperation::Scale) {
                    t.scale = start.scale * ratio;
                }
            }

            // The gizmo wrote the active entity directly, but an ancestor of it
            // is moving too and that motion already reaches it through the
            // hierarchy. Put it back where the drag started so it is carried
            // rather than carried *and* pushed.
            if (m_dragActiveIsDescendant) transform = m_dragStartTransform;
        }

        state.markSceneDirty();
    }
}

void GizmoOverlay::handleViewportPick(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    // Input via ImGui (same source as the rest of the editor): the editor
    // gates mouse capture per-frame and ImGui handles edge detection.
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (!state.viewportHovered) return;
    if (m_gizmo.isOver()) return;
    if (m_gizmo.isUsing()) return;

    // The 3D pass renders into the editor's viewport rect, so mouse -> NDC
    // is computed against the viewport (not the full window).
    const ImVec2 mp = ImGui::GetMousePos();
    const float vpX = ec.viewportPos.x;
    const float vpY = ec.viewportPos.y;
    const float vpW = std::max(1.0f, ec.viewportSize.x);
    const float vpH = std::max(1.0f, ec.viewportSize.y);

    float ndcX =  (2.0f * (mp.x - vpX) / vpW) - 1.0f;
    float ndcY = -(2.0f * (mp.y - vpY) / vpH) + 1.0f;

    // Unproject to world-space ray
    glm::mat4 invProj = glm::inverse(ctx.visibility->projection);
    glm::mat4 invView = glm::inverse(ctx.visibility->view);

    glm::vec4 clipRay(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 viewRay = invProj * clipRay;
    viewRay = glm::vec4(viewRay.x, viewRay.y, -1.0f, 0.0f);

    glm::vec3 worldDir = glm::normalize(glm::vec3(invView * viewRay));
    glm::vec3 rayOrigin = ctx.visibility->cameraPosition;
    glm::vec3 invDir(1.0f / worldDir.x, 1.0f / worldDir.y, 1.0f / worldDir.z);

    // Test against the culled visible set instead of the whole scene: the
    // visibility pass already filtered frustum/distance/size AND precomputed
    // each entity's world matrix, so picking gets it for free. Off-screen /
    // hidden meshes are not pickable - that matches what the user can see.
    EntityId hitEntity{};
    float nearestT = std::numeric_limits<float>::max();

    for (const VisibleEntity& v : ctx.visibility->entries) {
        if (!ctx.scene.has<Mesh>(v.id)) continue;
        const Mesh& mesh = ctx.scene.get<Mesh>(v.id);
        if (!mesh.mesh || !ctx.resources.isAlive(mesh.mesh)) continue;
        const auto& asset = ctx.resources.get(mesh.mesh);
        if (!Math::hasValidBounds(asset.boundsMin, asset.boundsMax)) continue;

        glm::vec3 worldMin, worldMax;
        Math::localToWorldAABB(v.model, asset.boundsMin, asset.boundsMax, worldMin, worldMax);

        float t;
        if (Math::rayIntersectsAABB(rayOrigin, invDir, worldMin, worldMax, t) && t < nearestT) {
            nearestT = t;
            hitEntity = v.id;
        }
    }

    // Also test light entities (no mesh, just position proximity). Lights
    // aren't in the visibility set, but HierarchySystem already cached each
    // hierarchical entity's WorldTransform so we just read it.
    ctx.scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (ctx.scene.has<Mesh>(id)) return; // already tested above
        if (!light.enabled)          return; // unselectable when off, matches gizmo draw

        glm::vec3 pos = resolvedWorldPosition(ctx.scene, id, transform);

        // Pick AABB scales with the light's reach: big area lights stay easy
        // to hit, tiny point lights still need a near click. Directionals have
        // no radius so fall back to a fixed value.
        const float radius = (light.type == LightType::Directional)
            ? 0.5f
            : std::clamp(light.radius * 0.2f, 0.3f, 3.0f);
        const glm::vec3 lightMin = pos - glm::vec3(radius);
        const glm::vec3 lightMax = pos + glm::vec3(radius);

        float t;
        if (Math::rayIntersectsAABB(rayOrigin, invDir, lightMin, lightMax, t) && t < nearestT) {
            nearestT = t;
            hitEntity = id;
        }
    });

    // Selection is editor UI state - it does not modify the scene. Setting
    // sceneDirty here used to prompt the user to save just for clicking.
    // hierarchyDirty is still raised so the Hierarchy panel re-highlights.
    const bool ctrl  = ImGui::GetIO().KeyCtrl;
    const bool shift = ImGui::GetIO().KeyShift;
    if (hitEntity) {
        if (ctrl)       state.toggleSelection(hitEntity);
        else if (shift) state.addToSelection(hitEntity);
        else            state.selectEntity(hitEntity);
    } else if (!ctrl && !shift) {
        // Click on empty space deselects (modified clicks leave the set alone)
        state.deselect();
    }
    state.hierarchyDirty = true;
}

} // namespace Engine
