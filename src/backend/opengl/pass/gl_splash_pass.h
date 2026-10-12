#pragma once

#include <memory>
#include <string>

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
 * Runs last over the whole backbuffer, so it is also the clear. The images arrive as paths,
 * so they can show before any project asset exists; each is decoded when its key changes, not
 * per frame. A no-op once the splash sequence is over.
 */
class GLSplashPass : public GLPass {
    public:
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
            float                               aspect = 1.0f;  ///< Width / height, for the fit.
        };

    private:
        /**
         * @brief Decode and upload @p key into @p image, replacing whatever it held.
         *
         * An empty key holds nothing; a file that will not decode leaves the texture null, so
         * that image is not drawn.
         *
         * @param image The logo or the caption.
         * @param key   Image path the SplashFrame named.
         */
        static void adopt(SplashImage& image, const std::string& key);

    private:
        Vkm::GL::Shader m_shader;
        SplashImage     m_logo;
        SplashImage     m_caption;
};

} // namespace Vkm::Engine
