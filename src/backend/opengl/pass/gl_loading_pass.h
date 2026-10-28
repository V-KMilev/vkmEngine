#pragma once

#include <array>
#include <memory>

#include <glm/glm.hpp>

#include "gl_compute_shader.h"
#include "gl_shader.h"
#include "gl_texture.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief The loading screen's backdrop: water seen from above, under a project's own screen and the
 *        loading screen's bar and line, while it shows and no splash does; plain black where the
 *        project turned it off (RenderView::loadingWater).
 *
 * The water is a grid of heights and the speeds they rise at, stepped on the GPU at a fixed rate
 * (shaders/loading/water_step): its ripples run across it and die away, a slow current carries it,
 * and the pointer pushes it down along the way it moved (RenderView::pointer), leaving a wake. Drawn
 * over the screen (shaders/loading/water) as clear, dark water over a stone floor, its slopes bending
 * the floor and gathering the light on it. It starts still each time the screen comes up.
 */
class GLLoadingPass : public GLPass {
    public:
        GLLoadingPass();
        ~GLLoadingPass() override;

        GLLoadingPass(const GLLoadingPass& other) = delete;
        GLLoadingPass& operator=(const GLLoadingPass& other) = delete;

        GLLoadingPass(GLLoadingPass && other) = delete;
        GLLoadingPass& operator=(GLLoadingPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /// Make the grid for a screen @p aspect wide over its height, still.
        void makeWater(float aspect);

        /**
         * @brief Step the water once.
         *
         * @param seconds The step's length.
         * @param path    The pointer's way this step, from xy to zw, in cells; x below zero for none.
         * @param push    How fast the pointer moved, in cells a second.
         */
        void step(float seconds, const glm::vec4& path, float push);

    private:
        Vkm::GL::ComputeShader m_step;   ///< shaders/loading/water_step
        Vkm::GL::Shader        m_water;  ///< shaders/loading/water

        /// The grid, two states stepped in turn: x height, y its speed.
        std::array<std::unique_ptr<Vkm::GL::Texture2D>, 2> m_grid;
        int        m_current = 0;     ///< Which of m_grid holds the water now.
        glm::ivec2 m_cells{0};        ///< The grid's size.
        float      m_owed    = 0.0f;  ///< Seconds the water is behind the frames, under a step.

        bool  m_shown = false;  ///< Whether the last frame drew it.
        float m_time  = 0.0f;   ///< Seconds since it came up.

        /// Where the pointer was over it when a step last took its path, in cells; x below zero off it.
        glm::vec2 m_pointer{-1.0f};
        float     m_pointerSeconds = 0.0f;  ///< Seconds since then: a frame of no step holds the path.
};

} // namespace Vkm::Engine
