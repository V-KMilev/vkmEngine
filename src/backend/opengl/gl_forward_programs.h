#pragma once

#include <memory>

#include "gl_shader.h"

namespace Vkm::Engine {

class GLIBL;
class GLMaterial;

/**
 * @brief Every variant of the forward PBR ubershader, each compiled once, and the one rule for
 *        which a run draws with.
 *
 * The programs are shared: a program's uniforms are the last caller's, so a borrower sets each it
 * reads before drawing (zeroUniforms puts them all back as linked). They are compiled by build(),
 * which the backend calls once the programs a first frame draws are on their way: these are the
 * slowest it has, and would hold the splash up behind them.
 */
class GLForwardPrograms {
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
         * @brief The program a run of @p material draws with.
         *
         * A posed run draws skinned, a layered material's layers being a static surface's; a plain
         * material (GLMaterial::plain) draws without the features it leaves off; a baked run reads
         * the lightmaps.
         *
         * @param material The run's material; null draws as a whole one.
         * @param skinned  Whether the run is posed by the frame's palette.
         * @param baked    Whether a static run reads the lightmaps.
         * @return The program.
         */
        Vkm::GL::Shader& programFor(const GLMaterial* material, bool skinned, bool baked);

        /**
         * @brief Call @p fn on each program a frame may draw with.
         *
         * @param posed Whether the skinned ones are in use this frame.
         * @param baked Whether the lightmap-reading ones are.
         * @param fn    Called with each program.
         */
        template<typename Fn>
        void forEach(bool posed, bool baked, Fn&& fn) {
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
        }

        /// Zero every program's plain uniforms, the types Vkm::GL::ShaderBase::zeroUniforms sets.
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
        static void bindOffline(Vkm::GL::Shader& program, const GLIBL& ibl);

    private:
        /// Every variant, built together: a whole material, a plain and a layered one, static;
        /// the first two posed; the three static ones reading the lightmaps.
        struct Programs {
            Programs();

            Vkm::GL::Shader pbr;
            Vkm::GL::Shader plain;
            Vkm::GL::Shader layered;
            Vkm::GL::Shader skinned;
            Vkm::GL::Shader plainSkinned;
            Vkm::GL::Shader baked;
            Vkm::GL::Shader bakedPlain;
            Vkm::GL::Shader bakedLayered;
        };

    private:
        std::unique_ptr<Programs> m_programs;  ///< Null until build().
};

} // namespace Vkm::Engine
