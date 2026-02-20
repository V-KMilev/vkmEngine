#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

// TODO: Move this to a config file or something
/**
 * @brief Standard 3D basis direction vectors in world space.
 * 
 * WORLD_AXIS_X_RIGHT   (+X): Points in the positive X direction (to the right).
 * WORLD_AXIS_Y_UP      (+Y): Points in the positive Y direction (upwards).
 * WORLD_AXIS_Z_FORWARD (+Z): Points in the positive Z direction (out of the screen in right-handed systems).
 */
inline const glm::vec3 WORLD_AXIS_X_RIGHT   = {1.0f, 0.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Y_UP      = {0.0f, 1.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Z_FORWARD = {0.0f, 0.0f, 1.0f};

/**
 * @brief Component representing spatial transformation (position, rotation, scale) in 3D space.
 *
 * Simple data-only component. Transform calculations (model matrix, direction vectors) should be
 * handled by systems that process this component, not by the component itself.
 */
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};        ///< Local position in world space
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};  ///< Local rotation as quaternion (identity = no rotation)
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};        ///< Local scale

    mutable glm::mat4 cachedModelMatrix = glm::mat4(1.0f); ///< Cached model matrix (recomputed when dirty)
    mutable bool dirty = true;                               ///< True when position/rotation/scale changed

    /**
     * @brief Compute the model matrix from transform data.
     *
     * Uses fused TRS construction: builds translation, rotation, scale
     * directly without intermediate matrix multiplications.
     *
     * @param transform The transform component.
     * @return The computed model matrix.
     */
    static glm::mat4 computeModelMatrix(const Transform& transform) {
        // Convert quaternion to rotation matrix
        const glm::mat4 rot = glm::mat4_cast(transform.rotation);

        // Build TRS matrix directly: scale rotation columns, set translation
        glm::mat4 model;
        model[0] = rot[0] * transform.scale.x;
        model[1] = rot[1] * transform.scale.y;
        model[2] = rot[2] * transform.scale.z;
        model[3] = glm::vec4(transform.position, 1.0f);

        return model;
    }

    /**
     * @brief Get the cached model matrix, recomputing only if dirty.
     * @param transform The transform component.
     * @return Const reference to the cached model matrix.
     */
    static const glm::mat4& getModelMatrix(const Transform& transform) {
        if (transform.dirty) {
            transform.cachedModelMatrix = computeModelMatrix(transform);
            transform.dirty = false;
        }
        return transform.cachedModelMatrix;
    }

    /**
     * @brief Compute the forward direction vector from a rotation quaternion.
     * @param rotation The rotation quaternion.
     * @return Normalized forward vector (Z+ direction).
     */
    static glm::vec3 computeForward(const glm::quat& rotation) {
        return glm::normalize(rotation * WORLD_AXIS_Z_FORWARD);
    }

    /**
     * @brief Compute the up direction vector from a rotation quaternion.
     * @param rotation The rotation quaternion.
     * @return Normalized up vector (Y+ direction).
     */
    static glm::vec3 computeUp(const glm::quat& rotation) {
        return glm::normalize(rotation * WORLD_AXIS_Y_UP);
    }

    /**
     * @brief Compute the right direction vector from a rotation quaternion.
     * @param rotation The rotation quaternion.
     * @return Normalized right vector (X+ direction).
     */
    static glm::vec3 computeRight(const glm::quat& rotation) {
        return glm::normalize(rotation * WORLD_AXIS_X_RIGHT);
    }

    /**
     * @brief Compute the view matrix from a transform.
     * @param transform The transform.
     * @return The computed view matrix.
     */
    static glm::mat4 computeView(const Transform& transform) {
        return glm::lookAt(
            transform.position,
            transform.position + computeForward(transform.rotation),
            computeUp(transform.rotation)
        );
    }
};

} // namespace Engine
