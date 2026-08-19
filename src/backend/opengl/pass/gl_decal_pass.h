#pragma once

#include <memory>

#include "gl_pass.h"

namespace Vkm::GL {
    class Shader;
}

namespace Engine {

class GLMesh;

/**
 * @brief Projected decals - bullet holes, blood, scorch.
 *
 * Draws each decal's box over the resolved scene, reconstructs the surface under
 * it from depth, and alpha-blends the projected material where that surface falls
 * inside the box. Runs after the colour resolve; a no-op with no decals.
 */
class GLDecalPass : public GLPass {
    public:
        GLDecalPass();
        ~GLDecalPass() override;

        GLDecalPass(const GLDecalPass& other) = delete;
        GLDecalPass& operator=(const GLDecalPass& other) = delete;

        GLDecalPass(GLDecalPass && other) = delete;
        GLDecalPass& operator=(GLDecalPass && other) = delete;

        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Vkm::GL::Shader> m_shader;
        std::unique_ptr<GLMesh>        m_cube;
};

} // namespace Engine
