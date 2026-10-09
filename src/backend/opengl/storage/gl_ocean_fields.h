#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gl_compute_shader.h"
#include "gl_frame_buffer.h"
#include "gl_shader.h"

#include "frame/gl_instance_batcher.h"

namespace Vkm::Engine {

struct GLFrameContext;

/**
 * @brief What a `waves` surface's pixels read besides the OceanBlock: the ripples and the shore.
 *
 * The ripples are the ocean's short spectrum drawn once a frame on three grids of wavenumbers and
 * transformed into three repeating fields, a layer each of a RIPPLE_FIELD_SIZE-square array with its
 * levels below, sampled with anisotropy since the sea is seen at grazing angles
 * (shaders/ocean_ripples.glsl): along X a row at a time, then along Z a column at a time.
 *
 * The shore is how far each point of a square of sea ahead of the eye is from anything breaking
 * the water, in metres: the scene drawn looking straight down, only what lies under the water,
 * the topmost of it a texel, and again only what lies over it, the lowest, against the water's
 * height on a coarse grid (shaders/ocean/shore, shore_water); then a distance transform over it,
 * a row and a column at a time (ocean/shore_rows, shore_columns). Drawn in the world, not the
 * screen, it rings what the camera cannot see as well as what it can. Water near anything is a
 * waterline, and foams. bind() hands both to the forward pass, on their units (OceanTextureSlots).
 */
class GLOceanFields {
    public:
        GLOceanFields();
        ~GLOceanFields();

        GLOceanFields(const GLOceanFields& other) = delete;
        GLOceanFields& operator=(const GLOceanFields& other) = delete;

        GLOceanFields(GLOceanFields && other) = delete;
        GLOceanFields& operator=(GLOceanFields && other) = delete;

    public:
        /// Draw the frame's ripples (the bound OceanBlock's) into the three fields, and their levels.
        void drawRipples();

        /**
         * @brief Capture the shore over the square of sea ahead of the frame's camera.
         *
         * @param ctx The frame: its camera, ocean, objects and their materials.
         */
        void captureShore(GLFrameContext& ctx);

        /// Bind what this frame made; a frame that made neither leaves the units alone.
        void bind() const;

        /// Forget this frame's products: the next frame makes its own or has none.
        void beginFrame();

        /// Where this frame's shore lies: xy its corner on the XZ plane (least X, most Z), z its
        /// edge in metres; w how far it looks round a point, in metres - past that it says
        /// nothing - or 0 when there is none to read.
        glm::vec4 shore() const { return m_shored ? m_shoreArea : glm::vec4(0.0f); }

    private:
        Vkm::GL::ComputeShader m_ripples;
        Vkm::GL::ComputeShader m_ripplesColumns;
        Vkm::GL::ComputeShader m_ripplesDown;
        Vkm::GL::Shader        m_shore;
        Vkm::GL::Shader        m_shoreSkinned;
        Vkm::GL::ComputeShader m_shoreWater;
        Vkm::GL::ComputeShader m_shoreRows;
        Vkm::GL::ComputeShader m_shoreColumns;
        Vkm::GL::FrameBuffer   m_shoreTarget;
        Vkm::GL::FrameBuffer   m_shoreOverTarget;
        GLInstanceBatcher      m_shoreBatcher;
        std::vector<uint32_t>  m_shoreObjects;  ///< What lies over the square, this frame.
        uint32_t m_rows        = 0;  ///< RGBA32F array: the fields transformed along X alone.
        uint32_t m_fields      = 0;  ///< GL_TEXTURE_2D_ARRAY, a layer a ripple field.
        int      m_levels      = 0;  ///< Its levels, down to two texels.
        uint32_t  m_shoreHeight = 0; ///< R16F, coarse: the water's height above its rest.
        uint32_t  m_shoreUnder = 0;  ///< R16F: metres under the water, 0 inside what breaks it.
        uint32_t  m_shoreDepth = 0;  ///< Its depth, keeping the topmost.
        uint32_t  m_shoreOver  = 0;  ///< R16F: 0 where the lowest thing over the water faces up.
        uint32_t  m_shoreOverDepth = 0;  ///< Its depth, keeping the lowest.
        uint32_t  m_shoreHalf  = 0;  ///< R32F: the transform along X alone, squared metres.
        uint32_t  m_shoreMap   = 0;  ///< R16F: metres to the nearest thing breaking the water.
        glm::vec4 m_shoreArea  = glm::vec4(0.0f);
        bool      m_rippled    = false;
        bool      m_shored     = false;
};

} // namespace Vkm::Engine
