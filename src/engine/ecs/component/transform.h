#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/math/rotation.h"
#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Component representing spatial transformation (position, rotation, scale) in 3D space.
 *
 * For pure quat/axis math, use the helpers in core/math/ (rotation.h, axes.h).
 */
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};        ///< Local position
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};  ///< Local rotation as quaternion (identity = no rotation)
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};        ///< Local scale

    /**
     * @brief Compute the model matrix from transform data.
     *
     * Uses fused TRS construction: builds translation, rotation, scale
     * directly without intermediate matrix multiplications.
     */
    static glm::mat4 computeModelMatrix(const Transform& transform) {
        const glm::mat4 rot = glm::mat4_cast(transform.rotation);

        glm::mat4 model;
        model[0] = rot[0] * transform.scale.x;
        model[1] = rot[1] * transform.scale.y;
        model[2] = rot[2] * transform.scale.z;
        model[3] = glm::vec4(transform.position, 1.0f);

        return model;
    }

    /**
     * @brief Compute the view matrix from a transform.
     */
    static glm::mat4 computeView(const Transform& transform) {
        return glm::lookAt(
            transform.position,
            transform.position + Math::computeForward(transform.rotation),
            Math::computeUp(transform.rotation)
        );
    }
};
} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Transform)
    VKM_F(position),
    VKM_F(rotation),
    VKM_F(scale)
VKM_REFLECT_END()
