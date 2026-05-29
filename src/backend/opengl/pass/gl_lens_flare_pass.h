#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
    class Texture2D;
}

namespace Engine {

/**
 * @brief Screen-space lens flare (ghosts + halo + chromatic aberration).
 *
 * For any bright pixel in the resolved HDR scene, generates a chain of
 * mirror-around-center "ghost" reflections plus a soft halo at a fixed
 * radius, added onto the scene. The pass renders scene + flare into the post
 * scratch and blits back into the resolved HDR (DoF pattern - off the MSAA
 * target, so the graph resolves once). Works on any brightness source (sun,
 * emissives, bright specular, SSR returns) - occlusion is implicit because
 * hidden lights leave no bright pixel.
 *
 * Runs after SSR and before TAA so reflections cause flare and TAA can
 * temporally stabilise the result. No-op when env.lensFlare.enabled is off.
 */
class GLLensFlarePass : public RenderPass {
    public:
        GLLensFlarePass() = delete;
        ~GLLensFlarePass() override;

        GLLensFlarePass(const GLLensFlarePass& other) = delete;
        GLLensFlarePass& operator=(const GLLensFlarePass& other) = delete;

        GLLensFlarePass(GLLensFlarePass && other) = delete;
        GLLensFlarePass& operator=(GLLensFlarePass && other) = delete;

        explicit GLLensFlarePass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            // Render into PostScratch, blit back into SceneHDRResolved (DoF /
            // motion-blur pattern) so the post chain reads the result where it
            // always reads and the graph does not re-resolve.
            builder.read(RGResource::SceneHDRResolved);
            builder.write(RGResource::PostScratch);
            builder.write(RGResource::SceneHDRResolved);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;  ///< Shared attribute-less fullscreen triangle
        std::unique_ptr<Core::Texture2D> m_starburst;       ///< Procedural aperture-blade starburst (generated once)
};

} // namespace Engine
