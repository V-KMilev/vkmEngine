#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "vkm_opengl/shader/shader.h"

#include "decoded_image.h"
#include "pass/gl_pass.h"

namespace Vkm::OpenGL {
    class Texture2D;
}

namespace Vkm::Engine {

/**
 * @brief Draws the startup logo over black, covering everything under it, and the line of its
 *        caption, if it has one, small at the foot of the screen.
 *
 * Runs over the whole backbuffer after the frame, so it is also the clear; only the loading
 * screen draws after it. An ident's logo (SplashFrame::reveal) is not faded in: it is dark,
 * beveled metal seen only by a comet's light, which strikes it red hot (shaders/opengl/overlay/splash); its
 * caption comes in with the strike. The images arrive as paths, so they can show before any
 * project asset exists; each is made on a worker when its key changes, not per frame - decoded, and
 * the logo's shape - how far inside it each texel is, which its bevel and edge are drawn from - and
 * its glow - its coverage blurred past its edges, which the hot logo gives off - worked out with
 * it - and only uploaded here. A no-op once the splash sequence is over.
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

        /// Whether an image the splash shows is still being made on a worker; it is drawn once made.
        bool pending() const;

    private:
        /**
         * @brief What a worker makes of an image file, for the main thread to upload.
         */
        struct MadeImage {
            DecodedImage       decoded;
            std::vector<float> shape;           ///< Its distance field, one float a texel; or none.
            std::vector<float> glow;            ///< Its blurred coverage, two floats a texel; or none.
            glm::ivec2         glowSize{0};     ///< The glow's texels across and up.
            glm::vec4          glowRect{0.0f};  ///< As SplashImage::glowRect.
        };

        /**
         * @brief An image the splash draws, made once for the path it was made from.
         */
        struct SplashImage {
            std::string                             key;      ///< Its path; empty when none is held.
            std::future<MadeImage>                  made;     ///< Its making, until it is uploaded.
            std::unique_ptr<Vkm::OpenGL::Texture2D> texture;  ///< Null when none, until made, or undecodable.
            std::unique_ptr<Vkm::OpenGL::Texture2D> shape;    ///< Its distance field; the logo's alone.
            std::unique_ptr<Vkm::OpenGL::Texture2D> glow;     ///< Its blurred coverage; the logo's alone.
            glm::vec4                               glowRect{1.0f, 1.0f, 0.0f, 0.0f};
                ///< What takes its uv to its glow's: xy a scale, zw an offset.
            float aspect = 1.0f;  ///< Width / height, for the fit.
        };

    private:
        /**
         * @brief Start making @p key into @p image on a worker, replacing whatever it held.
         *
         * An empty key holds nothing. One replaced before its worker is done is dropped with what
         * the worker makes of it.
         *
         * @param image  The logo or the caption.
         * @param key    Image path the SplashFrame named.
         * @param shaped Whether to work out its shape and glow too (SplashImage::shape, ::glow).
         */
        static void adopt(SplashImage& image, const std::string& key, bool shaped);

        /**
         * @brief Upload what the worker made of @p image, once it is done.
         *
         * A file that would not decode leaves the texture null, so that image is not drawn.
         *
         * @param image The logo or the caption.
         */
        static void land(SplashImage& image);

    private:
        static constexpr uint32_t RELEASE_FRAMES = 4;  ///< Frames the images are kept once the splash is
                                                       ///< over: more than the driver has in flight.

        Vkm::OpenGL::Shader m_shader;
        SplashImage         m_logo;
        SplashImage         m_caption;
        uint32_t            m_releaseIn = 0;  ///< Frames before they are freed, once the splash is over.
};

} // namespace Vkm::Engine
