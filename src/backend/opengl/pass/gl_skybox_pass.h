#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "gl_render_pass.h"
#include "resource/gl_mesh.h"

namespace Engine {

/**
 * @brief Draws the baked environment cubemap as the scene background.
 *
 * Runs after opaque geometry, into the HDR target, at the far plane (depth
 * func LEQUAL, no depth write). Outputs linear radiance - the composite pass
 * tone-maps it together with lit geometry. No-ops until the IBL bake is ready.
 */
class GLSkyboxPass : public GLRenderPass {
    public:
        GLSkyboxPass() = delete;
        ~GLSkyboxPass() override = default;

        GLSkyboxPass(const GLSkyboxPass& other) = delete;
        GLSkyboxPass& operator=(const GLSkyboxPass& other) = delete;

        GLSkyboxPass(GLSkyboxPass && other) = delete;
        GLSkyboxPass& operator=(GLSkyboxPass && other) = delete;

        explicit GLSkyboxPass(ShaderHandle shader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void executeGL(GLBackend& gl, RenderGraphContext& ctx) override;

        /// Skip the background draw when the user hides the skybox
        /// (EnvironmentConfig::skybox.enabled) - IBL lighting still applies.
        /// Composes with the explicit per-pass enable.
        bool enabledForView(const RenderView& view) const override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::IBL);
            builder.write(RGResource::SceneHDR);
        }

    private:
        ShaderHandle m_shader;
        std::unique_ptr<GLMesh> m_cube;
};

} // namespace Engine
