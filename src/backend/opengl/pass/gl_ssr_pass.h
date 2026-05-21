#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {

/**
 * @brief Screen-space reflections, additively blended into the HDR scene.
 *
 * Reuses the prepass G-buffer (view normal/position) and the resolved HDR
 * color. Runs after opaque + skybox + grid and before bloom, so reflections
 * are tone-mapped together with the scene. No-ops when disabled or when the
 * G-buffer is not ready.
 */
class GLSSRPass : public RenderPass {
    public:
        GLSSRPass() = delete;
        ~GLSSRPass();

        GLSSRPass(const GLSSRPass& other) = delete;
        GLSSRPass& operator=(const GLSSRPass& other) = delete;

        GLSSRPass(GLSSRPass && other) = delete;
        GLSSRPass& operator=(GLSSRPass && other) = delete;

        explicit GLSSRPass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::SceneHDRResolved);
            builder.read(RGResource::GBufferNormal);
            builder.read(RGResource::GBufferPosition);
            builder.write(RGResource::SceneHDR);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;  ///< Shared attribute-less fullscreen triangle
};

} // namespace Engine
