#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {

/**
 * @brief Auto-exposure metering + eye adaptation over the resolved HDR scene.
 *
 * Renders scene log-luminance into a mip pyramid (top mip = geometric-mean
 * luminance), then eases a ping-pong 1x1 adapted value toward it using the
 * frame delta time. The composite reads the adapted value to derive exposure.
 * Runs after bloom and before composite.
 */
class GLExposurePass : public RenderPass {
    public:
        GLExposurePass() = delete;
        ~GLExposurePass();

        GLExposurePass(const GLExposurePass& other) = delete;
        GLExposurePass& operator=(const GLExposurePass& other) = delete;

        GLExposurePass(GLExposurePass && other) = delete;
        GLExposurePass& operator=(GLExposurePass && other) = delete;

        GLExposurePass(ShaderHandle lumShader, ShaderHandle adaptShader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::SceneHDRResolved);
            builder.write(RGResource::AdaptedLuminance);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        ShaderHandle m_lumShader;
        ShaderHandle m_adaptShader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;  ///< Shared attribute-less fullscreen triangle
};

} // namespace Engine
