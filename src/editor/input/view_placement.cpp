#include "input/view_placement.h"

#include <algorithm>
#include <vector>

#include "core/math/axes.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/terrain.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "input/view_framing.h"
#include "mesh_pick.h"
#include "resource/resource_manager.h"
#include "system/terrain/terrain_brush.h"
#include "system/terrain/terrain_system.h"

namespace Vkm::Engine::ViewPlacement {

bool firstSurface(
    const Scene& scene,
    const ResourceManager& resources,
    const Math::Ray& ray,
    float& t,
    const std::vector<EntityId>& ignore,
    EntityId* met
) {
    float    nearest = REACH;
    EntityId first{};
    const auto ignored = [&](EntityId id) {
        return std::find(ignore.begin(), ignore.end(), id) != ignore.end();
    };

    scene.forEach<Mesh, Transform>([&](EntityId id, const Mesh& mesh, const Transform&) {
        const MeshAsset* asset = resources.tryGet(mesh.mesh);
        if (!mesh.visible || !asset || ignored(id)) return;

        // The box first, in the mesh's own space: an affine map keeps t in world-ray terms.
        // Walked now, so a body Drop to Ground just moved is met where it went.
        const glm::mat4 model   = HierarchyOperations::computeWorldMatrix(scene, id);
        const glm::mat4 toLocal = glm::inverse(model);
        const glm::vec3 origin  = glm::vec3(toLocal * glm::vec4(ray.origin, 1.0f));
        const glm::vec3 dir     = glm::vec3(toLocal * glm::vec4(ray.direction, 0.0f));
        float hit = 0.0f;
        if (!Math::rayIntersectsAABB(origin, 1.0f / dir, asset->bounds(), hit)) return;
        if (asset->skin.empty() && !asset->indices.empty() && !rayHitsMesh(ray, model, *asset, hit)) return;
        if (hit < nearest) {
            nearest = hit;
            first   = id;
        }
    });

    scene.forEach<Terrain, Transform>([&](EntityId id, const Terrain& terrain, const Transform& local) {
        const TerrainAsset* asset = resources.tryGet(terrain.terrain);
        if (!asset || ignored(id)) return;

        // Placed rigidly, so the local ray's metres are the world's.
        const glm::mat4 toLocal = glm::inverse(terrainModel(scene, id, local));
        const glm::vec3 origin  = glm::vec3(toLocal * glm::vec4(ray.origin, 1.0f));
        const glm::vec3 dir     = glm::normalize(glm::mat3(toLocal) * ray.direction);
        float hit = 0.0f;
        if (TerrainBrush::hit(*asset, origin, dir, nearest, hit) && hit < nearest) {
            nearest = hit;
            first   = id;
        }
    });

    if (!first) return false;
    t = nearest;
    if (met) *met = first;
    return true;
}

bool dropBelow(const Scene& scene, const ResourceManager& resources, EntityId entity, float& drop) {
    const std::vector<EntityId> own = HierarchyOperations::collectSubtree(scene, entity);
    Math::AABB box = ViewFraming::worldBox(scene, resources, entity);
    for (const EntityId id : own) {
        if (!scene.has<Transform>(id)) continue;
        box.expand(ViewFraming::worldBox(scene, resources, id));
    }

    // From a little inside its base, so a surface it is sunk into or resting on is still found.
    const float lift = std::min(SINK_REACH, box.max.y - box.min.y);
    const glm::vec3 base(box.center().x, box.min.y, box.center().z);
    const Math::Ray down{base + Math::WORLD_AXIS_Y * lift, -Math::WORLD_AXIS_Y};
    float t = 0.0f;
    if (firstSurface(scene, resources, down, t, own)) {
        drop = t - lift;
        return true;
    }
    if (base.y < 0.0f) return false;
    drop = base.y;
    return true;
}

float meshBase(const ResourceManager& resources, MeshHandle mesh) {
    const MeshAsset* asset = resources.tryGet(mesh);
    return asset ? -asset->boundsMin.y : 0.0f;
}

glm::vec3 placeInView(const Scene& scene, const ResourceManager& resources, const ViewAim& aim, float base) {
    if (!aim.known) return glm::vec3(0.0f);
    return placeAlong(scene, resources, aim.ray, aim.focus, base);
}

glm::vec3 placeAlong(
    const Scene& scene,
    const ResourceManager& resources,
    const Math::Ray& ray,
    float ahead,
    float base
) {
    const glm::vec3 lift = Math::WORLD_AXIS_Y * base;
    float t = 0.0f;
    if (firstSurface(scene, resources, ray, t)) return ray.origin + ray.direction * t + lift;

    // An empty scene's floor is the grid.
    if (ray.direction.y < 0.0f) {
        const float ground = -ray.origin.y / ray.direction.y;
        if (ground >= 0.0f && ground <= MAX_DISTANCE) return ray.origin + ray.direction * ground + lift;
    }
    return ray.origin + ray.direction * std::clamp(ahead, MIN_DISTANCE, MAX_DISTANCE);
}

} // namespace Vkm::Engine::ViewPlacement
