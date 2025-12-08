#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @struct TransformProperties
 * @brief Encapsulates basic transformation attributes for a transformable object.
 *
 * Members:
 *   - position: The position (translation) of the object in world space.
 *   - rotation: The Euler rotation (in radians) applied to the object.
 *   - scale:    The scaling factors applied to the object in each axis.
 */
struct TransformProperties {
    glm::vec3 position {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation {0.0f, 0.0f, 0.0f};
    glm::vec3 scale    {1.0f, 1.0f, 1.0f};
};

/**
 * @class CPUTransform
 * @brief Manages transformation (position, rotation, scale) on the CPU side and computes the model matrix.
 *
 * The CPUTransform class stores transformation properties, computes the model matrix, and
 * provides both mutators and accessors for efficiently manipulating and accessing transform state.
 */
class CPUTransform {
    public:
        CPUTransform() = default;
        ~CPUTransform() = default;

        CPUTransform(const CPUTransform& other) = delete;
        CPUTransform& operator=(const CPUTransform& other) = delete;

        CPUTransform(CPUTransform && other) = delete;
        CPUTransform& operator=(CPUTransform && other) = delete;

        explicit CPUTransform(const TransformProperties& properties);

    public:
        /**
         * @brief Sets the position of the transform.
         * @param position The new position vector.
         */
        void setPosition(const glm::vec3& position);

        /**
         * @brief Sets the rotation of the transform (in Euler angles, radians).
         * @param rotation The new rotation vector.
         */
        void setRotation(const glm::vec3& rotation);

        /**
         * @brief Sets the scale of the transform.
         * @param scale The new scale vector.
         */
        void setScale(const glm::vec3& scale);

        /**
         * @brief Gets the position vector.
         * @return Reference to the current position vector.
         */
        const glm::vec3& getPosition() const { return m_properties.position; }

        /**
         * @brief Gets the rotation vector (Euler angles, radians).
         * @return Reference to the current rotation vector.
         */
        const glm::vec3& getRotation() const { return m_properties.rotation; }

        /**
         * @brief Gets the scale vector.
         * @return Reference to the current scale vector.
         */
        const glm::vec3& getScale() const { return m_properties.scale; }

        /**
         * @brief Gets the 4x4 model matrix representing this transform.
         * @return Reference to the current model matrix.
         */
        const glm::mat4& getModelMatrix() const { return m_modelMatrix; }

    public:
        /**
         * @brief Applies a translation transform by offsetting the current position.
         * @param translation Translation vector to apply.
         */
        void translate(const glm::vec3& translation);

        /**
         * @brief Applies an additional rotation to the current rotation (in radians).
         * @param rotation Euler angles to add to the current rotation.
         */
        void rotate(const glm::vec3& rotation);

        /**
         * @brief Multiplies the current scale by the given scale vector.
         * @param scale Scale multipliers to apply.
         */
        void scale(const glm::vec3& scale);

    public:
        /**
         * @brief Returns the forward direction vector based on current rotation.
         * @return The normalized forward vector (usually +Z).
         */
        glm::vec3 getForward() const;

        /**
         * @brief Returns the right direction vector based on current rotation.
         * @return The normalized right vector (usually +X).
         */
        glm::vec3 getRight() const;

        /**
         * @brief Returns the up direction vector based on current rotation.
         * @return The normalized up vector (usually +Y).
         */
        glm::vec3 getUp() const;

    private:
        /**
         * @brief Updates the cached model matrix according to the current transformation properties.
         * This is called automatically after property changes.
         */
        void updateModelMatrix() const;

    private:
        TransformProperties m_properties;

        mutable glm::mat4 m_modelMatrix;
};

} // namespace Engine