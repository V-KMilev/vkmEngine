#pragma once

#include <memory>
#include <string>

#include "gl_pass.h"

namespace Vkm::GL {
    class Shader;
    class Texture2D;
}

namespace Vkm::Engine {

/**
 * @brief Draws the startup logo over black, covering everything under it.
 *
 * Runs last, after UI, and covers the whole backbuffer rather than the viewport
 * rect, so it is also the clear: black everywhere the mark is not. The image
 * arrives as a path rather than a texture handle, so it can show before any of
 * a project's assets exist; the pass decodes and uploads the first time it sees
 * a new key and holds it until the key changes, so the file is read once per
 * logo rather than once per frame. A no-op once the splash sequence is over.
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
        /**
         * @brief Decode and upload @p key, replacing whatever was held.
         *
         * A file that will not decode leaves the texture null, so that entry's
         * turn shows black rather than dropping the whole sequence.
         *
         * @param key Image path the SplashFrame named.
         */
        void adopt(const std::string& key);

    private:
        std::unique_ptr<Vkm::GL::Shader>      m_shader;
        std::unique_ptr<Vkm::GL::Texture2D>   m_logo;

        std::string m_key;           ///< Image m_logo holds; empty when it holds nothing.
        float       m_aspect = 1.0f; ///< m_logo's width / height, for the fit.
};

} // namespace Vkm::Engine
