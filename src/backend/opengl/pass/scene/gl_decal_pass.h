#pragma once

#include "vkm_opengl/shader/shader.h"

#include "pass/gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Projected decals - bullet holes, blood, scorch.
 *
 * Reconstructs the surface inside each decal's box from depth and alpha-blends the projected
 * material onto it, lit as the surface under it is, but through the live shadow maps alone: the
 * G-buffer holds no lightmap coordinates, so a decal past a light's live reach is lit where a baked
 * floor is shadowed. Runs after the reflections, so a glossy surface does not reflect over what is
 * stuck to it; a no-op with none.
 */
class GLDecalPass : public GLPass {
    public:
        GLDecalPass();
        ~GLDecalPass() override;

        GLDecalPass(const GLDecalPass& other) = delete;
        GLDecalPass& operator=(const GLDecalPass& other) = delete;

        GLDecalPass(GLDecalPass && other) = delete;
        GLDecalPass& operator=(GLDecalPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::OpenGL::Shader m_shader;
};

} // namespace Vkm::Engine
