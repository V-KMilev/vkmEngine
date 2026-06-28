#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "gl_pass.h"
#include "gl_screen_triangle.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Camera motion blur over the resolved HDR scene.
 *
 * Reconstructs each pixel's world position from depth, reprojects it through the
 * previous frame's view-projection to get a screen-space velocity, and blurs the
 * HDR colour along it. Camera-only: there is no per-object velocity buffer, so
 * moving geometry under a still camera is not blurred. Runs after SSR and before
 * bloom; reads the HDR target and writes the scratch target, blitting back so it
 * never samples a bound attachment.
 */
class GLMotionBlurPass : public GLPass {
    public:
        GLMotionBlurPass();
        ~GLMotionBlurPass() override;

        GLMotionBlurPass(const GLMotionBlurPass& other) = delete;
        GLMotionBlurPass& operator=(const GLMotionBlurPass& other) = delete;

        GLMotionBlurPass(GLMotionBlurPass && other) = delete;
        GLMotionBlurPass& operator=(GLMotionBlurPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Core::Shader> m_shader;
        Core::ScreenTriangle          m_tri;

        glm::mat4 m_prevViewProj{1.0f};  ///< Last frame's view-projection (seeds velocity).
        bool      m_havePrev = false;    ///< First frame has no previous: skip the blur.
};

} // namespace Engine
