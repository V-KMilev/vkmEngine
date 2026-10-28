#pragma once

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::GL {
    class Texture2D;
}

namespace Vkm::Engine {

/**
 * @brief Draws the startup logo over black, covering everything under it, and the line of its
 *        caption, if it has one, small at the foot of the screen.
 *
 * Runs over the whole backbuffer after the frame, so it is also the clear; only the loading
 * screen draws after it. An ident's logo (SplashFrame::reveal) is not faded in: it is dark,
 * bevelled metal seen only by a comet's light, which strikes it red hot (shaders/splash); its
 * caption comes in with the strike. The images arrive as paths, so they can show before any
 * project asset exists; each is decoded when its key changes, not per frame, and the logo's shape
 * - how far inside it each texel is, which its bevel and edge are drawn from - and its glow - its
 * coverage blurred past its edges, which the hot logo gives off - worked out with it.
 * A no-op once the splash sequence is over.
 */
class GLSplashPass : public GLPass {
    public:
        /// Compile the program.
        GLSplashPass();
        ~GLSplashPass() override;

        GLSplashPass(const GLSplashPass& other) = delete;
        GLSplashPass& operator=(const GLSplashPass& other) = delete;

        GLSplashPass(GLSplashPass && other) = delete;
        GLSplashPass& operator=(GLSplashPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /// An image the splash draws, decoded once for the path it was decoded from.
        struct SplashImage {
            std::string                         key;            ///< Its path; empty when none is held.
            std::unique_ptr<Vkm::GL::Texture2D> texture;        ///< Null when none, or it did not decode.
            std::unique_ptr<Vkm::GL::Texture2D> shape;          ///< Its distance field; the logo's alone.
            std::unique_ptr<Vkm::GL::Texture2D> glow;           ///< Its blurred coverage; the logo's alone.
            /// What takes its uv to its glow's: xy a scale, zw an offset.
            glm::vec4                           glowRect{1.0f, 1.0f, 0.0f, 0.0f};
            float                               aspect = 1.0f;  ///< Width / height, for the fit.
        };

    private:
        /**
         * @brief Decode and upload @p key into @p image, replacing whatever it held.
         *
         * An empty key holds nothing; a file that will not decode leaves the texture null, so
         * that image is not drawn.
         *
         * @param image  The logo or the caption.
         * @param key    Image path the SplashFrame named.
         * @param shaped Whether to work out its shape and glow too (SplashImage::shape, ::glow).
         */
        static void adopt(SplashImage& image, const std::string& key, bool shaped);

    private:
        Vkm::GL::Shader m_shader;
        SplashImage     m_logo;
        SplashImage     m_caption;
};

} // namespace Vkm::Engine
