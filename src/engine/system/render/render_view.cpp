#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_view.h"

#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"
#include "system/visibility/visibility.h"

namespace Engine {

void RenderView::build(
    const Scene& scene,
    const Visibility& visibility
) {
    if (!visibility.hasCamera) {
        // No camera this frame: emit an empty snapshot, not a stale one.
        drawables.clear();
        lights.clear();
        shadowCasters.clear();
        LOG_ERROR("No active camera; nothing to render this frame");
        return;
    }

    buildCamera(visibility);
    buildDrawables(scene, visibility);
    buildLights(scene);
    buildShadowCasters(scene, visibility);
}

void RenderView::buildCamera(const Visibility& visibility) {
    camera.view       = visibility.view;
    camera.projection = visibility.projection;
    camera.position   = visibility.cameraPosition;
}

void RenderView::buildDrawables(const Scene& scene, const Visibility& visibility) {
    // Reuse capacity from the previous frame; only grows, never shrinks.
    drawables.clear();
    drawables.reserve(visibility.entries.size());

    // One drawable per visible entity. Just snapshot handles + matrix - the
    // backend resolves the handles and decides how to sort / batch them.
    for (const auto& entry : visibility.entries) {
        // deleted between cull and render
        if (!scene.isAlive(entry.id)) continue;
        const Mesh& mesh = scene.get<Mesh>(entry.id);
        // unresolved slot
        if (!mesh.mesh || !mesh.material) continue;

        DrawableData drawable;
        drawable.mesh        = mesh.mesh;
        drawable.material    = mesh.material;
        drawable.model       = entry.model;
        drawable.castShadows = mesh.castShadows;
        drawables.push_back(drawable);
    }
}

void RenderView::buildLights(const Scene& scene) {
    lights.clear();
    lights.reserve(scene.count<Light>());

    // Every enabled light, resolved to world space. Position/rotation come from
    // the WorldTransform when the light is parented, else its local Transform.
    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (!light.enabled) return;

        glm::vec3 position;
        glm::quat rotation;
        if (scene.has<WorldTransform>(id)) {
            const glm::mat4& world = scene.get<WorldTransform>(id).model;
            position = glm::vec3(world[3]);
            rotation = glm::quat_cast(glm::mat3(
                glm::normalize(glm::vec3(world[0])),
                glm::normalize(glm::vec3(world[1])),
                glm::normalize(glm::vec3(world[2]))
            ));
        } else {
            position = transform.position;
            rotation = transform.rotation;
        }

        LightData data;
        data.type      = light.type;
        data.color     = light.color;
        data.intensity = light.intensity;
        data.position  = position;
        data.direction = Math::computeForward(rotation);

        data.radius         = light.radius;
        data.innerConeAngle = light.innerConeAngle;
        data.outerConeAngle = light.outerConeAngle;

        data.castShadows    = light.castShadows;
        data.shadowBias     = light.shadowBias;
        data.shadowDistance = light.shadowDistance;

        // Area lights: fold rotation + size into world-space half-extent
        // axes. Disk uses areaRadius on both so cross(U, V) stays a clean
        // surface normal.
        if (light.type == LightType::Rect || light.type == LightType::Disk) {
            const glm::vec3 right = rotation * glm::vec3(1, 0, 0);
            const glm::vec3 up    = rotation * glm::vec3(0, 1, 0);
            if (light.type == LightType::Rect) {
                data.axisU = right * (light.areaWidth  * 0.5f);
                data.axisV = up    * (light.areaHeight * 0.5f);
            } else {
                data.axisU = right * light.areaRadius;
                data.axisV = up    * light.areaRadius;
            }
            data.twoSided = light.twoSided;
        }

        lights.push_back(data);
    });
}

void RenderView::buildShadowCasters(const Scene& scene, const Visibility& visibility) {
    shadowCasters.clear();
    shadowCasters.reserve(visibility.shadowCasters.size());

    // The caster set is scene-wide (not camera-culled); resolve each entity's
    // mesh handle and carry its world AABB so the backend can frustum-cull per
    // light. The model + bounds were already computed by the VisibilitySystem.
    for (const VisibleEntity& entry : visibility.shadowCasters) {
        // deleted between cull and render
        if (!scene.isAlive(entry.id)) continue;
        const Mesh& mesh = scene.get<Mesh>(entry.id);
        // unresolved slot
        if (!mesh.mesh) continue;

        shadowCasters.push_back({ mesh.mesh, entry.model, entry.worldMin, entry.worldMax });
    }
}

} // namespace Engine
