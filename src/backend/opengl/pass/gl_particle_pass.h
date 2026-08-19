#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gl_pass.h"
#include "gl_vertex_array.h"

namespace Vkm::GL {
    class Shader;
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

struct ParticleData;

/**
 * @brief Draws the frame's particles as camera-facing billboards.
 *
 * Runs after the forward pass so particles land in the scene target and depth-test
 * against the geometry already there (without writing depth). Additive emitters
 * draw first (order-independent), then the back-to-front-sorted alpha ones.
 *
 * Attribute-less: instances live in an SSBO the vertex stage indexes, so a batch
 * is one instanced 4-vertex draw with no vertex buffer to maintain.
 */
class GLParticlePass : public GLPass {
    public:
        GLParticlePass();
        ~GLParticlePass() override;

        GLParticlePass(const GLParticlePass& other) = delete;
        GLParticlePass& operator=(const GLParticlePass& other) = delete;

        GLParticlePass(GLParticlePass && other) = delete;
        GLParticlePass& operator=(GLParticlePass && other) = delete;

        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief Upload @p batch to the instance SSBO and draw it as one
         * instanced quad call (the vertex shader expands each particle).
         */
        void drawBatch(const std::vector<ParticleData>& batch);

    private:
        std::unique_ptr<Vkm::GL::Shader>              m_shader;
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_instances;
        uint32_t                                      m_capacity = 0;  ///< Instances the SSBO can hold.
        Vkm::GL::VertexArray                          m_vao;           ///< Empty VAO: core profile needs one bound to draw.
};

} // namespace Vkm::Engine
