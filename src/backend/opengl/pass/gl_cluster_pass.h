#pragma once

#include "gl_compute_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

class GLClusterGrid;

/**
 * @brief Forward+ light cull: a compute dispatch that fills the per-cluster light lists.
 *
 * Reads the light SSBO bound by GLBackend::render, then barriers so later reads see the writes.
 * The frame's grid is the camera's; a mirror culls into a grid of its own (GLPlanarReflectionPass).
 */
class GLClusterPass : public GLPass {
    public:
        GLClusterPass();
        ~GLClusterPass() override;

        GLClusterPass(const GLClusterPass& other) = delete;
        GLClusterPass& operator=(const GLClusterPass& other) = delete;

        GLClusterPass(GLClusterPass && other) = delete;
        GLClusterPass& operator=(GLClusterPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

        /**
         * @brief Cull the bound lights into @p clusters for the bound camera, and leave it bound.
         *
         * @param clusters The grid written.
         */
        void cull(const GLClusterGrid& clusters);

    private:
        Vkm::GL::ComputeShader m_compute;
};

} // namespace Vkm::Engine
