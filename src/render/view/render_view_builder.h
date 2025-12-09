#pragma once

#include "render_view.h"

namespace Engine {
    class Scene;
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
         * @param width The width of the viewport.
         * @param height The height of the viewport.
         * @return The constructed RenderView.
         */
        static RenderView build(
            const Scene& scene,
            uint32_t width,
            uint32_t height
        );
};

} // namespace Engine