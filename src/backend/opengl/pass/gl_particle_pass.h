#pragma once

#include <cstdint>
#include <vector>

#include "gl_shader.h"

#include "gl_pass.h"
#include "gl_target.h"

#include "ecs/component/render/particle_emitter.h"
#include "system/render/data/camera_data.h"
#include "system/render/data/particle_data.h"

namespace Vkm::Engine {

struct RenderView;

/**
 * @brief Draws the frame's particles: billboards and strips, each blend in turn.
 *
 * Runs after the forward pass, depth-tested without writing depth, and before it into each planar
 * reflection's picture (drawMirrored). Additive particles draw first, grouped by texture, then
 * alpha ones, which arrive unsorted and are sorted back-to-front here, then distortion ones, sorted
 * the same, over a copy of the scene they bend. Strips - ribbons and trails - follow the billboards
 * of their blend: additive ones grouped by texture, the others sorted back-to-front by their heads.
 * Attribute-less: billboards and strip points live in SSBOs, each uploaded once a blend and once
 * each time they draw, drawn as an instanced draw for each run of one texture and normal map, and a
 * multi-draw for each run of strips.
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

        /**
         * @brief Draw the frame's additive and alpha particles into a planar reflection's picture.
         *
         * Seen by @p eye, which the camera block already holds, over @p target's scene and fading
         * on its depth, clipped at @p clip while GL_CLIP_DISTANCE0 is on. Lit by every light: the
         * clusters are the camera's. No fog or aerial perspective, which are the camera's, no
         * distortion, which bends the camera's screen, and nothing on the viewmodel layer.
         *
         * @param ctx    The frame: its particles, with the shadow atlas bound.
         * @param target The picture, drawn opaque already.
         * @param size   Its size.
         * @param eye    The mirrored camera.
         * @param clip   The plane, as the PBR program takes it.
         */
        void drawMirrored(
            GLFrameContext& ctx,
            GLTarget& target,
            glm::uvec2 size,
            const CameraData& eye,
            const glm::vec4& clip
        );

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
         * @brief Fill m_order with @p count indices, far to near from @p eye.
         *
         * @tparam PositionOf Callable taking an index and returning a world position.
         * @param eye        The eye they are sorted from.
         * @param count      How many there are to sort.
         * @param positionOf Where the one at an index sits, in world space.
         */
        template <typename PositionOf>
        void orderByDepth(const CameraData& eye, uint32_t count, PositionOf&& positionOf);

        /**
         * @brief Order @p particles far-to-near into m_sorted, or by texture for an order-free blend.
         *
         * @param eye       The eye they are sorted from.
         * @param particles The billboards of one blend.
         * @param byDepth   Far to near; otherwise grouped by texture, a draw a texture.
         */
        void sortBillboards(const CameraData& eye, const std::vector<ParticleData>& particles, bool byDepth);

        /**
         * @brief Bind what a run shows, and its normal map, to @p shader, and tell it which it has.
         *
         * @param ctx       The frame: its view's texture list, and the uploads.
         * @param shader    The program drawing the run.
         * @param texture   ParticleData::params.z: 0 for none, else 1 + its place in the list.
         * @param normalMap ParticleData::uvNext.w, the same way; billboards alone read one.
         * @return False while the run's texture is still loading: the run is not drawn.
         */
        bool bindTexture(GLFrameContext& ctx, Vkm::GL::Shader& shader, float texture, float normalMap = 0.0f);

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
         * @param ctx     The frame: the view's points, uploaded as this draw began, and the
         *                textures.
         * @param eye     The eye they are sorted from.
         * @param strips  The strips of one blend.
         * @param byDepth Far to near, by each strip's head; otherwise grouped by texture.
         */
        void drawStrips(
            GLFrameContext& ctx,
            const CameraData& eye,
            const std::vector<ParticleStrip>& strips,
            bool byDepth
        );

        /**
         * @brief Draw one blend: its billboards, then its strips.
         *
         * @param ctx        The frame: its strip points, and the GL state the blend is set on.
         * @param eye        The eye drawing: the camera's, or a mirror's.
         * @param blend      Which blend: the blend function, the shaders' u_blend, and whether to sort
         *                   by depth.
         * @param billboards The view's billboards of @p blend, unsorted.
         * @param strips     The view's strips of @p blend, unsorted.
         */
        void drawBlend(
            GLFrameContext& ctx,
            const CameraData& eye,
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

        /// A planar reflection's picture's depth, copied the same way.
        GLTarget m_mirrorDepth{GLTarget::Layout::ColorDepth, GL_R8};

        /// What a mirror sees, off the viewmodel layer: additive billboards, alpha ones, and strips of each.
        std::vector<ParticleData>  m_seenAdditive;
        std::vector<ParticleData>  m_seenAlpha;
        std::vector<ParticleStrip> m_seenStrips[2];
};

} // namespace Vkm::Engine
