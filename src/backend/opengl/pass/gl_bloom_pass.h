#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {

/**
 * @brief Energy-conserving bloom over the resolved HDR scene (COD/Jimenez).
 *
 * Resolves the MSAA HDR target, progressively downsamples it into the bloom
 * mip chain (Karis-averaged first tap to tame fireflies), then additively
 * upsamples with a 3x3 tent. Mip 0 is left holding the final bloom, which
 * the composite pass blends in before exposure + AgX. Runs after the scene
 * passes and before composite.
 */
class GLBloomPass : public RenderPass {
    public:
        GLBloomPass() = delete;
        ~GLBloomPass() override;

        GLBloomPass(const GLBloomPass& other) = delete;
        GLBloomPass& operator=(const GLBloomPass& other) = delete;

        GLBloomPass(GLBloomPass && other) = delete;
        GLBloomPass& operator=(GLBloomPass && other) = delete;

        GLBloomPass(ShaderHandle downsampleShader, ShaderHandle upsampleShader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::SceneHDRResolved);
            builder.write(RGResource::BloomChain);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        ShaderHandle m_downShader;
        ShaderHandle m_upShader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;  ///< Shared attribute-less fullscreen triangle
};

} // namespace Engine
