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
     * An axis scaled to nothing is answered rather than refused: the scale comes
     * back as zero and the rotation from the axes that survived.
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

        for (int axis = 0; axis < 3; ++axis)
            basis[axis] = (out.scale[axis] != 0.0f) ? basis[axis] / out.scale[axis]
                                                    : glm::vec3(0.0f);

        // An axis scaled to nothing keeps no direction to recover a rotation
        // from, and dividing by that zero is a NaN quaternion that spreads into
        // every matrix built from the result - a clip hiding a joint by keying
        // its scale to zero is enough to reach it. The two axes that survived
        // still carry the rotation, so the lost one is the axis they imply; with
        // two of them gone there is none left to recover and the identity's
        // column stands in.
        for (int axis = 0; axis < 3; ++axis) {
            if (glm::dot(basis[axis], basis[axis]) > 0.0f) continue;

            const glm::vec3 implied = glm::cross(basis[(axis + 1) % 3], basis[(axis + 2) % 3]);
            basis[axis] = glm::dot(implied, implied) > 0.0f ? implied : glm::mat3(1.0f)[axis];
        }

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
