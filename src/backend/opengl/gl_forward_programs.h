#pragma once

#include <cstdint>
#include <memory>

#include "vkm_opengl/shader/shader.h"

namespace Vkm::Engine {

class GLIBL;
class GLMaterial;

/**
 * @brief Every variant of the forward PBR ubershader and the water program, each compiled once, and
 *        the one rule for which a run draws with.
 *
 * The programs are shared: a program's uniforms are the last caller's, so a borrower sets each it
 * reads before drawing (zeroUniforms puts them all back as linked). They are compiled by build(),
 * which the backend calls once the programs a first frame draws are on their way: these are the
 * slowest it has, and would hold the splash up behind them.
 */
class GLForwardPrograms {
    public:
        /// Which lit program a material draws with, before skinning and baking pick among them;
        /// GLInstanceBatcher sorts runs by the value.
        enum class Kind : uint8_t {
            Plain   = 0,  ///< GLMaterial::plain: compiled without the optional features.
            Whole   = 1,  ///< Every feature; a run with no material draws so too.
            Layered = 2   ///< GLMaterial::layered, static only: a posed one draws as its plain() says.
        };

    public:
        GLForwardPrograms();
        ~GLForwardPrograms();

        GLForwardPrograms(const GLForwardPrograms& other) = delete;
        GLForwardPrograms& operator=(const GLForwardPrograms& other) = delete;

        GLForwardPrograms(GLForwardPrograms && other) = delete;
        GLForwardPrograms& operator=(GLForwardPrograms && other) = delete;

    public:
        /// Start compiling every variant; the getters below are for after it.
        void build();

        /**
         * @brief The kind of lit program a run of @p material draws with: layered over plain over whole.
         *
         * @param material The run's material; null draws as a whole one.
         * @param skinned  Whether the run is posed by the frame's palette.
         * @return The kind programFor picks within.
         */
        static Kind kindOf(const GLMaterial* material, bool skinned);

        /**
         * @brief The lit program a run of @p material draws with; a water run draws with water().
         *
         * A posed run draws skinned; within that, kindOf picks; a baked run reads the lightmaps.
         *
         * @param material The run's material; null draws as a whole one.
         * @param skinned  Whether the run is posed by the frame's palette.
         * @param baked    Whether a static run reads the lightmaps.
         * @return The program.
         */
        Vkm::OpenGL::Shader& programFor(const GLMaterial* material, bool skinned, bool baked);

        /// The program a water run draws with (InstanceDraw::water): shaders/opengl/scene/forward/water.
        Vkm::OpenGL::Shader& water();

        /**
         * @brief Call @p fn on each program a frame may draw with.
         *
         * @param posed Whether the skinned ones are in use this frame.
         * @param baked Whether the lightmap-reading ones are.
         * @param water Whether the water one is.
         * @param fn    Called with each program.
         */
        template<typename Fn>
        void forEach(bool posed, bool baked, bool water, Fn&& fn) {
            Programs& p = *m_programs;
            fn(p.pbr);
            fn(p.plain);
            fn(p.layered);
            if (baked) {
                fn(p.baked);
                fn(p.bakedPlain);
                fn(p.bakedLayered);
            }
            if (posed) {
                fn(p.skinned);
                fn(p.plainSkinned);
            }
            if (water) fn(p.water);
        }

        /// Zero every program's plain uniforms, the types Vkm::OpenGL::ShaderBase::zeroUniforms sets.
        void zeroUniforms();

        /**
         * @brief Bind @p program and set the uniforms every offline draw shares.
         *
         * The per-frame inputs (AO, clusters, probes, irradiance volume, fog) exist for no offscreen
         * draw, so each is off - the volume until GLSceneCapture::setAmbientVolume lends one;
         * u_probeCount = 0 also stops probe bakes recursing. u_iblIntensity is one, not the scene's:
         * the shader scales probe and volume reads by it, so a capture would carry it twice. Set,
         * because unset is 0 and drops the indirect term. The render size is the camera's
         * (GLCamera::update). The scene color a transmissive surface reads is left as zeroUniforms
         * leaves it: none.
         *
         * @param program One of these programs, about to draw offscreen.
         * @param ibl     Global IBL, bound as the ambient term once it is ready.
         */
        static void bindOffline(Vkm::OpenGL::Shader& program, const GLIBL& ibl);

    private:
        /// Every variant, built together: a whole material, a plain and a layered one, static;
        /// the first two posed; the three static ones reading the lightmaps; and water.
        struct Programs {
            Programs();

            Vkm::OpenGL::Shader pbr;
            Vkm::OpenGL::Shader plain;
            Vkm::OpenGL::Shader layered;
            Vkm::OpenGL::Shader skinned;
            Vkm::OpenGL::Shader plainSkinned;
            Vkm::OpenGL::Shader baked;
            Vkm::OpenGL::Shader bakedPlain;
            Vkm::OpenGL::Shader bakedLayered;
            Vkm::OpenGL::Shader water;
        };

    private:
        std::unique_ptr<Programs> m_programs;  ///< Null until build().
};

} // namespace Vkm::Engine
