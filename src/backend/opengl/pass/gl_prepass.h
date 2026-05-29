#pragma once

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Engine {

/**
 * @brief Depth/normal prepass - fills the thin view-space G-buffer.
 *
 * Renders opaque instanced batches into GLGBuffer's normal + position MRT so
 * the GTAO pass has view-space data to work from. Runs after shadows and
 * before the forward pass.
 */
class GLPrepass : public RenderPass {
    public:
        GLPrepass() = delete;
        ~GLPrepass() override = default;

        GLPrepass(const GLPrepass& other) = delete;
        GLPrepass& operator=(const GLPrepass& other) = delete;

        GLPrepass(GLPrepass && other) = delete;
        GLPrepass& operator=(GLPrepass && other) = delete;

        explicit GLPrepass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        /// The prepass only exists to fill the view-space G-buffer for
        /// downstream consumers (GTAO/SSR/TAA/DoF/MotionBlur/HiZ). Skip the
        /// full opaque re-draw entirely when none of them will run this frame.
        bool enabledForView(const RenderView& view) const override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.write(RGResource::GBufferNormal);
            builder.write(RGResource::GBufferPosition);
        }

    private:
        ShaderHandle m_shader;
};

} // namespace Engine
