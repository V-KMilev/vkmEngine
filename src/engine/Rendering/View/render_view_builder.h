#pragma once

#include "render_view.h"

namespace Engine {
    class Scene;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Builder class for creating RenderView instances from a Scene.
 *
 * Provides a static method to construct a RenderView from a given Scene,
 * along with viewport dimensions for perspective projection calculations.
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
         * @brief Build a RenderView from a given Scene, along with view space dimensions for perspective projection calculations.
         * @param scene The Scene to build the RenderView from.
         * @param resources Reference to ResourceManager for accessing mesh bounds for frustum culling.
         * @return The constructed RenderView.
         */
        static RenderView build(const Scene& scene, const ResourceManager& resources);
};

} // namespace Engine