#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/target/frame_buffer.h"
#include "vkm_opengl/texture/texture.h"

#include "frame/draw/gl_instance_batcher.h"
#include "pass/gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Draws what water's pixels read besides the WaveBlock: the ripples (GLWaveFields), which
 * all water shows, and the shore (GLWaterShore) its waterlines foam by.
 *
 * Before the forward pass, which binds them on their units (WaterTextureSlots).
 * The ripples are drawn while water is drawn and the wind raises any (GLWaves::rippling); the shore
 * only while water with a waterline is drawn (GLFrameContext::shore), seen from a camera.
 */
class GLWaterPass : public GLPass {
    public:
        GLWaterPass();
        ~GLWaterPass() override;

        GLWaterPass(const GLWaterPass& other) = delete;
        GLWaterPass& operator=(const GLWaterPass& other) = delete;

        GLWaterPass(GLWaterPass && other) = delete;
        GLWaterPass& operator=(GLWaterPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief Capture the shore over the square of sea ahead of the frame's camera, CAPTURE_RATE
         *        times a second: it follows waves seconds long and things that float, and a capture
         *        draws the scene twice. A frame saying no time has passed captures.
         *
         * @param ctx The frame: its camera, its waves, the water it is for, its objects and their
         *            materials.
         */
        void captureShore(GLFrameContext& ctx);

        static constexpr float CAPTURE_RATE = 30.0f;  ///< Captures a second, the least whose lag a wading
                                                      ///< character's ring does not show.

        Vkm::OpenGL::Shader        m_draw;
        Vkm::OpenGL::Shader        m_drawSkinned;
        Vkm::OpenGL::ComputeShader m_water;
        Vkm::OpenGL::ComputeShader m_rows;
        Vkm::OpenGL::ComputeShader m_columns;
        Vkm::OpenGL::FrameBuffer   m_underTarget;
        Vkm::OpenGL::FrameBuffer   m_overTarget;
        GLInstanceBatcher          m_batcher;
        std::vector<uint32_t>      m_objects;  ///< What lies over the square, this frame.

        // A square of SHORE_SIZE texels but the coarse height.
        std::unique_ptr<Vkm::OpenGL::Texture2D> m_height;       ///< R16F: the water's height above rest.
        std::unique_ptr<Vkm::OpenGL::Texture2D> m_under;        ///< R16F: meters under the water, 0 inside.
        std::unique_ptr<Vkm::OpenGL::Texture2D> m_underDepth;   ///< Its depth, keeping the topmost.
        std::unique_ptr<Vkm::OpenGL::Texture2D> m_over;         ///< R16F: 0 where the lowest over faces up.
        std::unique_ptr<Vkm::OpenGL::Texture2D> m_overDepth;    ///< Its depth, keeping the lowest.
        std::unique_ptr<Vkm::OpenGL::Texture2D> m_half;         ///< R32F: transformed along X, square meters.
        float                                   m_owed = 0.0f;  ///< Captures owed since the last, by the
                                                                ///< seconds gone.
};

} // namespace Vkm::Engine
