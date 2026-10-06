#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/math/rotation.h"
#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Local position, rotation and scale of an entity.
 *
 * For pure quat/axis math, use core/math/ (rotation.h, axes.h).
 */
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};        ///< Local position
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};  ///< Local rotation
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};        ///< Local scale

    /**
     * @brief Compute the model matrix from transform data.
     *
     * @param transform Local TRS to compose, without intermediate matrix products.
     * @return translation * rotation * scale.
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
     * @brief Recover the TRS that computeModelMatrix() would have built @p model from.
     *
     * Exact for chains of translations, rotations and uniform scales. Shear (a
     * non-uniformly scaled joint with a rotated child) is dropped. A zero-scaled
     * axis returns zero scale, rotation from the surviving axes.
     *
     * @param model Model matrix to decompose.
     * @return Its position, rotation and scale.
     */
    static Transform fromModelMatrix(const glm::mat4& model) {
        const glm::mat3 basis(model);

        Transform out;
        out.position = glm::vec3(model[3]);
        out.scale    = {glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2])};

        // No rotation reproduces a mirror, so the flip goes on the scale axis
        // Math::worldRotationOf takes it off.
        if (glm::determinant(basis) < 0.0f) out.scale.x = -out.scale.x;

        out.rotation = Math::worldRotationOf(model);

        return out;
    }

    /**
     * @brief Compute the view matrix from a transform.
     *
     * @param transform Camera pose; looks along its forward (-Z).
     * @return World-to-view matrix.
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
    VKM_F(position)
    VKM_F(rotation)
    VKM_F(scale)
VKM_REFLECT_END()
