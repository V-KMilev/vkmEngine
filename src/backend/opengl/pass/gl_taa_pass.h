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
 * @brief Temporal anti-aliasing (camera-reprojection accumulation).
 *
 * Runs after SSR, before bloom. Reads the resolved HDR scene + prior history,
 * reprojects via the previous view-projection, neighbourhood-clamps, blends,
 * then blits the result back into the HDR resolve target so the downstream
 * post chain consumes the stabilised image. Disabled by default
 * (EnvironmentConfig::taa) - a no-op pass when off, so the verified pipeline
 * is unchanged until opted in.
 */
class GLTAAPass : public RenderPass {
    public:
        GLTAAPass() = delete;
        ~GLTAAPass();

        GLTAAPass(const GLTAAPass& other) = delete;
        GLTAAPass& operator=(const GLTAAPass& other) = delete;

        GLTAAPass(GLTAAPass && other) = delete;
        GLTAAPass& operator=(GLTAAPass && other) = delete;

        explicit GLTAAPass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::SceneHDRResolved);
            builder.read(RGResource::GBufferPosition);
            builder.read(RGResource::TAAHistory);
            builder.write(RGResource::SceneHDRResolved);
            builder.write(RGResource::TAAHistory);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        ShaderHandle m_shader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;

        glm::mat4 m_prevViewProj = glm::mat4(1.0f);
        bool      m_havePrev = false;
};

} // namespace Engine
