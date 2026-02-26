#include "../editor_common.h"

#include <glm/gtx/matrix_decompose.hpp>

#include "platform/window/window_manager.h"
#include "platform/window/input_handle.h"
#include "system/visibility/bounds_utils.h"
#include "resource/resource_manager.h"

namespace Engine {

void EditorSystem::drawTransformGizmo(FrameContext& ctx, ImVec2 vpMin, float vpWidth, float vpHeight) {
    if (!m_selectedEntity || !ctx.scene.isAlive(m_selectedEntity)) return;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;
    if (!ctx.scene.has<Transform>(m_selectedEntity)) return;

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

    auto& transform = ctx.scene.get<Transform>(m_selectedEntity);

    // For parented entities, manipulate in world space and convert back to local
    bool hasParent = ctx.scene.has<Hierarchy>(m_selectedEntity)
                  && ctx.scene.get<Hierarchy>(m_selectedEntity).parent;

    glm::mat4 parentWorld = glm::mat4(1.0f);
    if (hasParent) {
        parentWorld = HierarchyUtils::computeWorldMatrix(ctx.scene, m_selectedEntity);
        glm::mat4 localModel = Transform::computeModelMatrix(transform);
        parentWorld = parentWorld * glm::inverse(localModel);
    }

    glm::mat4 model = hasParent
        ? parentWorld * Transform::computeModelMatrix(transform)
        : Transform::computeModelMatrix(transform);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (m_gizmo.manipulate(drawList, ctx.visibility->view, subProj,
                            m_gizmoOperation, m_gizmoMode, model,
                            vpMin, vpWidth, vpHeight)) {

        // Convert back to local space if parented
        if (hasParent) {
            model = glm::inverse(parentWorld) * model;
        }

        glm::vec3 pos, scale, skew;
        glm::vec4 persp;
        glm::quat rot;
        glm::decompose(model, scale, rot, pos, skew, persp);

        transform.position = pos;
        transform.rotation = glm::normalize(rot);
        transform.scale    = scale;
    }
}

void EditorSystem::handleViewportPick(FrameContext& ctx, ImVec2 vpMin, float vpWidth, float vpHeight) {
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    auto& mouse = ctx.window.getInputHandle().getMouse();
    bool leftNow = mouse.isButtonPressed(0); // GLFW_MOUSE_BUTTON_LEFT
    bool leftJustClicked = leftNow && !m_leftMouseWasDown;
    m_leftMouseWasDown = leftNow;

    if (!leftJustClicked) return;
    if (!m_viewportHovered) return;
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
            model = HierarchyUtils::computeWorldMatrix(ctx.scene, id);
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
            glm::mat4 wm = HierarchyUtils::computeWorldMatrix(ctx.scene, id);
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
        m_selectedEntity = hitEntity;
        m_hierarchyDirty = true;
    } else {
        // Click on empty space deselects
        m_selectedEntity = {};
    }
}

} // namespace Engine
