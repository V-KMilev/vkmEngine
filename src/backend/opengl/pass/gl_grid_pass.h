#pragma once

#include <memory>

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

class GLMesh;
class GLMeshPool;

/**
 * @brief Draws a world-space ground grid on the XZ plane.
 *
 * Alpha-blends a camera-centred quad over the finished HDR scene. The colour chain has no depth,
 * so the fragment shader tests the geometry target's depth. Gated on RenderSettings::grid.
 */
class GLGridPass : public GLPass {
    public:
        /**
         * @brief Compile the grid program and build its quad in @p pool.
         *
         * @param pool Holds the quad; outlives the pass.
         */
        explicit GLGridPass(GLMeshPool& pool);
        ~GLGridPass() override;

        GLGridPass(const GLGridPass& other) = delete;
        GLGridPass& operator=(const GLGridPass& other) = delete;

        GLGridPass(GLGridPass && other) = delete;
        GLGridPass& operator=(GLGridPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::GL::Shader         m_shader;
        std::unique_ptr<GLMesh> m_quad;
};

} // namespace Vkm::Engine
