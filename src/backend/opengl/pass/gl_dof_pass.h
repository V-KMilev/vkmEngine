#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

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
class GLDofPass : public RenderPass {
    public:
        GLDofPass() = delete;
        ~GLDofPass();

        GLDofPass(const GLDofPass& other) = delete;
        GLDofPass& operator=(const GLDofPass& other) = delete;

        GLDofPass(GLDofPass && other) = delete;
        GLDofPass& operator=(GLDofPass && other) = delete;

        explicit GLDofPass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::SceneHDRResolved);
            builder.read(RGResource::GBufferPosition);
            builder.write(RGResource::SceneHDRResolved);
        }

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;
};

} // namespace Engine
