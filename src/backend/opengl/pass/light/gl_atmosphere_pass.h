#pragma once

#include "vkm_opengl/shader/compute_shader.h"

#include "pass/gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief The procedural sky's per-frame tables (Hillaire 2020): the sky-view table and the
 *        aerial-perspective volume.
 *
 * Computes, from the atmosphere's air tables, the sky the skybox draws - the radiance of every
 * view from the eye, current with the sun every frame - and the air between the eye and every
 * froxel of the view, which GLPass::bindFog lends the passes that shade surfaces. Runs before
 * the skybox and anything lit. A no-op without the procedural sky; the sky-view table also when
 * the skybox is hidden.
 */
class GLAtmospherePass : public GLPass {
    public:
        GLAtmospherePass();
        ~GLAtmospherePass() override;

        GLAtmospherePass(const GLAtmospherePass& other) = delete;
        GLAtmospherePass& operator=(const GLAtmospherePass& other) = delete;

        GLAtmospherePass(GLAtmospherePass && other) = delete;
        GLAtmospherePass& operator=(GLAtmospherePass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::OpenGL::ComputeShader m_skyView;
        Vkm::OpenGL::ComputeShader m_aerialPerspective;
};

} // namespace Vkm::Engine
