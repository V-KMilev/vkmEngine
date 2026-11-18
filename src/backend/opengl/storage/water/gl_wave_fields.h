#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "vkm_opengl/buffer/shader_storage_buffer.h"
#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/texture/texture_2d_array.h"

#include "convention/gl_bindings.h"

namespace Vkm::Engine {

struct WaveFrame;

constexpr int RIPPLE_FIELDS = 3;  ///< The ripples' repeating fields (GLWaveFields), longest ripples first.
constexpr int RIPPLE_FIELD_SIZE = 2 * GLBindings::ComputeGroups::RIPPLE_FFT;
    ///< Texels across a ripple field, a butterfly a thread for half of them
    ///< (shaders/opengl/scene/water/ripples/ripples.glsl).

/**
 * @brief What water reads besides the WaveBlock and the shore (GLWaterShore): the swell
 *        and the ripples.
 *
 * The swell is the waves' Swell drawn at the frame's moment on its cascades' grids, into
 * SSBO_SWELL, which the vertex shaders move the water by (shaders/opengl/scene/water/swell/swell.glsl): its
 * spectrum uploaded once a change, then a row of each grid a group, then a column. A texel's foam
 * is kept there too, a frame at a time.
 *
 * The ripples are the waves' short spectrum drawn on three grids of wavenumbers and transformed into three
 * repeating fields, a layer each of a RIPPLE_FIELD_SIZE-square array with its levels below, sampled with
 * anisotropy since the sea is seen at grazing angles (shaders/opengl/scene/water/ripples/ripples.glsl): along
 * X a row at a time, then along Z a column at a time. Each field keeps its own clock, FIELD_RATE draws a
 * second at most, a third of a draw behind the one before: at 60 fps a frame draws all three, and past three
 * times that, one. Their textures are made when the scene has water, not when it is first seen.
 *
 * bind() hands both to a draw: the swell at SSBO_SWELL, the ripples on their unit
 * (WaterTextureSlots).
 */
class GLWaveFields {
    public:
        GLWaveFields();
        ~GLWaveFields();

        GLWaveFields(const GLWaveFields& other) = delete;
        GLWaveFields& operator=(const GLWaveFields& other) = delete;

        GLWaveFields(GLWaveFields && other) = delete;
        GLWaveFields& operator=(GLWaveFields && other) = delete;

    public:
        /**
         * @brief Start a frame: make the ripples' textures if the scene has water, and draw @p waves'
         *        swell, which the frame's first passes move the water by, if water with waves is drawn.
         *
         * @param waves     The frame's waves; their WaveBlock bound.
         * @param hasWater  Whether the scene has any water (RenderObjects::waters).
         * @param swellShown Whether water with a share of the waves is drawn.
         */
        void beginFrame(const WaveFrame& waves, bool hasWater, bool swellShown);

        /**
         * @brief Draw @p waves' swell at their moment into SSBO_SWELL, and bind it.
         *
         * Its foam sheds over the waves' own seconds since the swell was last drawn
         * (WaveSurface::time): waves held at one moment keep it, and ones turned back shed none.
         *
         * @param waves The waves; their WaveBlock bound. Ones with no swell draw nothing.
         */
        void drawSwell(const WaveFrame& waves);

        /**
         * @brief Bring the ripples (the bound WaveBlock's) up to the frame: each field that is due
         *        and its levels, or all three after a frame with no water or that says no time has
         *        passed.
         *
         * @param seconds Wall-clock seconds since the last frame.
         */
        void drawRipples(float seconds);

        /// Forget the ripples: no water draws them now, and what is held would be stale when one does.
        void forgetRipples() { m_rippled = false; }

        /// Bind what is held; with no swell this frame or no ripples it leaves their units alone.
        void bind() const;

    private:
        /// Make the ripples' textures, once.
        void makeFields();

        /// Draw one ripple field and its levels.
        void drawField(uint32_t field);

    private:
        static constexpr float FIELD_RATE = 60.0f;  ///< Draws a second of each field at most: one a frame at
                                                    ///< 60 fps, so none steps in motion.

        Vkm::OpenGL::ComputeShader                        m_swellRows;
        Vkm::OpenGL::ComputeShader                        m_swellColumns;
        Vkm::OpenGL::ComputeShader                        m_ripples;
        Vkm::OpenGL::ComputeShader                        m_ripplesColumns;
        Vkm::OpenGL::ComputeShader                        m_ripplesDown;
        std::unique_ptr<Vkm::OpenGL::ShaderStorageBuffer> m_swellSpectrum;
            ///< The swell's spectrum, as made (Swell::version), and its grids and foam.
        std::unique_ptr<Vkm::OpenGL::ShaderStorageBuffer> m_swell;
        uint64_t                                          m_swellVersion = 0;
        double                                            m_foamTime     = 0.0;  ///< The sea's, last drawn.
        bool                                              m_swelled      = false;

        std::unique_ptr<Vkm::OpenGL::Texture2DArray> m_rows;    ///< RGBA32F: the fields transformed along X
                                                                ///< alone.
        std::unique_ptr<Vkm::OpenGL::Texture2DArray> m_fields;  ///< A layer a ripple field, its levels down
                                                                ///< to two texels.

        bool                             m_rippled = false;
        std::array<float, RIPPLE_FIELDS> m_owed{};  ///< Each field's draws owed since its last, by the
                                                    ///< seconds gone; due from half of one.
};

} // namespace Vkm::Engine
