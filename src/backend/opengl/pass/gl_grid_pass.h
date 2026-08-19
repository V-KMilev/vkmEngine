#pragma once

#include <memory>

#include "gl_pass.h"

namespace Vkm::GL {
    class Shader;
}

namespace Vkm::Engine {

class GLMesh;

/**
 * @brief Draws a world-space ground grid on the XZ plane.
 *
 * Runs after the resolved HDR scene is complete (past the bloom capture) and
 * alpha-blends a camera-centred grid quad over it. Depth-tested against the
 * scene depth (LEQUAL) so geometry in front occludes the grid, with depth
 * writes off - it is a transparent overlay, not occluding geometry. Gated on
 * RenderSettings::grid.
 */
class GLGridPass : public GLPass {
    public:
        GLGridPass();
        ~GLGridPass() override;

        GLGridPass(const GLGridPass& other) = delete;
        GLGridPass& operator=(const GLGridPass& other) = delete;

        GLGridPass(GLGridPass && other) = delete;
        GLGridPass& operator=(GLGridPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Vkm::GL::Shader> m_shader;
        std::unique_ptr<GLMesh>        m_quad;
};

} // namespace Vkm::Engine
