#include "input/view_framing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/math/bounds.h"
#include "core/math/rotation.h"
#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/terrain.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "input/camera_controller_system.h"
#include "resource/resource_manager.h"
#include "system/terrain/terrain_system.h"
#include "system/visibility/visibility.h"

namespace Vkm::Engine::ViewFraming {

namespace {

/// Room left around what is framed, as a share of its size.
constexpr float FRAME_MARGIN = 1.1f;

/// The least radius framed, so a light or an empty is not looked at from inside it.
constexpr float MIN_FRAME_RADIUS = 1.0f;

} // namespace

Math::AABB worldBox(const Scene& scene, const ResourceManager& resources, EntityId id) {
    const Transform& local = scene.get<Transform>(id);
    if (const Terrain* terrain = scene.tryGet<Terrain>(id)) {
        const TerrainAsset* asset = resources.tryGet(terrain->terrain);
        if (asset && !asset->heights.empty()) {
            const auto [low, high] = std::minmax_element(asset->heights.begin(), asset->heights.end());
            const float half = asset->size * 0.5f;
            const Math::AABB grid{glm::vec3(-half, *low, -half), glm::vec3(half, *high, half)};
            return Math::transform(terrainModel(scene, id, local), grid);
        }
    }
    // Walked now, so a child under a scaled parent frames at its drawn size.
    const glm::mat4  world = HierarchyOperations::computeWorldMatrix(scene, id);
    const Mesh*      mesh  = scene.tryGet<Mesh>(id);
    const MeshAsset* asset = mesh ? resources.tryGet(mesh->mesh) : nullptr;
    if (asset && asset->bounds().valid()) return Math::transform(world, asset->bounds());
    const glm::vec3 at(world[3]);
    return {at, at};
}

void viewFromEntity(const Scene& scene, EntityId entity, CameraControllerSystem& camera) {
    if (!camera.isActive() || !scene.has<Transform>(entity)) return;
    const glm::mat4 world = HierarchyOperations::computeWorldMatrix(scene, entity);
    EditorViewpoint view  = camera.viewpoint();
    view.position = glm::vec3(world[3]);
    Math::toYawPitch(Math::computeForward(Math::worldRotationOf(world)), view.yaw, view.pitch);
    camera.lookThrough({});
    camera.setViewpoint(view);
}

float fitDistance(float radius, const CameraControllerSystem& camera) {
    return std::max(radius, MIN_FRAME_RADIUS) * FRAME_MARGIN / std::sin(camera.fieldOfView() * 0.5f);
}

void frameSelected(
    const FrameContext& ctx,
    const std::vector<EntityId>& selection,
    CameraControllerSystem& camera
) {
    const Scene& scene = ctx.scene;

    // Every entity the selection holds, each once however the selection nests.
    std::vector<EntityId> framed;
    std::vector<bool>     counted;
    for (const EntityId selected : selection) {
        if (!scene.isAlive(selected)) continue;
        for (const EntityId id : HierarchyOperations::collectSubtree(scene, selected)) {
            if (!scene.has<Transform>(id)) continue;
            if (counted.size() <= id.slot()) counted.resize(id.slot() + 1, false);
            if (counted[id.slot()]) continue;
            counted[id.slot()] = true;
            framed.push_back(id);
        }
    }
    if (framed.empty()) return;

    // The frame's posed boxes first; what it drew none for brings its own.
    Math::AABB box = Math::AABB::empty();
    std::vector<bool> drawn(counted.size(), false);
    if (ctx.visibility) {
        const Visibility& visibility = *ctx.visibility;
        for (size_t object = 0; object < visibility.entities.size(); ++object) {
            const EntityId id = visibility.entities[object];
            if (id.slot() >= counted.size() || !counted[id.slot()] || !scene.isAlive(id)) continue;
            box.expand(visibility.objects.bounds[object]);
            drawn[id.slot()] = true;
        }
    }
    for (const EntityId id : framed) {
        if (!drawn[id.slot()]) box.expand(worldBox(scene, ctx.resources, id));
    }

    camera.focusOn(box.center(), fitDistance(glm::length(box.max - box.min) * 0.5f, camera));
}

void frameAll(const FrameContext& ctx, CameraControllerSystem& camera) {
    if (!ctx.visibility || ctx.visibility->objects.visible.empty()) return;

    // Posed world boxes from visibility, so a rig frames where its bones put it.
    Math::AABB           all     = Math::AABB::empty();
    const RenderObjects& objects = ctx.visibility->objects;
    for (const uint32_t object : objects.visible) all.expand(objects.bounds[object]);

    camera.focusOn(all.center(), fitDistance(glm::length(all.max - all.min) * 0.5f, camera));
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
