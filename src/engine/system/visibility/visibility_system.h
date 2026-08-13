#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "core/memory/types.h"
#include "system/visibility/visibility.h"

namespace Engine {

/**
 * @brief Builds the per-frame Visibility result (visible entities + shadow casters).
 *
 * Runs in the Visibility stage. Finds the active camera (cached for O(1)
 * re-lookup, falling back to a scene scan), then culls every Mesh in parallel
 * through frustum -> distance -> screen-size tests, transforming each mesh's
 * local AABB to world space via WorldTransform when present (else Transform).
 * The result is published on FrameContext::visibility for the render side
 * (RenderSystem, editor picking). Shadow casters are gathered separately so off-screen
 * occluders survive frustum culling.
 */
class VisibilitySystem : public System {
    public:
        /**
         * @brief Tunable cull thresholds for the visibility pass.
         *
         * Mirrored into the VisibilityContext at the start of each frame, where
         * the raw thresholds are pre-squared for the sqrt-free distance and
         * screen-size tests.
         */
        struct Settings {
            float minPixels   = 3.0f;    ///< Screen-pixel cull threshold.
            float maxDistance = 500.0f;  ///< World-space cull distance.
        };

        VisibilitySystem() = default;
        ~VisibilitySystem() override = default;

        VisibilitySystem(const VisibilitySystem& other) = delete;
        VisibilitySystem& operator=(const VisibilitySystem& other) = delete;

        VisibilitySystem(VisibilitySystem && other) = delete;
        VisibilitySystem& operator=(VisibilitySystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

        Settings&       getSettings()       { return m_settings; }
        const Settings& getSettings() const { return m_settings; }
        void setSettings(const Settings& s) { m_settings = s; }

    private:
        /**
         * @brief Resolve the active camera into m_result (view / projection /
         * cameraPosition / hasCamera) and refresh the camera-entity cache.
         *
         * Tries the cached entity first (O(1)); on a miss, scans for the first
         * active camera. Returns false (m_result.hasCamera left false) when none
         * is found.
         */
        bool resolveActiveCamera(Scene& scene, float viewportAspect);

    private:
        Settings m_settings;

        EntityId m_cachedCameraEntity{};
        Visibility m_result;

        std::vector<uint8_t>   m_visibleFlags;
        std::vector<uint8_t>   m_casterFlags;
        std::vector<glm::mat4>  m_modelMatrices;
        std::vector<MeshHandle> m_meshes;      ///< Geometry chosen per index (LOD level, or the Mesh component's own).
        std::vector<glm::vec3> m_worldMins;
        std::vector<glm::vec3> m_worldMaxs;
};

} // namespace Engine
