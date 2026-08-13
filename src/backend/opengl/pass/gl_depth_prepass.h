#pragma once

#include <memory>

#include "gl_pass.h"
#include "data/gl_instance_batcher.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Lays down opaque depth before the forward pass for early-Z.
 *
 * Renders every non-transparent drawable into the scene target, priming opaque
 * depth so the forward pass can run with LEQUAL + depth writes off and let the
 * GPU reject hidden fragments before the expensive PBR shader. Also writes the
 * G-buffer (view normal + roughness + metalness) into colour attachment 1 for
 * the GTAO + decal passes, and clears the HDR target's attachments for the frame.
 * Alpha-masked materials run the same discard here so their holes match. The
 * pass is unconditional: the forward pass assumes primed depth (LEQUAL, writes
 * off) and never clears.
 */
class GLDepthPrepass : public GLPass {
    public:
        GLDepthPrepass();
        ~GLDepthPrepass() override;

        GLDepthPrepass(const GLDepthPrepass& other) = delete;
        GLDepthPrepass& operator=(const GLDepthPrepass& other) = delete;

        GLDepthPrepass(GLDepthPrepass && other) = delete;
        GLDepthPrepass& operator=(GLDepthPrepass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Core::Shader> m_shader;
};

} // namespace Engine
