#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {
    class GLMesh;
    class GLBackend;
    class GLIBL;
    class GLShader;
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
        ~GLIBLBakePass() override;

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
        /**
         * @brief Bake one IBL set (env + irradiance + prefilter + BRDF).
         *
         * Reads the HDR equirect at @p path and produces all four targets
         * inside @p ibl. Returns true if work happened; false if @p path
         * was empty, already-baked, or failed to load. Called once for
         * the global IBL and once per reflection probe per frame.
         */
        bool bakeOne(GLBackend& gl, GLIBL& ibl, const std::string& path,
                     class GLShader* eq, class GLShader* irr,
                     class GLShader* pf, class GLShader* br);

        ShaderHandle m_equirectShader;
        ShaderHandle m_irradianceShader;
        ShaderHandle m_prefilterShader;
        ShaderHandle m_brdfShader;

        std::unique_ptr<GLMesh>            m_cube;     ///< Unit cube for the six face captures
        std::unique_ptr<Core::ScreenTriangle> m_brdfScreenTri;  ///< Attribute-less fullscreen triangle for the BRDF LUT

        std::string m_skipPath;  ///< Global path that failed to load; do not retry until it changes
        std::unordered_map<std::uint32_t /*entityId*/, std::string> m_probeSkipPaths;
};

} // namespace Engine
