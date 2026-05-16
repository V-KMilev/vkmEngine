#include "gizmo_overlay.h"
#include "../editor_common.h"

#include <glm/gtc/quaternion.hpp>

#include "platform/window/window_manager.h"
#include "platform/window/input_handle.h"
#include "system/visibility/bounds_utils.h"
#include "resource/resource_manager.h"

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

    // The 3D rendering covers the full GLFW window (glViewport(0,0,W,H)),
    // so the projection maps clip [-1,1] to the full window, not just the
    // viewport child region. We remap the projection so clip [-1,1] maps
    // to the viewport sub-region, letting the gizmo use the viewport rect
    // for both correct position AND correct size scaling.
    float winW = static_cast<float>(ctx.window.getWidth());
    float winH = static_cast<float>(ctx.window.getHeight());

    float sx = winW / vpWidth;
    float sy = winH / vpHeight;
    float tx = (winW - vpWidth  - 2.0f * vpMin.x) / vpWidth;
    float ty = -(winH - vpHeight - 2.0f * vpMin.y) / vpHeight;

    glm::mat4 remap(1.0f);
    remap[0][0] = sx;  remap[3][0] = tx;
    remap[1][1] = sy;  remap[3][1] = ty;

    glm::mat4 subProj = remap * ctx.visibility->projection;

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

    // Capture start rotation when drag begins
    if (m_gizmo.isUsing() && !m_dragActive) {
        m_dragStartRot = transform.rotation;
        m_dragActive = true;
    }
    if (!m_gizmo.isUsing()) {
        m_dragActive = false;
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

            transform.rotation = glm::normalize(deltaRot * m_dragStartRot);
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

        // Local transform changed — mark this entity's hierarchy subtree dirty
        // so HierarchySystem recomputes WorldTransforms next frame.
        HierarchyOperations::markDirty(ctx.scene, state.selectedEntity);
    }
}

void GizmoOverlay::handleViewportPick(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    auto& mouse = ctx.window.getInputHandle().getMouse();
    bool leftNow = mouse.isButtonPressed(0); // GLFW_MOUSE_BUTTON_LEFT
    bool leftJustClicked = leftNow && !m_leftMouseWasDown;
    m_leftMouseWasDown = leftNow;

    if (!leftJustClicked) return;
    if (!state.viewportHovered) return;
    if (m_gizmo.isOver()) return;
    if (m_gizmo.isUsing()) return;

    // The projection maps clip space to the full window (glViewport covers
    // the entire GLFW window), so NDC must be computed from full window coords.
    float winW = static_cast<float>(ctx.window.getWidth());
    float winH = static_cast<float>(ctx.window.getHeight());

    float mouseX = static_cast<float>(mouse.getX());
    float mouseY = static_cast<float>(mouse.getY());

    // Convert to NDC [-1, 1]
    float ndcX =  (2.0f * mouseX / winW)  - 1.0f;
    float ndcY = -(2.0f * mouseY / winH) + 1.0f;

    // Unproject to world-space ray
    glm::mat4 invProj = glm::inverse(ctx.visibility->projection);
    glm::mat4 invView = glm::inverse(ctx.visibility->view);

    glm::vec4 clipRay(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 viewRay = invProj * clipRay;
    viewRay = glm::vec4(viewRay.x, viewRay.y, -1.0f, 0.0f);

    glm::vec3 worldDir = glm::normalize(glm::vec3(invView * viewRay));
    glm::vec3 rayOrigin = ctx.visibility->cameraPosition;
    glm::vec3 invDir(1.0f / worldDir.x, 1.0f / worldDir.y, 1.0f / worldDir.z);

    // Test against all mesh entities, find nearest hit
    EntityId hitEntity{};
    float nearestT = std::numeric_limits<float>::max();

    ctx.scene.forEach<Mesh, Transform>([&](EntityId id, const Mesh& mesh, const Transform& transform) {
        const auto& asset = ctx.resources.get(mesh.mesh);
        if (!hasValidBounds(asset.boundsMin, asset.boundsMax)) return;

        // Compute world matrix (hierarchy-aware)
        glm::mat4 model;
        if (ctx.scene.has<Hierarchy>(id) && ctx.scene.get<Hierarchy>(id).parent) {
            model = HierarchyOperations::computeWorldMatrix(ctx.scene, id);
        } else {
            model = Transform::computeModelMatrix(transform);
        }

        glm::vec3 worldMin, worldMax;
        localToWorldAABB(model, asset.boundsMin, asset.boundsMax, worldMin, worldMax);

        float t;
        if (rayIntersectsAABB(rayOrigin, invDir, worldMin, worldMax, t) && t < nearestT) {
            nearestT = t;
            hitEntity = id;
        }
    });

    // Also test light entities (no mesh, just position proximity)
    ctx.scene.forEach<Light, Transform>([&](EntityId id, const Light&, const Transform& transform) {
        if (ctx.scene.has<Mesh>(id)) return; // already tested above

        glm::vec3 pos = transform.position;
        if (ctx.scene.has<Hierarchy>(id) && ctx.scene.get<Hierarchy>(id).parent) {
            glm::mat4 wm = HierarchyOperations::computeWorldMatrix(ctx.scene, id);
            pos = glm::vec3(wm[3]);
        }

        // Use a small sphere-like AABB around the light position
        float radius = 0.5f;
        glm::vec3 lightMin = pos - glm::vec3(radius);
        glm::vec3 lightMax = pos + glm::vec3(radius);

        float t;
        if (rayIntersectsAABB(rayOrigin, invDir, lightMin, lightMax, t) && t < nearestT) {
            nearestT = t;
            hitEntity = id;
        }
    });

    if (hitEntity) {
        state.selectedEntity = hitEntity;
        state.hierarchyDirty = true;
    } else {
        // Click on empty space deselects
        state.selectedEntity = {};
    }
}

} // namespace Engine
