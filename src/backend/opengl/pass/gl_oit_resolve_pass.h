#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {

/**
 * @brief Composite the Weighted-Blended OIT pair into the HDR scene.
 *
 * Reads OITAccum (RGBA16F) and OITRevealage (R8), produced by the
 * transparent forward phase when env.transparency.useOIT is on, and
 * blends the resolved transparent contribution over the opaque+sky
 * scene already in SceneHDR. McGuire-Bavoil 2013 final composite:
 *     finalRGB = lerp(accum.rgb / max(accum.a, eps),
 *                     backgroundRGB,
 *                     revealage)
 *
 * No-op pass when OIT is disabled. Sits between the transparent
 * forward pass and the post chain.
 */
class GLOITResolvePass : public RenderPass {
    public:
        GLOITResolvePass() = delete;
        ~GLOITResolvePass() override;

        GLOITResolvePass(const GLOITResolvePass& other) = delete;
        GLOITResolvePass& operator=(const GLOITResolvePass& other) = delete;

        GLOITResolvePass(GLOITResolvePass && other) = delete;
        GLOITResolvePass& operator=(GLOITResolvePass && other) = delete;

        explicit GLOITResolvePass(ShaderHandle shader);

    public:
        bool enabledForView(const RenderView& view) const override;

        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::OITAccum);
            builder.read(RGResource::OITRevealage);
            builder.write(RGResource::SceneHDR);
        }

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;
};

} // namespace Engine
