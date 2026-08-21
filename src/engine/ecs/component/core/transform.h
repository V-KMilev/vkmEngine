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
     * @brief Recover the TRS that computeModelMatrix() would have built @p model
     *        from.
     *
     * The inverse of the function above, and the way a system that derives a
     * transform from a matrix - a bone socket reading a posed bone - hands the
     * result back to a component the hierarchy can resolve.
     *
     * Exact for any matrix that is a chain of translations, rotations and
     * uniform scales, which is what a rig composes. Shear cannot be represented
     * by a TRS at all and is dropped: it only appears when a non-uniformly
     * scaled joint carries a rotated child, the same case whose lighting the
     * skinned vertex stage already approximates.
     *
     * @param model Model matrix to decompose.
     * @return The position, rotation and scale it was composed from.
     */
    static Transform fromModelMatrix(const glm::mat4& model) {
        glm::mat3 basis(model);

        Transform out;
        out.position = glm::vec3(model[3]);
        out.scale    = {glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2])};

        // A mirrored basis has no rotation that reproduces it, so the flip is
        // carried on one scale axis - which is where computeModelMatrix would
        // have taken it from. Without this, quat_cast reads a reflection as a
        // rotation and answers with a garbage quaternion.
        if (glm::determinant(basis) < 0.0f) out.scale.x = -out.scale.x;

        basis[0] /= out.scale.x;
        basis[1] /= out.scale.y;
        basis[2] /= out.scale.z;
        out.rotation = glm::normalize(glm::quat_cast(basis));

        return out;
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
