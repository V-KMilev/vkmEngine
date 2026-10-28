#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_compute_shader.h"
#include "gl_mip_chain_texture.h"
#include "gl_shader.h"

#include "gl_frame_context.h"
#include "gl_pass.h"
#include "gl_target.h"
#include "frame/gl_instance_batcher.h"
#include "storage/gl_cluster_grid.h"
#include "system/render/data/camera_data.h"
#include "system/render/data/planar_reflector_data.h"

namespace Vkm::Engine {

class GLCamera;
class GLClusterPass;
class GLForwardPrograms;
class GLParticlePass;
class GLSceneCapture;
class GLView;
struct RenderView;

/**
 * @brief Planar reflections: the scene drawn again from the camera mirrored in each seen
 *        PlanarReflector, for GLReflectionPass to lay over the surfaces on its rectangle.
 *
 * Of the reflectors the camera sees from their front, the MAX_DRAWN covering the most of the
 * screen are drawn, each at its share of the frame's size, clipped at its plane, through the
 * PBR programs with the frame's lights, shadows, probes and sky - the lights culled into clusters
 * for the mirrored camera as the camera's are (the particles take every light), and every shadow
 * hard, which a picture that size hides. Opaque first, then the transparents far to near over a
 * copy of it to refract, then the particles (GLParticlePass::drawMirrored). The picture goes into
 * a mip chain filtered as the reflection pass's own is, so a rough surface reads it blurred. No
 * GTAO, screen shadows, fog, decals or distortion: those are the camera's screen.
 *
 * prepare() runs before the passes, with the assets: it picks the reflectors and culls what each
 * mirrored camera sees. execute() draws after the shadows.
 */
class GLPlanarReflectionPass : public GLPass {
    public:
        /// The most reflectors drawn in a frame: each is a drawing of the scene.
        static constexpr int MAX_DRAWN = 2;

        /// How far the resolve reads a picture off its place, as a share of the screen, for a normal
        /// leaning one unit off the plane's: a ripple or a normal map moves the reflection with it.
        static constexpr float PICTURE_BEND = 0.08f;

    public:
        /**
         * @brief Draws with the forward programs and the capture's sky.
         *
         * @param capture   The backend's scene capture, outliving this pass: its sky.
         * @param programs  The backend's forward programs, outliving this pass.
         * @param camera    The backend's camera block, pointed at each mirror and put back.
         * @param particles The backend's particle pass, outliving this one: it draws them here too.
         * @param lightCull The backend's light cull, beside this pass in its list: it culls each
         *                  mirror's lights.
         */
        GLPlanarReflectionPass(
            GLSceneCapture& capture,
            GLForwardPrograms& programs,
            GLCamera& camera,
            GLParticlePass& particles,
            GLClusterPass& lightCull
        );
        ~GLPlanarReflectionPass() override;

        GLPlanarReflectionPass(const GLPlanarReflectionPass& other) = delete;
        GLPlanarReflectionPass& operator=(const GLPlanarReflectionPass& other) = delete;

        GLPlanarReflectionPass(GLPlanarReflectionPass && other) = delete;
        GLPlanarReflectionPass& operator=(GLPlanarReflectionPass && other) = delete;

    public:
        /**
         * @brief Pick this frame's reflectors and what each mirrored camera sees.
         *
         * @param view   The frame.
         * @param glView GPU mirror, synced for @p view: its materials sort what is seen into
         *               opaque and glass.
         */
        void prepare(const RenderView& view, const GLView& glView);

        /// Draws the pictures, and leaves them for the reflection pass (GLFrameContext::planarPictures).
        void execute(GLFrameContext& ctx) override;

        /**
         * @brief The storage slot reflector @p reflector last drew into: for tests.
         *
         * @param reflector Its index in RenderView::planarReflectors.
         * @return The slot, or -1 when no slot was last drawn by it.
         */
        int slotOf(int reflector) const;

    private:
        /// One reflector to draw: where it is, its mirrored camera, what that sees.
        struct Drawn {
            PlanarReflectorData   reflector{};
            glm::vec4             plane{0.0f};  ///< World: unit normal, then -dot(normal, center).
            CameraData            camera;
            glm::uvec2            size{0u};
            glm::vec4             rect{0.0f};   ///< The part of its picture drawn: x0, y0, x1, y1 in [0, 1].
            int                   slot = 0;     ///< Which of m_slots it draws into.
            std::vector<uint32_t> objects;      ///< Opaque or cut out.
            std::vector<uint32_t> glass;        ///< Transparent.
        };

        /// What a drawn reflector draws into, kept for the one last drawn there: each is sized by its
        /// reflector's renderScale, so a reflector that changes rank keeps its storage.
        struct Slot {
            std::unique_ptr<GLTarget> target;          ///< The picture.
            std::unique_ptr<GLTarget> backdrop;        ///< Its copy, for its glass to refract.
            std::unique_ptr<GLTarget> depth;           ///< Its depth, copied for its particles to fade on.
            Vkm::GL::MipChainTexture  chain;           ///< The picture filtered down.
            GLClusterGrid             clusters;        ///< Its light lists.
            int                       reflector = -1;  ///< Last drawn here: in RenderView::planarReflectors.
        };

    private:
        /**
         * @brief Draw reflector @p i's picture and filter it.
         *
         * @param ctx The frame.
         * @param i   Which of those drawn.
         */
        void draw(GLFrameContext& ctx, int i);

        /**
         * @brief Blend reflector @p i's transparents over its picture, far to near.
         *
         * @param ctx The frame.
         * @param i   Which of those drawn.
         */
        void drawGlass(GLFrameContext& ctx, int i);

    private:
        GLSceneCapture&    m_capture;
        GLForwardPrograms& m_programs;
        GLCamera&          m_camera;
        GLParticlePass&    m_particles;
        GLClusterPass&     m_lightCull;

        Vkm::GL::ComputeShader m_source;      ///< The picture into its chain's base.
        Vkm::GL::ComputeShader m_downsample;  ///< Each chain level from the one before.

        std::array<Drawn, MAX_DRAWN>           m_drawn;
        std::array<Slot, MAX_DRAWN>            m_slots;
        std::array<GLPlanarPicture, MAX_DRAWN> m_pictures;  ///< What execute() leaves on the frame.
        int m_count = 0;

        GLInstanceBatcher m_batcher;
};

} // namespace Vkm::Engine
