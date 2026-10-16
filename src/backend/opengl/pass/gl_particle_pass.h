#pragma once

#include <cstdint>
#include <vector>

#include "gl_shader.h"

#include "gl_pass.h"
#include "gl_target.h"

#include "ecs/component/render/particle_emitter.h"
#include "system/render/data/particle_data.h"

namespace Vkm::Engine {

struct RenderView;

/**
 * @brief Draws the frame's particles: billboards and strips, each blend in turn.
 *
 * Runs after the forward pass, depth-tested without writing depth. Additive particles draw first,
 * grouped by texture, then alpha ones, which arrive unsorted and are sorted back-to-front here,
 * then distortion ones, sorted the same, over a copy of the scene they bend. Strips - ribbons and
 * trails - follow the billboards of their blend: additive ones grouped by texture, the others
 * sorted back-to-front by their heads. Attribute-less: billboards and strip points live in SSBOs,
 * each uploaded once a blend and once a frame, drawn as an instanced draw for each run of one
 * texture and a multi-draw for each run of strips.
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
         * @brief One sorted particle's or strip's place in the far-to-near order.
         *
         * Eight bytes with a depth measured once each; the sorted order is applied afterwards in
         * one pass.
         */
        struct DepthOrder {
            float    depth;  ///< View depth, the sort key: the quads face the view plane.
            uint32_t index;  ///< Its place in the unsorted list.
        };

    private:
        /**
         * @brief Fill m_order with @p count indices, far to near from @p view's eye.
         *
         * @tparam PositionOf Callable taking an index and returning a world position.
         * @param view       The eye they are sorted from.
         * @param count      How many there are to sort.
         * @param positionOf Where the one at an index sits, in world space.
         */
        template <typename PositionOf>
        void orderByDepth(const RenderView& view, uint32_t count, PositionOf&& positionOf);

        /**
         * @brief Order @p particles far-to-near into m_sorted, or by texture for an order-free blend.
         *
         * @param view      The eye they are sorted from.
         * @param particles The billboards of one blend.
         * @param byDepth   Far to near; otherwise grouped by texture, a draw a texture.
         */
        void sortBillboards(const RenderView& view, const std::vector<ParticleData>& particles, bool byDepth);

        /**
         * @brief Bind what a run shows, and its normal map, to @p shader, and tell it which it has.
         *
         * @param ctx       The frame: its view's texture list, and the uploads.
         * @param shader    The program drawing the run.
         * @param texture   ParticleData::params.z: 0 for none, else 1 + its place in the list.
         * @param normalMap ParticleData::uvNext.w, the same way; billboards alone read one.
         */
        void bindTexture(GLFrameContext& ctx, Vkm::GL::Shader& shader, float texture, float normalMap = 0.0f);

        /**
         * @brief Upload @p batch to the instance SSBO and draw it, an instanced quad call a run.
         *
         * @param ctx   The frame: its attribute-less VAO, which the draws bind, and the textures.
         * @param batch The billboards to draw, each texture's in runs.
         */
        void drawBillboards(GLFrameContext& ctx, const std::vector<ParticleData>& batch);

        /**
         * @brief Draw @p strips, a triangle strip each through the frame's strip points, a draw
         *        call for each run of one texture.
         *
         * @param ctx     The frame: the view's points, uploaded once a frame, and the textures.
         * @param strips  The strips of one blend.
         * @param byDepth Far to near, by each strip's head; otherwise grouped by texture.
         */
        void drawStrips(GLFrameContext& ctx, const std::vector<ParticleStrip>& strips, bool byDepth);

        /**
         * @brief Draw one blend: its billboards, then its strips.
         *
         * @param ctx        The frame: its eye, its strip points, and the GL state the blend is set on.
         * @param blend      Which blend: the blend function, the shaders' u_blend, and whether to sort
         *                   by depth.
         * @param billboards The view's billboards of @p blend, unsorted.
         * @param strips     The view's strips of @p blend, unsorted.
         */
        void drawBlend(
            GLFrameContext& ctx,
            ParticleBlend blend,
            const std::vector<ParticleData>& billboards,
            const std::vector<ParticleStrip>& strips
        );

    private:
        Vkm::GL::Shader m_shader;  ///< Billboards (shaders/particle).
        Vkm::GL::Shader m_strip;   ///< Ribbons and trails (shaders/particle_strip).

        std::vector<DepthOrder>    m_order;   ///< Sort keys; capacity kept across frames.
        std::vector<ParticleData>  m_sorted;  ///< The billboards drawing, in their order.
        std::vector<ParticleStrip> m_strips;  ///< The strips drawing, in their order.
        std::vector<int32_t>       m_firsts;  ///< A strip run's first vertices, for one multi-draw.
        std::vector<int32_t>       m_counts;  ///< And its vertex counts.

        /// The scene's depth, copied: the particles test against it attached and fade on it here.
        GLTarget m_depthCopy{GLTarget::Layout::ColorDepth, GL_R8};
};

} // namespace Vkm::Engine
