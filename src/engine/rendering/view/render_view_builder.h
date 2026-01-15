#pragma once

#include "render_view.h"

class ThreadPool;

namespace Engine {
    class Scene;
    class ResourceManager;
    class SpatialIndex;
}

namespace Engine {

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
        /**
         * @brief Build a RenderView from pre-computed visible entity IDs.
         * @param scene The Scene to build the RenderView from.
         * @param resources Reference to ResourceManager for accessing mesh data.
         * @param visibleIds Pre-computed visible entity IDs from SceneView.
         * @return The constructed RenderView.
         */
        static RenderView build(
            const Scene& scene,
            const ResourceManager& resources,
            const std::vector<uint32_t>& visibleIds
        );

};

} // namespace Engine