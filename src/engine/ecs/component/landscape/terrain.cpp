#include "ecs/component/landscape/terrain.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/component/scene/world_transform.h"

namespace Vkm::Engine {

glm::mat4 terrainModel(const Scene& scene, EntityId entity, const Transform& local) {
    return glm::translate(glm::mat4(1.0f), resolvedWorldPosition(scene, entity, local))
        * glm::mat4_cast(resolvedWorldRotation(scene, entity, local));
}

} // namespace Vkm::Engine
