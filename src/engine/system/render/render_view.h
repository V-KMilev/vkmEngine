#pragma once

#include <cstdint>
#include <vector>

#include "system/render/data/camera_data.h"
#include "system/render/data/drawable_data.h"
#include "system/render/data/light_data.h"

namespace Engine {

class Scene;
struct Visibility;

/**
 * @brief The backend-agnostic snapshot handed to RenderBackend::render each frame.
 *
 * This is the whole engine -> backend contract: every backend consumes exactly
 * this struct, which is what makes them interchangeable. build() refills it from
 * the visible set the VisibilitySystem already produced, reusing the vectors'
 * capacity across frames.
 */
struct RenderView {
    uint32_t viewportX      = 0;
    uint32_t viewportY      = 0;
    uint32_t viewportWidth  = 0;
    uint32_t viewportHeight = 0;

    CameraData camera;
    std::vector<DrawableData> drawables;
    std::vector<LightData>    lights;

    public:
        void build(
            const Scene& scene,
            const Visibility& visibility
        );

    private:
        void buildCamera(const Visibility& visibility);
        void buildDrawables(const Scene& scene, const Visibility& visibility);
        void buildLights(const Scene& scene);
};

} // namespace Engine
