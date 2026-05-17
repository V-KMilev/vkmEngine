#pragma once

#include <memory>
#include <string>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {
    class GLMesh;
}

namespace Engine {

/**
 * @brief Bakes the IBL product set from the environment HDR (split-sum).
 *
 * Runs early in the pipeline but does work only when the environment map
 * path changes: load equirect HDR -> render the environment cubemap ->
 * convolve diffuse irradiance -> GGX-prefilter specular mips -> integrate
 * the BRDF/DFG LUT. Results live in GLView's GLIBL and are sampled by the
 * forward and skybox passes.
 */
class GLIBLBakePass : public RenderPass {
    public:
        GLIBLBakePass() = delete;
        ~GLIBLBakePass();

        GLIBLBakePass(const GLIBLBakePass& other) = delete;
        GLIBLBakePass& operator=(const GLIBLBakePass& other) = delete;

        GLIBLBakePass(GLIBLBakePass && other) = delete;
        GLIBLBakePass& operator=(GLIBLBakePass && other) = delete;

        GLIBLBakePass(
            ShaderHandle equirectShader,
            ShaderHandle irradianceShader,
            ShaderHandle prefilterShader,
            ShaderHandle brdfShader
        );

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.write(RGResource::IBL);
        }

    private:
        ShaderHandle m_equirectShader;
        ShaderHandle m_irradianceShader;
        ShaderHandle m_prefilterShader;
        ShaderHandle m_brdfShader;

        std::unique_ptr<GLMesh>            m_cube;     ///< Unit cube for the six face captures
        std::unique_ptr<Core::ScreenTriangle> m_brdfScreenTri;  ///< Attribute-less fullscreen triangle for the BRDF LUT

        std::string m_skipPath;  ///< A path that failed to load; do not retry until it changes
};

} // namespace Engine
