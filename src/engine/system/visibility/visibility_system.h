#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/system.h"
#include "system/visibility/visibility.h"

namespace Vkm::Engine {

struct Camera;

/**
 * @brief Builds the per-frame Visibility result (every scene mesh, and which of them the camera sees).
 *
 * Runs after the Transform stage, whose world transforms it culls. Views through
 * FrameContext::hostView when offered, else the active camera; culls every Mesh
 * in parallel (frustum -> distance -> screen size) and publishes on
 * FrameContext::visibility. Every drawable mesh is also gathered, seen or not,
 * so off-screen occluders and offline captures survive frustum culling.
 */
class VisibilitySystem : public System {
    public:
        VisibilitySystem() = default;
        ~VisibilitySystem() override = default;

        VisibilitySystem(const VisibilitySystem& other) = delete;
        VisibilitySystem& operator=(const VisibilitySystem& other) = delete;

        VisibilitySystem(VisibilitySystem && other) = delete;
        VisibilitySystem& operator=(VisibilitySystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

    private:
        /**
         * @brief Resolve the view this frame renders through into m_result.
         *
         * A host's free view, else the scene camera it names, else the active
         * camera (cached, scanned on a miss). A camera's pose is its resolved
         * world pose, so one parented to a rig renders from the rig's place.
         *
         * @param ctx            Supplies the scene and host view.
         * @param viewportAspect Taken by a camera in auto-aspect mode.
         * @return false, with m_result.hasCamera left false, when there is
         *         nothing to render through.
         */
        bool resolveCamera(const FrameContext& ctx, float viewportAspect);

        /**
         * @brief Write one camera's matrices, position and depth of field into m_result.
         *
         * @param camera         Projection and depth-of-field parameters.
         * @param position       Eye, world space.
         * @param rotation       Eye, world space.
         * @param viewportAspect Used while camera.aspect <= 0.
         * @param entity         The scene camera, or empty for a free view.
         */
        void publishCamera(
            const Camera& camera,
            const glm::vec3& position,
            const glm::quat& rotation,
            float viewportAspect,
            EntityId entity
        );

    private:
        EntityId m_cachedCameraEntity{};
        uint64_t m_cameraEpoch = 0;  ///< Scene::epoch() of the held camera.
        Visibility m_result;
        bool m_noCameraLogged = false;  ///< The no-camera warning fires once per gap.

        std::vector<uint8_t> m_state;  ///< Per object: the cull's STATE_* bits.
};

} // namespace Vkm::Engine
