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
constexpr glm::vec3 WORLD_AXIS_X_RIGHT   = {1.0f, 0.0f, 0.0f};
constexpr glm::vec3 WORLD_AXIS_Y_UP      = {0.0f, 1.0f, 0.0f};
constexpr glm::vec3 WORLD_AXIS_Z_FORWARD = {0.0f, 0.0f, 1.0f};

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

    /**
     * @brief Compute the model matrix from transform data.
     * @param transform The transform component.
     * @return The computed model matrix.
     */
    static glm::mat4 computeModelMatrix(const Transform& transform) {
        glm::mat4 model = glm::mat4(1.0f);

        model  = glm::translate(model, transform.position);
        model *= glm::mat4_cast(transform.rotation);
        model  = glm::scale(model, transform.scale);

        return model;
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
};

} // namespace Engine
