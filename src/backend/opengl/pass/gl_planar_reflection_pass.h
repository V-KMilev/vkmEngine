#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_compute_shader.h"
#include "gl_mip_chain_texture.h"
#include "gl_shader.h"

#include "gl_pass.h"
#include "gl_target.h"
#include "frame/gl_instance_batcher.h"
#include "system/render/data/camera_data.h"
#include "system/render/data/planar_reflector_data.h"

namespace Vkm::Engine {

class GLCamera;
class GLSceneCapture;
class GLView;
class ResourceManager;
struct RenderView;

/**
 * @brief Planar reflections: the opaque scene drawn again from the camera mirrored in each seen
 *        PlanarReflector, for GLReflectionPass to lay over the surfaces on its rectangle.
 *
 * Of the reflectors the camera sees from their front, the MAX_DRAWN covering the most of the
 * screen are drawn, each at its share of the frame's size, clipped at its plane, through the
 * PBR program with the frame's lights, shadows, probes and sky - every light, not the clusters,
 * which are the camera's. The picture goes into a mip chain filtered as the reflection pass's own
 * is, so a rough surface reads it blurred. No GTAO, screen shadows, fog, transparents or
 * particles: those are the camera's screen.
 *
 * prepare() runs before the passes, with the assets: it picks, culls and readies the materials
 * the mirrored camera sees, which the frame's sync leaves out. execute() draws after the shadows.
 */
class GLPlanarReflectionPass : public GLPass {
    public:
        /// The most reflectors drawn in a frame: each is a drawing of the scene.
        static constexpr int MAX_DRAWN = 2;

    public:
        /**
         * @brief Draws with the capture's PBR program and its sky.
         *
         * @param capture The backend's scene capture, outliving this pass: its program and sky.
         * @param camera  The backend's camera block, pointed at each mirror and put back.
         */
        GLPlanarReflectionPass(GLSceneCapture& capture, GLCamera& camera);
        ~GLPlanarReflectionPass() override;

        GLPlanarReflectionPass(const GLPlanarReflectionPass& other) = delete;
        GLPlanarReflectionPass& operator=(const GLPlanarReflectionPass& other) = delete;

        GLPlanarReflectionPass(GLPlanarReflectionPass && other) = delete;
        GLPlanarReflectionPass& operator=(GLPlanarReflectionPass && other) = delete;

    public:
        /**
         * @brief Pick this frame's reflectors and what each mirrored camera sees.
         *
         * @param view      The frame.
         * @param glView    GPU mirror; the materials the mirrors see are readied in it.
         * @param resources Resolves those materials.
         */
        void prepare(const RenderView& view, GLView& glView, const ResourceManager& resources);

        void execute(GLFrameContext& ctx) override;

        /// The reflectors drawn this frame.
        int count() const { return m_count; }

        /// Reflector @p i of those drawn.
        const PlanarReflectorData& reflector(int i) const { return m_drawn[i].reflector; }

        /// Its picture's filtered chain.
        const Vkm::GL::MipChainTexture& picture(int i) const { return m_chains[i]; }

    private:
        /// One reflector to draw: where it is, its mirrored camera, what that sees.
        struct Drawn {
            PlanarReflectorData   reflector{};
            glm::vec4             plane{0.0f};  ///< World: unit normal, then -dot(normal, center).
            CameraData            camera;
            glm::uvec2            size{0u};
            std::vector<uint32_t> objects;
        };

    private:
        /**
         * @brief Draw reflector @p i's picture and filter it.
         *
         * @param ctx The frame.
         * @param i   Which of those drawn.
         */
        void draw(GLFrameContext& ctx, int i);

    private:
        GLSceneCapture& m_capture;
        GLCamera&       m_camera;

        Vkm::GL::Shader        m_skinned;     ///< Posed meshes (shaders/forward/pbr_skinned).
        Vkm::GL::ComputeShader m_source;      ///< The picture into its chain's base.
        Vkm::GL::ComputeShader m_downsample;  ///< Each chain level from the one before.

        std::array<Drawn, MAX_DRAWN>                     m_drawn;
        std::array<std::unique_ptr<GLTarget>, MAX_DRAWN> m_targets;
        std::array<Vkm::GL::MipChainTexture, MAX_DRAWN>  m_chains;
        int m_count = 0;

        GLInstanceBatcher m_batcher;
};

} // namespace Vkm::Engine
