#pragma once

#include "render_view.h"
#include "entity.h"

class ThreadPool;

namespace Engine {
    class Scene;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Screen-size culling: skip objects smaller than ~2 pixels at 1080p
 */
constexpr float MIN_SCREEN_SIZE_SQ = 0.002f * 0.002f;

/**
 * @brief Builder class for creating RenderView instances from a Scene.
 *
 * Provides static methods to construct a RenderView from a given Scene,
 * using pre-computed visible entity IDs from SceneView.
 */
class RenderViewBuilder {
    public:
        RenderViewBuilder() = delete;
        ~RenderViewBuilder() = delete;

        RenderViewBuilder(const RenderViewBuilder& other) = delete;
        RenderViewBuilder& operator=(const RenderViewBuilder& other) = delete;

        RenderViewBuilder(RenderViewBuilder && other) = delete;
        RenderViewBuilder& operator=(RenderViewBuilder && other) = delete;

    public:
        static RenderView build(
            const Scene& scene,
            const ResourceManager& resources,
            const std::vector<EntityId>& visibleIds
        );

};

} // namespace Engine