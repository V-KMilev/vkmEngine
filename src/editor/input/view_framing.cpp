#include "input/view_framing.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

#include "core/math/bounds.h"
#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/mesh.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "input/camera_controller_system.h"
#include "resource/resource_manager.h"
#include "system/visibility/visibility.h"

namespace Vkm::Engine::ViewFraming {

void frameSelected(const FrameContext& ctx, EntityId selected, CameraControllerSystem& camera) {
    if (!ctx.scene.has<Transform>(selected)) return;

    // World space, so a child under a scaled parent frames at its drawn size.
    const glm::mat4 world = HierarchyOperations::computeWorldMatrix(ctx.scene, selected);
    glm::vec3 targetPos     = glm::vec3(world[3]);
    float     focusDistance = 5.0f;

    const Mesh*      mesh  = ctx.scene.tryGet<Mesh>(selected);
    const MeshAsset* asset = mesh ? ctx.resources.tryGet(mesh->mesh) : nullptr;
    if (asset && asset->bounds().valid()) {
        const Math::AABB box    = Math::transform(world, asset->bounds());
        const glm::vec3  extent = box.max - box.min;
        targetPos     = box.center();
        focusDistance = glm::max(glm::max(extent.x, glm::max(extent.y, extent.z)) * 1.5f, 2.0f);
    }

    camera.focusOn(targetPos, focusDistance);
}

void frameAll(const FrameContext& ctx, CameraControllerSystem& camera) {
    if (!ctx.visibility || ctx.visibility->objects.visible.empty()) return;

    // Posed world boxes from visibility, so a rig frames where its bones put it.
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    const RenderObjects& objects = ctx.visibility->objects;
    for (const uint32_t object : objects.visible) {
        mn = glm::min(mn, objects.bounds[object].min);
        mx = glm::max(mx, objects.bounds[object].max);
    }

    const glm::vec3 center = (mn + mx) * 0.5f;
    const glm::vec3 extent = mx - mn;
    const float diag = glm::length(extent);
    // Floored so a tiny scene does not pull the camera inside its geometry.
    const float distance = std::max(2.0f, diag * 1.1f);
    camera.focusOn(center, distance);
}

float clearance(const FrameContext& ctx, const glm::vec3& target) {
    if (!ctx.visibility) return 0.0f;

    float reach = 0.0f;
    for (const Math::AABB& box : ctx.visibility->objects.bounds) {
        // The furthest corner is the one away from the target on every axis.
        const glm::vec3 corner = glm::max(glm::abs(box.min - target), glm::abs(box.max - target));
        reach = std::max(reach, glm::length(corner));
    }
    return reach;
}

} // namespace Vkm::Engine::ViewFraming
