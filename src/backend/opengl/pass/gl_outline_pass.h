#pragma once

#include "resource/shader_asset.h"
#include "gl_render_pass.h"

namespace Engine {

/**
 * @brief Two-phase stencil-based selection outline.
 *
 * For every drawable whose Selected tag was copied onto DrawableData::
 * selected by RenderView::build, this pass first stamps the mesh into the
 * shared depth/stencil renderbuffer (no color writes), then re-renders the
 * mesh with vertices inflated outward in screen space, masked by
 * stencil != 1 so only the silhouette band survives. The result lands in
 * the SceneTarget overlay attachment so the composite pass blends it over
 * the tonemapped scene at pixel-exact intensity.
 *
 * Color and thickness come from EnvironmentConfig::SelectionOutlineConfig
 * so the editor (and gameplay code, if it ever wants the effect) can
 * retune it without recompiling.
 */
class GLOutlinePass : public GLRenderPass {
    public:
        GLOutlinePass() = delete;
        ~GLOutlinePass() override = default;

        GLOutlinePass(const GLOutlinePass& other) = delete;
        GLOutlinePass& operator=(const GLOutlinePass& other) = delete;

        GLOutlinePass(GLOutlinePass && other) = delete;
        GLOutlinePass& operator=(GLOutlinePass && other) = delete;

        explicit GLOutlinePass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;

        void executeGL(GLBackend& gl, RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.write(RGResource::Overlay);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        ShaderHandle m_shader;
};

} // namespace Engine
