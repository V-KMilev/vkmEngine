#pragma once

#include <memory>
#include <vector>

#include "gl_pass.h"
#include "data/gl_instance_batcher.h"

namespace Core {
    class Shader;
}

namespace Engine {
    struct DrawableData;
}

namespace Engine {

/**
 * @brief Lays down opaque depth before the forward pass for early-Z.
 *
 * Renders every non-transparent drawable depth-only into the scene target, so
 * the forward pass can run with LEQUAL + depth writes off and let the GPU
 * reject hidden fragments before the expensive PBR shader. Alpha-masked
 * materials run the same discard here so their holes match. Sets
 * GLFrameContext::depthPrimed; disable the pass to fall back to single-pass
 * forward.
 */
class GLDepthPrePass : public GLPass {
    public:
        GLDepthPrePass();
        ~GLDepthPrePass() override;

        GLDepthPrePass(const GLDepthPrePass& other) = delete;
        GLDepthPrePass& operator=(const GLDepthPrePass& other) = delete;

        GLDepthPrePass(GLDepthPrePass && other) = delete;
        GLDepthPrePass& operator=(GLDepthPrePass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Core::Shader>    m_shader;
        GLInstanceBatcher                m_batcher;
        std::vector<const DrawableData*> m_opaque;  ///< non-transparent drawables, refilled per frame
};

} // namespace Engine
