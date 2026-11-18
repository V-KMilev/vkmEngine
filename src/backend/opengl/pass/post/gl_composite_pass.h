#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "vkm_opengl/shader/shader.h"

#include "pass/gl_pass.h"
#include "core/render_mode.h"
#include "io/project/render_settings.h"

namespace Vkm::OpenGL {
    class Texture3D;
}

namespace Vkm::Engine {

class GLShadowAtlas;

/**
 * @brief Resolves the HDR scene target to the backbuffer.
 *
 * Tonemaps and sRGB-encodes (`linearToSrgb`, `shaders/opengl/include/color.glsl`) the HDR target into the
 * default framebuffer, grading it through RenderSettings::gradingLut when one is set. The
 * backbuffer is plain 8-bit with no sRGB conversion of its own, so the encode, the grade, the
 * dither and the blending of everything drawn later happen in display values. A scene smaller
 * than the viewport is resolved at its own size for GLUpscalePass instead
 * (GLFrameContext::upscaleSource), which dithers it in the window.
 */
class GLCompositePass : public GLPass {
    public:
        /**
         * @brief How a draw of the composite takes a scene to the display: the inputs every draw states.
         */
        struct Display {
            Tonemap    tonemap         = Tonemap::ACES;
            float      exposure        = 1.0f;             ///< What the scene is scaled by first: 2^EV.
            float      bloomStrength   = 0.0f;             ///< 0 without bloom.
            float      gradingStrength = 0.0f;             ///< The graded color's share; 0 with no table.
            RenderMode mode            = RenderMode::Lit;  ///< Lit is the final image, the rest debug views.
            bool       dither          = true;             ///< Off when the upscale dithers in the window.
        };

    public:
        GLCompositePass();
        ~GLCompositePass() override;

        GLCompositePass(const GLCompositePass& other) = delete;
        GLCompositePass& operator=(const GLCompositePass& other) = delete;

        GLCompositePass(GLCompositePass && other) = delete;
        GLCompositePass& operator=(GLCompositePass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

        /**
         * @brief Read the grading table again if its strip has been saved over since it was read.
         *
         * @return Whether it was read again.
         */
        bool reloadChangedGrading();

        /**
         * @brief Bind @p shader, a build of shaders/opengl/post/composite, and state @p display to it.
         *
         * Binds the shadow atlas's raw view too: the shader declares it as a plain sampler2D, which
         * the driver validates at draw time whether or not a debug view reads it. Its own unit, as
         * the shadow readers' unit carries a comparing sampler.
         *
         * @param shader  The composite program.
         * @param display Its display inputs.
         * @param atlas   The shadow atlas, read by its debug view alone.
         */
        static void bindDisplay(
            Vkm::OpenGL::Shader& shader,
            const Display& display,
            const GLShadowAtlas& atlas
        );

    private:
        /**
         * @brief Make the grading table from the strip at @p path, unless it was made from it last.
         *
         * An empty path makes none; so does a file that is no strip, which is reported once.
         *
         * @param path The strip, as RenderSettings::gradingLut names it.
         */
        void adoptGrading(const std::string& path);

    private:
        Vkm::OpenGL::Shader                     m_shader;
        std::string                             m_gradingPath;     ///< What m_grading was made from or tried.
        std::filesystem::file_time_type         m_gradingWritten;  ///< When that file was written, as read.
        std::unique_ptr<Vkm::OpenGL::Texture3D> m_grading;         ///< Display values in and out, or null.
};

} // namespace Vkm::Engine
