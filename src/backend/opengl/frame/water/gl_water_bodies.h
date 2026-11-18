#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/landscape/water.h"

namespace Vkm::OpenGL {
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

/**
 * @brief std430 layout - must match WaterBody in shaders/opengl/include/waves.glsl.
 */
struct WaterBodyGPU {
    glm::vec4 shares;  ///< x = of the waves, y = of the ripples, z = flow along +U in m/s, w = foam.
    glm::vec4 shore;   ///< x = meters of waterline foam; yzw unused.
};
static_assert(sizeof(WaterBodyGPU) == 32, "WaterBody in shaders/opengl/include/waves.glsl is two vec4s");

/**
 * @brief Each Water's values as the water programs read them (SSBO_WATER_BODIES), by its place in
 *        its storage (RenderObjects::waters, ObjectDraw::water).
 *
 * Uploaded only when what is packed changes.
 */
class GLWaterBodies {
    public:
        GLWaterBodies();
        ~GLWaterBodies();

        GLWaterBodies(const GLWaterBodies& other) = delete;
        GLWaterBodies& operator=(const GLWaterBodies& other) = delete;

        GLWaterBodies(GLWaterBodies && other) = delete;
        GLWaterBodies& operator=(GLWaterBodies && other) = delete;

    public:
        /**
         * @brief Upload @p waters if they differ from the last, and bind them.
         *
         * @param waters Every Water, in its storage's order.
         */
        void update(const std::vector<Water>& waters);

    private:
        std::unique_ptr<Vkm::OpenGL::ShaderStorageBuffer> m_buffer;
        std::vector<WaterBodyGPU>                         m_held;     ///< What m_buffer holds.
        std::vector<WaterBodyGPU>                         m_scratch;  ///< This frame's, packed to compare.
};

} // namespace Vkm::Engine
