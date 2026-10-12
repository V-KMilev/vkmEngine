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

struct RenderView;

/**
 * @brief Draws the frame's particles as camera-facing billboards.
 *
 * Runs after the forward pass, depth-tested without writing depth. Additive emitters draw first,
 * grouped by texture, then alpha ones, which arrive unsorted and are sorted back-to-front here;
 * each fogs at its own depth. Attribute-less: instances live in an SSBO, so a batch is one upload
 * and an instanced 4-vertex draw for each run of particles showing one texture.
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
         * @brief Copy the view's additive particles into m_sorted, grouped by texture.
         *
         * @param view The particles.
         */
        void sortAdditive(const RenderView& view);

        /**
         * @brief Bind what a run of particles shows, and tell the shader whether it is a texture.
         *
         * @param ctx     The frame: its view's texture list, and the uploads.
         * @param texture ParticleData::params.z: 0 for the soft disc, else 1 + its place in the list.
         */
        void bindTexture(GLFrameContext& ctx, float texture);

        /**
         * @brief Upload @p batch to the instance SSBO and draw it, an instanced quad call a run.
         *
         * @param ctx   The frame: its attribute-less VAO, which the draws bind, and the textures.
         * @param batch The billboards to draw, each texture's in runs.
         */
        void drawBatch(GLFrameContext& ctx, const std::vector<ParticleData>& batch);

    private:
        Vkm::GL::Shader                               m_shader;
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_instances;
        uint32_t                                      m_capacity = 0;  ///< Bytes the SSBO can hold.

        std::vector<ParticleOrder> m_order;   ///< Sort keys; capacity kept across frames.
        std::vector<ParticleData>  m_sorted;  ///< The batch drawing: by texture, or alpha far to near.

        /// The scene's depth, copied: the particles test against it attached and fade on it here.
        GLTarget m_depthCopy{GLTarget::Layout::ColorDepth, GL_R8};
};

} // namespace Vkm::Engine
