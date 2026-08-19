#pragma once

#include <memory>

#include "gl_pass.h"

namespace Vkm::GL {
    class Shader;
}

namespace Vkm::Engine {

/**
 * @brief Ground-Truth Ambient Occlusion (horizon-slice integral).
 *
 * Reads the opaque depth + G-buffer (oct view-normal) the depth prepass laid
 * down and integrates cosine-weighted visibility over a few screen-space
 * slices. Writes a single AO factor into the frame's AO target; the forward
 * pass multiplies it into the indirect term.
 * Runs after the prepass and before the forward draw, so it must see the primed
 * depth + G-buffer but not yet the lit colour.
 */
class GLGTAOPass : public GLPass {
    public:
        GLGTAOPass();
        ~GLGTAOPass() override;

        GLGTAOPass(const GLGTAOPass& other) = delete;
        GLGTAOPass& operator=(const GLGTAOPass& other) = delete;

        GLGTAOPass(GLGTAOPass && other) = delete;
        GLGTAOPass& operator=(GLGTAOPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Vkm::GL::Shader> m_shader;
};

} // namespace Vkm::Engine
