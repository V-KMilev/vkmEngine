#pragma once

#include <cstdint>

#include "component.h"

namespace Engine {

/**
 * @brief Enumeration of camera projection types.
 * Perspective: Projective, depth perspective (for 3D rendering).
 * Orthographic: Parallel projection (no perspective).
 */
enum class ProjectionType {
    Perspective  = 0,   ///< Perspective projection
    Orthographic = 1    ///< Orthographic (parallel) projection
};

/**
 * @brief Component representing a camera, containing projection and view parameters.
 *
 * Supports both perspective and orthographic projection. Provides fields and setters for
 * common parameters such as near/far planes, aspect ratio, field of view, orthographic height,
 * and exposure.
 */
class Camera final : public Component {
    public:
        Camera() = delete;
        ~Camera() override = default;

        /**
         * @brief Construct a Camera component.
         * @param id Unique component identifier.
         * @param projection Type of projection (default: Perspective).
         */
        Camera(
            uint32_t id,
            ProjectionType projection = ProjectionType::Perspective
        );

    public:
        /**
         * @brief Get the current projection type.
         * @return ProjectionType value.
         */
        ProjectionType getProjectionType() const { return m_projection; }

        /**
         * @brief Set the projection type.
         * @param type New projection type.
         */
        void setProjectionType(ProjectionType type) { m_projection = type; }

        /**
         * @brief Get the vertical field of view (radians; perspective only).
         * @return Vertical FOV in radians.
         */
        float getFovY() const { return m_fovY; }

        /**
         * @brief Get the orthographic half-height (world units; ortho only).
         * @return Height value.
         */
        float getOrthoHeight() const { return m_orthoHeight; }

        /**
         * @brief Get the near plane distance.
         * @return Near clip plane distance.
         */
        float getNearPlane() const { return m_near; }

        /**
         * @brief Get the far plane distance.
         * @return Far clip plane distance.
         */
        float getFarPlane() const { return m_far; }

        /**
         * @brief Get the aspect ratio (width / height).
         * @return Aspect ratio.
         */
        float getAspect() const { return m_aspect; }

        /**
         * @brief Get the camera exposure (linear multiplier for final output).
         * @return Exposure value.
         */
        float getExposure() const { return m_exposure; }

        /**
         * @brief Get whether the camera is currently active.
         * @return True if camera is active.
         */
        bool isActive() const { return m_active; }

        /**
         * @brief Set the vertical field of view (radians; perspective only).
         * @param fovY New vertical FOV in radians.
         */
        void setFovY(float fovY) { m_fovY = fovY; }

        /**
         * @brief Set the near plane distance.
         * @param n New near plane value.
         */
        void setNearPlane(float n) { m_near = n; }

        /**
         * @brief Set the far plane distance.
         * @param f New far plane value.
         */
        void setFarPlane(float f) { m_far = f; }

        /**
         * @brief Set the aspect ratio (width / height).
         * @param a New aspect ratio.
         */
        void setAspect(float a) { m_aspect = a; }

        /**
         * @brief Set the orthographic half-height (world units; ortho only).
         * @param h New half-height.
         */
        void setOrthoHeight(float h) { m_orthoHeight = h; }

        /**
         * @brief Set the camera active flag.
         * @param active True to enable, false to disable as main camera.
         */
        void setActive(bool active) { m_active = active; }

        /**
         * @brief Set the camera exposure.
         * @param exposure Linear scalar for output intensity.
         */
        void setExposure(float exposure) { m_exposure = exposure; }

    private:
        ProjectionType m_projection;

        float m_fovY;
        float m_aspect;
        float m_near;
        float m_far;

        float m_orthoHeight;

        bool  m_active;
        float m_exposure;
};

} // namespace Engine
