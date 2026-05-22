#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {

/**
 * @brief Camera motion blur (reprojection velocity) over the resolved HDR.
 *
 * Renders into the shared post scratch then blits back into the HDR resolve
 * target. Disabled by default (EnvironmentConfig::motionBlur) - a no-op when
 * off, so the verified pipeline is unchanged until opted in.
 */
class GLMotionBlurPass : public RenderPass {
    public:
        GLMotionBlurPass() = delete;
        ~GLMotionBlurPass() override;

        GLMotionBlurPass(const GLMotionBlurPass& other) = delete;
        GLMotionBlurPass& operator=(const GLMotionBlurPass& other) = delete;

        GLMotionBlurPass(GLMotionBlurPass && other) = delete;
        GLMotionBlurPass& operator=(GLMotionBlurPass && other) = delete;

        explicit GLMotionBlurPass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::SceneHDRResolved);
            builder.read(RGResource::GBufferPosition);
            builder.write(RGResource::SceneHDRResolved);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;

        glm::mat4 m_prevViewProj = glm::mat4(1.0f);
        bool      m_havePrev = false;
};

} // namespace Engine
