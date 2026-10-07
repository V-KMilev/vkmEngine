#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <GL/glew.h>

#include "system/render/irradiance_dilation.h"

namespace Vkm::GL {
    class ShaderBase;
    class Texture3D;
}

namespace Vkm::Engine {

struct IrradianceVolumeData;

/**
 * @brief GPU storage for one baked irradiance volume: SH-L1 on a probe grid.
 *
 * An RGBA16F 3D texture per coefficient, not packed along Z, so hardware trilinear blends
 * between probes rather than smearing one coefficient into the next. GLIrradianceBaker writes
 * them, GLPass::bindAmbient samples them; download()/upload() serve the CPU step between, where
 * the bake repairs the probes it refused.
 */
class GLIrradianceVolume {
    public:
        /// SH-L1: 1 constant + 3 linear, a texture each.
        static constexpr int SH_COEFFS = static_cast<int>(std::tuple_size_v<ProbeGridSH>);

        GLIrradianceVolume();
        ~GLIrradianceVolume();

        GLIrradianceVolume(const GLIrradianceVolume& other) = delete;
        GLIrradianceVolume& operator=(const GLIrradianceVolume& other) = delete;

        GLIrradianceVolume(GLIrradianceVolume && other) = delete;
        GLIrradianceVolume& operator=(GLIrradianceVolume && other) = delete;

    public:
        /**
         * @brief Allocate, or reallocate, the four volumes for a probe grid.
         *
         * A no-op when the dimensions already match.
         *
         * @param x Probes along X.
         * @param y Probes along Y.
         * @param z Probes along Z.
         */
        void resize(uint32_t x, uint32_t y, uint32_t z);

        /**
         * @brief Bind one SH coefficient volume as a compute image.
         *
         * @param i      Coefficient index (0..SH_COEFFS-1).
         * @param unit   Image unit to bind on.
         * @param access GL access flag (write for the projection compute).
         */
        void bindImage(int i, uint32_t unit, GLenum access) const;

        /**
         * @brief Bind one SH coefficient volume for sampling.
         *
         * @param i    Coefficient index (0..SH_COEFFS-1).
         * @param slot Texture unit it is sampled from (sampler3D).
         */
        void bindSlot(int i, uint32_t slot) const;

        /**
         * @brief Bind the grid for sampling and place it over @p box, in a shader that includes
         *        shaders/irradiance_volume.glsl.
         *
         * Sets u_hasIrradianceVolume to 1; a caller with no volume to lend sets it to 0 itself.
         *
         * @param shader Program whose uniforms are set; bound by the caller.
         * @param box    The world box the grid fills, what its light is scaled by, and how far
         *               inside the box it fades in.
         */
        void bindForShading(const Vkm::GL::ShaderBase& shader, const IrradianceVolumeData& box) const;

        /**
         * @brief Read every coefficient grid back into @p sh, sized to the grid.
         *
         * A synchronising read, for the bake's dilation; @p sh comes back empty with no storage.
         *
         * @param sh Destination, resized to this volume's cell count.
         */
        void download(ProbeGridSH& sh) const;

        /**
         * @brief Replace every coefficient grid from @p sh.
         *
         * Ignores a @p sh not sized to this grid, so a caller cannot half-write one.
         *
         * @param sh Source, one array per coefficient, cell count each.
         */
        void upload(const ProbeGridSH& sh) const;

        /**
         * @brief Record whether the grid holds a finished bake of the current volume.
         *
         * A finished bake also takes a new bakeId(), by which a capture that read the grid knows
         * it is stale.
         *
         * @param baked False while a bake runs or after it refused the volume;
         *              true once it filled and repaired the grid.
         */
        void setBaked(bool baked);

        /// Probes in the grid, as the last resize() allocated it.
        uint32_t cellCount() const;

        bool isReady() const { return m_ready; }

        /// Which finished bake the grid holds: a new value with every one, 0 before the first.
        uint32_t bakeId() const { return m_bakeId; }

    private:
        std::unique_ptr<Vkm::GL::Texture3D> m_sh[SH_COEFFS];
        bool     m_ready  = false;
        uint32_t m_bakeId = 0;
};

} // namespace Vkm::Engine
