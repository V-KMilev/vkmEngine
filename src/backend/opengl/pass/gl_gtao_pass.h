#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {

/**
 * @brief Screen-space ambient occlusion (GTAO-style) from the prepass.
 *
 * Fullscreen pass: reads the view-space normal/position G-buffer, writes a
 * single-channel AO factor the forward PBR pass multiplies into the ambient
 * term. Runs after the prepass and before the forward pass.
 */
class GLGTAOPass : public RenderPass {
    public:
        GLGTAOPass() = delete;
        ~GLGTAOPass() override;

        GLGTAOPass(const GLGTAOPass& other) = delete;
        GLGTAOPass& operator=(const GLGTAOPass& other) = delete;

        GLGTAOPass(GLGTAOPass && other) = delete;
        GLGTAOPass& operator=(GLGTAOPass && other) = delete;

        explicit GLGTAOPass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        /// AO is the only consumer-gated geometry effect with no explicit
        /// toggle of its own: it runs whenever the environment's AO is on.
        /// `ao.enabled` is pre-patched to false for diagnostic/wireframe
        /// frames in RenderSystem::buildView, so no disablePost check here
        /// (the AOOnly view mode keeps ao.enabled true on purpose).
        bool enabledForView(const RenderView& view) const override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::GBufferNormal);
            builder.read(RGResource::GBufferPosition);
            builder.write(RGResource::AO);
        }

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;  ///< Shared attribute-less fullscreen triangle
};

} // namespace Engine
