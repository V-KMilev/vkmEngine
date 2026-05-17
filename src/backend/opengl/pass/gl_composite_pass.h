#pragma once

#include <memory>
#include <string>
#include <cstdint>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
    class Texture2D;
}

namespace Engine {

/**
 * @brief Final pass: resolve the HDR scene target and tone-map to the screen.
 *
 * Resolves the MSAA HDR buffer to a single-sample texture, then draws a
 * full-screen triangle that applies camera exposure, the AgX display
 * transform, and the sRGB OETF, writing into the default framebuffer.
 *
 * This is the only place tone mapping and gamma encoding happen - every
 * object shader emits linear scene-referred radiance into the HDR target.
 */
class GLCompositePass : public RenderPass {
    public:
        GLCompositePass() = delete;
        ~GLCompositePass();

        GLCompositePass(const GLCompositePass& other) = delete;
        GLCompositePass& operator=(const GLCompositePass& other) = delete;

        GLCompositePass(GLCompositePass && other) = delete;
        GLCompositePass& operator=(GLCompositePass && other) = delete;

        /**
         * @brief Construct the composite pass.
         * @param shader Handle to the post/composite shader asset; sampler
         *               u_hdr is bound to slot 0 via the asset's bindings.
         */
        explicit GLCompositePass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::SceneHDRResolved);
            builder.read(RGResource::BloomChain);
            builder.read(RGResource::AdaptedLuminance);
            builder.write(RGResource::Backbuffer);
        }

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;  ///< Shared attribute-less fullscreen triangle

        std::unique_ptr<Core::Texture2D> m_lut;  ///< Lazily loaded color-grading LUT
        std::string m_lutPath;                   ///< Path the LUT was loaded from (reload guard)
};

} // namespace Engine
