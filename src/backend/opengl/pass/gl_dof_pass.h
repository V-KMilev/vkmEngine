#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "gl_render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {

/**
 * @brief Depth of field over the resolved HDR scene (CoC disc blur).
 *
 * Renders into the shared post scratch, then blits back into the HDR resolve
 * target for the downstream chain. Disabled by default
 * (EnvironmentConfig::dof) - a no-op when off, so the verified pipeline is
 * unchanged until opted in.
 */
class GLDofPass : public GLRenderPass {
    public:
        GLDofPass() = delete;
        ~GLDofPass() override;

        GLDofPass(const GLDofPass& other) = delete;
        GLDofPass& operator=(const GLDofPass& other) = delete;

        GLDofPass(GLDofPass && other) = delete;
        GLDofPass& operator=(GLDofPass && other) = delete;

        explicit GLDofPass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void executeGL(GLBackend& gl, RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            // Three-target dance: render into PostScratch, then blit back into
            // SceneHDRResolved so downstream post passes see the result in the
            // same logical resource they always read.
            builder.read(RGResource::SceneHDRResolved);
            builder.read(RGResource::GBufferPosition);
            builder.write(RGResource::PostScratch);
            builder.write(RGResource::SceneHDRResolved);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;
};

} // namespace Engine
