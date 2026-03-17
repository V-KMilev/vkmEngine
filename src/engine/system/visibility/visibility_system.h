#pragma once

#include <unordered_map>

#include <glm/glm.hpp>

#include "core/system.h"
#include "core/memory/types.h"

namespace Engine {

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

        void updateSingleThreaded(FrameContext& ctx);
        void updateMultiThreaded(FrameContext& ctx);

        void setMinPixels(float minPixels) { m_minPixels = minPixels; }
        void setMaxDistance(float maxDistance) { m_maxDistance = maxDistance; }

        float getMinPixels() const { return m_minPixels; }
        float getMaxDistance() const { return m_maxDistance; }

    private:
        float m_minPixels   = 3.0f;
        float m_maxDistance  = 500.0f;

        EntityId m_cachedCameraEntity{};
        Visibility m_result;  ///< Persistent buffer - vectors reuse capacity across frames.

        /// Pre-computed world matrices for parented entities (cleared each frame).
        std::unordered_map<uint32_t, glm::mat4> m_worldMatrixCache;

        /// Persistent buffers for multithreaded path - reuse capacity across frames.
        std::vector<uint8_t>   m_visibleFlags;
        std::vector<glm::mat4> m_modelMatrices;
};

} // namespace Engine
