#pragma once

#include <array>
#include <memory>

#include <glm/glm.hpp>

#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/texture/texture.h"

#include "pass/gl_pass.h"

namespace Vkm::Engine {

class GLUIPass;

/**
 * @brief The loading screen: its backdrop, a project's own screen over it (RenderView::cover) and its
 *        bar over that; the backdrop and the bar while it shows and no splash does, the project's
 *        screen over a splash too.
 *
 * The backdrop is water seen from above, or plain black where the project turned it off
 * (LoadingFrame::water). The water is a grid of heights and the speeds they rise at, stepped on the GPU at a
 * fixed rate (shaders/opengl/overlay/loading/water/step): its ripples run across it and die away, a slow
 * current carries it, and the pointer pushes it down along the way it moved (RenderView::pointer), leaving a
 * wake. Drawn over the screen (shaders/opengl/overlay/loading/water) as clear, dark water over a stone floor,
 * its slopes bending the floor and gathering the light on it. It starts still each time the screen comes up.
 *
 * The bar (loadingBar) is a line of water running along the part filled (shaders/opengl/overlay/loading/bar),
 * eased toward the progress (LoadingFrame::progress), coming in as the status line does
 * (LoadingFrame::content).
 */
class GLLoadingPass : public GLPass {
    public:
        /**
         * @brief Draws a project's screen through @p ui, which it borrows.
         *
         * @param ui The backend's UI pass, built before this one: one UI program draws both lists.
         */
        explicit GLLoadingPass(GLUIPass& ui);
        ~GLLoadingPass() override;

        GLLoadingPass(const GLLoadingPass& other) = delete;
        GLLoadingPass& operator=(const GLLoadingPass& other) = delete;

        GLLoadingPass(GLLoadingPass && other) = delete;
        GLLoadingPass& operator=(GLLoadingPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /// Make the grid still for a screen @p aspect wide over its height; a new size allocates it again.
        void makeWater(float aspect);

        /**
         * @brief Step the water once.
         *
         * @param seconds The step's length.
         * @param path    The pointer's way this step, from xy to zw, in cells; x below zero for none.
         * @param push    How fast the pointer moved, in cells a second.
         */
        void step(float seconds, const glm::vec4& path, float push);

        /**
         * @brief Draw the backdrop: the water, caught up by @p seconds, or black.
         *
         * @param ctx     The frame.
         * @param seconds This frame's share of the screen's clock (m_time).
         * @param fresh   Whether the screen came up this frame: the water starts still.
         */
        void drawBackdrop(GLFrameContext& ctx, float seconds, bool fresh);

        /**
         * @brief Draw the bar, its fill eased toward the progress.
         *
         * @param ctx   The frame.
         * @param fresh Whether the screen came up this frame: the fill starts at the progress.
         */
        void drawBar(GLFrameContext& ctx, bool fresh);

    private:
        GLUIPass&                  m_ui;     ///< The backend's UI pass; see the constructor.
        Vkm::OpenGL::ComputeShader m_step;   ///< shaders/opengl/overlay/loading/water/step
        Vkm::OpenGL::Shader        m_water;  ///< shaders/opengl/overlay/loading/water
        Vkm::OpenGL::Shader        m_bar;    ///< shaders/opengl/overlay/loading/bar

        std::array<std::unique_ptr<Vkm::OpenGL::Texture2D>, 2> m_grid;
            ///< The grid, two states stepped in turn: x height, y its speed.
        int        m_current = 0;     ///< Which of m_grid holds the water now.
        glm::ivec2 m_cells{0};        ///< The grid's size.
        float      m_owed    = 0.0f;  ///< Seconds the water is behind the frames, under a step.

        bool  m_shown = false;  ///< Whether the last frame drew the backdrop and the bar.
        float m_time  = 0.0f;   ///< Seconds since they came up, LONGEST_FRAME at most a frame.
        float m_fill  = 0.0f;   ///< How far the bar shows filled, easing toward the progress.

        glm::vec2 m_pointer{-1.0f};         ///< Where the pointer was over it when a step last took its path,
                                            ///< in cells; x below zero off it.
        float     m_pointerSeconds = 0.0f;  ///< Seconds since then: a frame of no step holds the path.
};

} // namespace Vkm::Engine
