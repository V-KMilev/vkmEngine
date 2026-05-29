#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {

/**
 * @brief Screen-space reflections over the resolved HDR scene.
 *
 * Reuses the prepass G-buffer (view normal/position) and the resolved HDR
 * color. Renders scene + reflections into the shared post scratch, then blits
 * back into the HDR resolve target (the same single-sample pattern as DoF) so
 * the post chain stays off the 4x MSAA target and the graph resolves once.
 * Runs after opaque + skybox + grid and before bloom. No-ops when disabled or
 * when the G-buffer is not ready.
 */
class GLSSRPass : public RenderPass {
    public:
        GLSSRPass() = delete;
        ~GLSSRPass() override;

        GLSSRPass(const GLSSRPass& other) = delete;
        GLSSRPass& operator=(const GLSSRPass& other) = delete;

        GLSSRPass(GLSSRPass && other) = delete;
        GLSSRPass& operator=(GLSSRPass && other) = delete;

        explicit GLSSRPass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            // Render into PostScratch, blit back into SceneHDRResolved so the
            // downstream chain reads the result in the resource it always reads
            // and the graph does not re-resolve (DoF / motion-blur pattern).
            builder.read(RGResource::SceneHDRResolved);
            builder.read(RGResource::GBufferNormal);
            builder.read(RGResource::GBufferPosition);
            builder.write(RGResource::PostScratch);
            builder.write(RGResource::SceneHDRResolved);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;  ///< Shared attribute-less fullscreen triangle
};

} // namespace Engine
