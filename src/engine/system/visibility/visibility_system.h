#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "core/memory/types.h"

namespace Engine {

class VisibilitySystem : public System {
    public:
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
        Settings m_settings;

        EntityId m_cachedCameraEntity{};
        Visibility m_result;  ///< Persistent buffer - vectors reuse capacity across frames.

        /// Persistent buffers for multithreaded path - reuse capacity across frames.
        std::vector<uint8_t>   m_visibleFlags;
        std::vector<glm::mat4> m_modelMatrices;
        std::vector<glm::vec3> m_worldMins;
        std::vector<glm::vec3> m_worldMaxs;
};

} // namespace Engine
