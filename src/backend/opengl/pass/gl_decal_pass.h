#pragma once

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Projected decals - bullet holes, blood, scorch.
 *
 * Reconstructs the surface inside each decal's box from depth and alpha-blends the projected
 * material onto it, lit by the key light's cascades. Runs after the reflections, so a glossy
 * surface does not reflect over what is stuck to it; a no-op with no decals.
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
        Vkm::GL::Shader m_shader;
};

} // namespace Vkm::Engine
