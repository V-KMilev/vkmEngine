#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_compute_shader.h"
#include "gl_frame_buffer.h"
#include "gl_shader.h"
#include "gl_texture.h"

#include "frame/gl_instance_batcher.h"

namespace Vkm::Engine {

struct GLFrameContext;

/**
 * @brief How far each point of a square of sea ahead of the eye is from anything breaking the
 *        water, in metres: what the sea's waterline foam reads.
 *
 * The scene drawn looking straight down, only what lies under the water, the topmost of it a texel,
 * and again only what lies over it, the lowest, against the water's height on a coarse grid
 * (shaders/ocean/shore, shore_water); then a distance transform over it, a row and a column at a
 * time (ocean/shore_rows, shore_columns). Drawn in the world, not the screen, it rings what the
 * camera cannot see as well as what it can. Water near anything is a waterline, and foams. bind()
 * hands it to a draw on its unit (OceanTextureSlots::SHORE), beside the swell and the ripples
 * (GLOceanFields).
 */
class GLOceanShore {
    public:
        GLOceanShore();
        ~GLOceanShore();

        GLOceanShore(const GLOceanShore& other) = delete;
        GLOceanShore& operator=(const GLOceanShore& other) = delete;

        GLOceanShore(GLOceanShore && other) = delete;
        GLOceanShore& operator=(GLOceanShore && other) = delete;

    public:
        /// Start a frame: forget the last one's shore, which this frame captures afresh or not at all.
        void beginFrame() { m_shored = false; }

        /**
         * @brief Capture the shore over the square of sea ahead of the frame's camera.
         *
         * @param ctx The frame: its camera, ocean, objects and their materials.
         */
        void capture(GLFrameContext& ctx);

        /// Bind this frame's shore; a frame that captured none leaves the unit alone.
        void bind() const;

    public:
        /// Where this frame's shore lies: xy its corner on the XZ plane (least X, most Z), z its
        /// edge in metres; w how far it looks round a point, in metres - past that it says
        /// nothing - or 0 when there is none to read.
        glm::vec4 area() const { return m_shored ? m_area : glm::vec4(0.0f); }

    private:
        Vkm::GL::Shader        m_draw;
        Vkm::GL::Shader        m_drawSkinned;
        Vkm::GL::ComputeShader m_water;
        Vkm::GL::ComputeShader m_rows;
        Vkm::GL::ComputeShader m_columns;
        Vkm::GL::FrameBuffer   m_underTarget;
        Vkm::GL::FrameBuffer   m_overTarget;
        GLInstanceBatcher      m_batcher;
        std::vector<uint32_t>  m_objects;  ///< What lies over the square, this frame.

        // A square of SHORE_SIZE texels but the coarse height.
        std::unique_ptr<Vkm::GL::Texture2D> m_height;      ///< R16F: the water's height above rest.
        std::unique_ptr<Vkm::GL::Texture2D> m_under;       ///< R16F: metres under the water, 0 inside.
        std::unique_ptr<Vkm::GL::Texture2D> m_underDepth;  ///< Its depth, keeping the topmost.
        std::unique_ptr<Vkm::GL::Texture2D> m_over;        ///< R16F: 0 where the lowest over faces up.
        std::unique_ptr<Vkm::GL::Texture2D> m_overDepth;   ///< Its depth, keeping the lowest.
        std::unique_ptr<Vkm::GL::Texture2D> m_half;        ///< R32F: transformed along X, square metres.
        std::unique_ptr<Vkm::GL::Texture2D> m_map;         ///< R16F: metres to what breaks the water.
        glm::vec4 m_area   = glm::vec4(0.0f);
        bool      m_shored = false;
};

} // namespace Vkm::Engine
