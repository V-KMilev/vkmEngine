#pragma once

#include <cstdint>
#include <vector>

#include "vkm_opengl/shader/shader.h"

#include "pass/gl_pass.h"
#include "storage/gl_target.h"
#include "frame/gl_camera.h"

#include "ecs/component/effect/particle_emitter.h"
#include "core/frame/camera_data.h"
#include "system/render/data/particle_data.h"

namespace Vkm::Engine {

struct RenderView;

/**
 * @brief Draws the frame's particles: billboards and strips, each blend in turn.
 *
 * Runs after the forward pass, depth-tested without writing depth. Additive particles draw first,
 * grouped by texture, then alpha ones, which arrive unsorted and are sorted back-to-front here - at
 * half resolution while they cover the scene thickly (chooseSize), over the scene's depth there, then
 * blended over the scene as one more particle - then distortion ones, sorted the same, over a copy of
 * the scene they bend.
 * Every blend is premultiplied. Strips - trails - follow the billboards of their blend:
 * additive ones grouped by texture, the others sorted back-to-front by their heads.
 * Attribute-less: billboards and strip points live in SSBOs, the billboards uploaded once a blend,
 * drawn as an instanced draw for each run of one texture and normal map, and a
 * multi-draw for each run of strips.
 */
class GLParticlePass : public GLPass {
    public:
        static constexpr float HALF_RESOLUTION_COVER = 3.0f;  ///< How many times over the alpha billboards in
                                                              ///< view, each as a square of its width, must
                                                              ///< cover the scene to draw at half resolution:
                                                              ///< fewer cost less at full size than the
                                                              ///< half-size target's own draws.

        static constexpr float FULL_RESOLUTION_COVER = 2.0f;  ///< How far under that they must fall to draw
                                                              ///< at full size again: cover hovering at the
                                                              ///< line keeps one size rather than flipping
                                                              ///< the sharpness frame to frame.

        static constexpr uint32_t RESOLUTION_HOLD_FRAMES = 8;  ///< Frames a choice of size holds at least
                                                               ///< before it may change again.

        static constexpr float BEND_REACH = 2.0f;  ///< The longest a distortion's bend may be before its
                                                   ///< strength scales it, as a share of the screen's
                                                   ///< height: the shaders hold it so
                                                   ///< (PARTICLE_BEND_REACH), and the scene is copied
                                                   ///< that far around it.

    public:
        /**
         * @brief Draws with the backend's camera block, @p camera, bound again after its own.
         *
         * @param camera The frame's camera block, which the half-resolution draw replaces for its
         *               viewport and puts back.
         */
        explicit GLParticlePass(GLCamera& camera);
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
         * @brief Choose this frame's size for the alpha particles, with hysteresis and a hold.
         *
         * Half once they cover the scene HALF_RESOLUTION_COVER times over, full again once under
         * FULL_RESOLUTION_COVER; a choice holds RESOLUTION_HOLD_FRAMES frames before it may change.
         *
         * @param view The frame: its alpha billboards and camera.
         */
        void chooseSize(const RenderView& view);

        /**
         * @brief Draw the alpha particles at half resolution, then blend them over the scene; at full
         *        size over the scene while chooseSize says so.
         *
         * @param ctx The frame: its particles, with the programs, lights and shadows bound, and
         *            m_depthCopy holding the scene's depth.
         */
        void drawHalf(GLFrameContext& ctx);

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
        bool bindTexture(
            GLFrameContext& ctx,
            Vkm::OpenGL::Shader& shader,
            float texture,
            float normalMap = 0.0f
        );

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
         * @param ctx     The frame: its camera, the view's points, uploaded as the pass began, and the
         *                textures.
         * @param strips  The strips of one blend.
         * @param byDepth Far to near, by each strip's head; otherwise grouped by texture.
         */
        void drawStrips(GLFrameContext& ctx, const std::vector<ParticleStrip>& strips, bool byDepth);

        /**
         * @brief Draw one blend: its billboards, then its strips.
         *
         * @param ctx        The frame: its camera, its strip points, and the GL state the blend is set on.
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
        Vkm::OpenGL::Shader m_shader;  ///< Billboards (shaders/opengl/scene/particle).
        Vkm::OpenGL::Shader m_strip;   ///< Trails (shaders/opengl/scene/particle/strip).

        std::vector<DepthOrder>    m_order;   ///< Sort keys; capacity kept across frames.
        std::vector<ParticleData>  m_sorted;  ///< The billboards drawing, in their order.
        std::vector<ParticleStrip> m_strips;  ///< The strips drawing, in their order.
        std::vector<int32_t>       m_firsts;  ///< A strip run's first vertices, for one multi-draw.
        std::vector<int32_t>       m_counts;  ///< And its vertex counts.

        GLTarget m_depthCopy{GLTarget::Layout::Depth};
            ///< The scene's depth, copied: the particles test against it attached and fade on it here.

        GLTarget m_half{GLTarget::Layout::ColorDepth};  ///< The alpha particles at half resolution,
                                                        ///< premultiplied, over the nearest of each block of
                                                        ///< the scene's depth
                                                        ///< (shaders/opengl/scene/particle/half_depth), and
                                                        ///< that depth copied to fade on.
        GLTarget m_halfDepthCopy{GLTarget::Layout::Depth};

        Vkm::OpenGL::Shader m_halfDepth;   ///< The scene's depth into m_half
                                           ///< (shaders/opengl/scene/particle/half_depth).
        Vkm::OpenGL::Shader m_composite;   ///< m_half over the scene
                                           ///< (shaders/opengl/scene/particle/composite).
        GLCamera            m_halfCamera;  ///< The frame's camera at m_half's size.
        GLCamera&           m_camera;      ///< The backend's; see the constructor.

        bool     m_halfSize = false;                   ///< Whether the alpha particles draw at half
                                                       ///< resolution.
        uint32_t m_sizeHeld = RESOLUTION_HOLD_FRAMES;  ///< Frames m_halfSize has held, up to
                                                       ///< RESOLUTION_HOLD_FRAMES: free to choose from the
                                                       ///< first.
};

} // namespace Vkm::Engine
