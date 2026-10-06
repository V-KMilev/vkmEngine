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
 * @brief Draws the startup logo over black, covering everything under it.
 *
 * Runs last over the whole backbuffer, so it is also the clear. The image arrives as a path,
 * so it can show before any project asset exists; it is decoded when the key changes, not per
 * frame. A no-op once the splash sequence is over.
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
         * A file that will not decode leaves the texture null: its turn shows black.
         *
         * @param key Image path the SplashFrame named.
         */
        void adopt(const std::string& key);

    private:
        Vkm::GL::Shader                     m_shader;
        std::unique_ptr<Vkm::GL::Texture2D> m_logo;

        std::string m_key;            ///< Image m_logo holds; empty when it holds nothing.
        float       m_aspect = 1.0f;  ///< m_logo's width / height, for the fit.
};

} // namespace Vkm::Engine
