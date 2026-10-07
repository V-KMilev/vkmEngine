#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gl_shader.h"

#include "gl_pass.h"
#include "gl_target.h"

#include "system/render/data/particle_data.h"

namespace Vkm::GL {
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

class ScreenTriangle;
struct RenderView;

/**
 * @brief Draws the frame's particles as camera-facing billboards.
 *
 * Runs after the forward pass, depth-tested without writing depth. Additive emitters draw first,
 * then alpha ones, which arrive unsorted and are sorted back-to-front here; each fogs at its own
 * depth. Attribute-less: instances live in an SSBO, so a batch is one instanced 4-vertex draw.
 */
class GLParticlePass : public GLPass {
    public:
        GLParticlePass();
        ~GLParticlePass() override;

        GLParticlePass(const GLParticlePass& other) = delete;
        GLParticlePass& operator=(const GLParticlePass& other) = delete;

        GLParticlePass(GLParticlePass && other) = delete;
        GLParticlePass& operator=(GLParticlePass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief One alpha particle's place in the far-to-near order.
         *
         * Eight bytes with a distance measured once per particle; the sorted order is applied to
         * the particles afterwards in one pass.
         */
        struct ParticleOrder {
            float    depth;  ///< View depth, the sort key: the quads face the view plane.
            uint32_t index;  ///< The particle's place in the unsorted list.
        };

    private:
        /**
         * @brief Order the view's alpha particles far-to-near into m_sorted.
         *
         * @param view The particles, and the eye they are sorted from.
         */
        void sortAlpha(const RenderView& view);

        /**
         * @brief Upload @p batch to the instance SSBO and draw it as one instanced quad call.
         *
         * @param batch    The billboards to draw.
         * @param emptyVao The frame's attribute-less VAO, which the draw binds.
         */
        void drawBatch(const std::vector<ParticleData>& batch, const ScreenTriangle& emptyVao);

    private:
        Vkm::GL::Shader                               m_shader;
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_instances;
        uint32_t                                      m_capacity = 0;  ///< Bytes the SSBO can hold.

        std::vector<ParticleOrder> m_order;   ///< Sort keys; capacity kept across frames.
        std::vector<ParticleData>  m_sorted;  ///< The alpha particles, far to near.

        /// The scene's depth, copied: the particles test against it attached and fade on it here.
        GLTarget m_depthCopy{GLTarget::Layout::ColorDepth, GL_R8};
};

} // namespace Vkm::Engine
