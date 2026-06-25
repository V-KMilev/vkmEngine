#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include <glm/gtc/quaternion.hpp>

#include "framework/editor_common.h"
#include "framework/editor_commands.h"
#include "system/visibility/visibility.h"
#include "system/visibility/bounds_utils.h"
#include "system/camera/camera_controller_system.h"
#include "resource/resource_manager.h"
#include "ecs/component/world_transform.h"

namespace Engine {

void GizmoOverlay::drawTransformGizmo(EditorContext& ec) {
    FrameContext& ctx     = ec.frame;
    EditorState&  state   = ec.state;
    ImVec2 vpMin          = ec.viewportPos;
    float  vpWidth        = ec.viewportSize.x;
    float  vpHeight       = ec.viewportSize.y;

    if (state.gizmoOperation == GizmoOperation::Select) return;  // pick-only, no handles
    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) return;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;
    if (!ctx.scene.has<Transform>(state.selectedEntity)) return;

    // The camera you fly *is* the viewport eye: a transform gizmo on it would
    // fight the fly controller (both write its Transform every frame). It can
    // still be selected - Camera params stay editable in the Inspector.
    if (state.selectedEntity == ec.cameraController.getCameraEntity().getID())
        return;

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
    }
    // Drag-end: only push if the transform actually changed during the drag
    // (no-op clicks on the gizmo don't deserve an undo entry).
    if (!m_gizmo.isUsing() && m_dragActive) {
        if (m_dragEntity && ctx.scene.isAlive(m_dragEntity)
                && ctx.scene.has<Transform>(m_dragEntity)) {
            const Transform& after = ctx.scene.get<Transform>(m_dragEntity);
            const Transform& bef   = m_dragStartTransform;
            const bool changed = bef.position != after.position
                              || bef.rotation != after.rotation
                              || bef.scale    != after.scale;
            if (changed) {
                state.commands.push(std::make_unique<TransformChangeCommand>(
                    m_dragEntity, bef, after, "Transform"));
            }
        }
        m_dragActive = false;
        m_dragEntity = {};
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

        // Local transform changed - mark this entity's hierarchy subtree dirty
        // so HierarchySystem recomputes WorldTransforms next frame.
        HierarchyOperations::markDirty(ctx.scene, state.selectedEntity);
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

    // Convert to NDC [-1, 1] in viewport space.
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
        if (!hasValidBounds(asset.boundsMin, asset.boundsMax)) continue;

        glm::vec3 worldMin, worldMax;
        localToWorldAABB(v.model, asset.boundsMin, asset.boundsMax, worldMin, worldMax);

        float t;
        if (rayIntersectsAABB(rayOrigin, invDir, worldMin, worldMax, t) && t < nearestT) {
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

        glm::vec3 pos = ctx.scene.has<WorldTransform>(id)
            ? glm::vec3(ctx.scene.get<WorldTransform>(id).model[3])
            : transform.position;

        // Pick AABB scales with the light's reach: big area lights stay easy
        // to hit, tiny point lights still need a near click. Directionals have
        // no radius so fall back to a fixed value.
        const float radius = (light.type == LightType::Directional)
            ? 0.5f
            : std::clamp(light.radius * 0.2f, 0.3f, 3.0f);
        const glm::vec3 lightMin = pos - glm::vec3(radius);
        const glm::vec3 lightMax = pos + glm::vec3(radius);

        float t;
        if (rayIntersectsAABB(rayOrigin, invDir, lightMin, lightMax, t) && t < nearestT) {
            nearestT = t;
            hitEntity = id;
        }
    });

    // Selection is editor UI state - it does not modify the scene. Setting
    // sceneDirty here used to prompt the user to save just for clicking.
    // hierarchyDirty is still raised so the Hierarchy panel re-highlights.
    if (hitEntity) {
        state.selectEntity(hitEntity);
    } else {
        // Click on empty space deselects
        state.deselect();
    }
    state.hierarchyDirty = true;
}

} // namespace Engine
