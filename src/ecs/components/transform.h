#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "component.h"

namespace Engine {

/**
 * @brief Component representing spatial transformation (position, rotation, scale) in 3D space.
 *
 * Provides position, rotation (quaternion), scale, and easy access to the transform matrix and direction vectors.
 * Includes helper functions for translation, rotation (in local or world space), and scaling.
 */
class Transform final : public Component {
    public:
        Transform() = delete;
        ~Transform() override = default;

        /**
        * @brief Construct a Transform component.
        * @param id        Unique component identifier.
        * @param position  Local position in world space (default: origin).
        * @param rotation  Local rotation as quaternion (default: identity/no rotation).
        * @param scale     Local scale (default: 1.0, 1.0, 1.0).
        */
        Transform(
            uint32_t id,
            const glm::vec3& position = {0.0f, 0.0f, 0.0f},
            const glm::quat& rotation = {1.0f, 0.0f, 0.0f, 0.0f},
            const glm::vec3& scale    = {1.0f, 1.0f, 1.0f}
        );

    public:
        /**
        * @brief Get the current position.
        * @return const reference to glm::vec3 position.
        */
        const glm::vec3& getPosition() const { return m_position; }

        /**
        * @brief Get the current rotation as a quaternion.
        * @return const reference to glm::quat rotation.
        */
        const glm::quat& getRotation() const { return m_rotation; }

        /**
        * @brief Get the current scale.
        * @return const reference to glm::vec3 scale.
        */
        const glm::vec3& getScale()    const { return m_scale; }

        /**
        * @brief Set the position.
        * @param position New position vector.
        */
        void setPosition(const glm::vec3& position);

        /**
        * @brief Set the rotation (quaternion).
        * @param rotation New rotation quaternion.
        */
        void setRotation(const glm::quat& rotation);

        /**
        * @brief Set the scale.
        * @param scale New scale vector.
        */
        void setScale(const glm::vec3& scale);

        /**
        * @brief Get the "forward" (Z+) direction in world space.
        * @return Normalized forward vector.
        */
        glm::vec3 getForward() const;

        /**
        * @brief Get the "right" (X+) direction in world space.
        * @return Normalized right vector.
        */
        glm::vec3 getRight() const;

        /**
        * @brief Get the "up" (Y+) direction in world space.
        * @return Normalized up vector.
        */
        glm::vec3 getUp() const;

        /**
        * @brief Get the resulting model matrix for this transform.
        * @return const reference to glm::mat4 model matrix.
        *
        * Lazily updates only if transform is dirty.
        */
        const glm::mat4& getModelMatrix() const;

    private:
        /**
        * @brief Recompute the model matrix using position, rotation, and scale.
        * Called internally when transform is dirty.
        */
        void updateModelMatrix() const;

    private:
        glm::vec3 m_position;
        glm::quat m_rotation;
        glm::vec3 m_scale;

        mutable glm::mat4 m_modelMatrix;
        mutable bool m_dirty;
};

} // namespace Engine