#pragma once

#include <cstdint>
#include <memory>

#include "gl_compute_shader.h"
#include "gl_shader_storage_buffer.h"
#include "gl_texture_2d_array.h"

namespace Vkm::Engine {

struct OceanData;

/**
 * @brief What a Sea surface reads besides the OceanBlock and the shore (GLOceanShore): the swell
 *        and the ripples.
 *
 * The swell is the ocean's OceanSwell drawn at the frame's moment on its cascades' grids, into
 * SSBO_OCEAN_SWELL, which the vertex shaders move the water by (shaders/ocean_swell.glsl): its
 * spectrum uploaded once a change, then a row of each grid a group, then a column. A texel's foam
 * is kept there too, a frame at a time.
 *
 * The ripples are the ocean's short spectrum drawn once a frame on three grids of wavenumbers and
 * transformed into three repeating fields, a layer each of a RIPPLE_FIELD_SIZE-square array with its
 * levels below, sampled with anisotropy since the sea is seen at grazing angles
 * (shaders/ocean_ripples.glsl): along X a row at a time, then along Z a column at a time.
 *
 * bind() hands both to a draw: the swell at SSBO_OCEAN_SWELL, the ripples on their unit
 * (OceanTextureSlots).
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
        /**
         * @brief Start a frame: forget the last one's fields, and draw @p ocean's swell, which the
         *        frame's first passes move the water by.
         *
         * @param ocean The frame's sea; its OceanBlock bound.
         */
        void beginFrame(const OceanData& ocean);

        /**
         * @brief Draw @p ocean's swell at its moment into SSBO_OCEAN_SWELL, and bind it.
         *
         * Its foam sheds over the sea's own seconds since the swell was last drawn
         * (OceanSurface::time): a sea held at one moment keeps it, and one turned back sheds none.
         *
         * @param ocean The sea; its OceanBlock bound. One with no swell draws nothing.
         */
        void drawSwell(const OceanData& ocean);

        /// Draw the frame's ripples (the bound OceanBlock's) into the three fields, and their levels.
        void drawRipples();

        /// Bind what this frame made; a frame that made none leaves the units alone.
        void bind() const;

    private:
        Vkm::GL::ComputeShader m_swellRows;
        Vkm::GL::ComputeShader m_swellColumns;
        Vkm::GL::ComputeShader m_ripples;
        Vkm::GL::ComputeShader m_ripplesColumns;
        Vkm::GL::ComputeShader m_ripplesDown;
        /// The swell's spectrum, as made (OceanSwell::version), and its grids and foam.
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_swellSpectrum;
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_swell;
        uint64_t                                      m_swellVersion = 0;
        double                                        m_foamTime     = 0.0;  ///< The sea's, last drawn.
        bool                                          m_swelled      = false;

        /// RGBA32F: the fields transformed along X alone.
        std::unique_ptr<Vkm::GL::Texture2DArray> m_rows;
        /// A layer a ripple field, its levels down to two texels.
        std::unique_ptr<Vkm::GL::Texture2DArray> m_fields;

        bool m_rippled = false;
};

} // namespace Vkm::Engine
