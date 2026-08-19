#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

namespace Vkm::GL {
    class Texture3D;
}

namespace Vkm::Engine {

/**
 * @brief GPU storage for one baked irradiance volume: SH-L1 on a probe grid.
 *
 * Four RGBA16F 3D textures, one per SH-L1 coefficient, each sized to the probe
 * grid. Keeping the coefficients in separate volumes (rather than packed along Z)
 * means a lookup is four *hardware-trilinear* fetches - the filtering blends
 * between probes, which is exactly what we want, instead of smearing one
 * coefficient into the next.
 *
 * GPU-only: the baker writes them as compute images, the forward pass samples
 * them. resize() reallocates when the authored grid resolution changes.
 */
class GLIrradianceVolume {
    public:
        static constexpr int SH_COEFFS = 4;  ///< SH-L1: 1 constant + 3 linear.

        GLIrradianceVolume();
        ~GLIrradianceVolume();

        GLIrradianceVolume(const GLIrradianceVolume& other) = delete;
        GLIrradianceVolume& operator=(const GLIrradianceVolume& other) = delete;

        GLIrradianceVolume(GLIrradianceVolume && other) = delete;
        GLIrradianceVolume& operator=(GLIrradianceVolume && other) = delete;

        /**
         * @brief (Re)allocate for a probe grid of @p x by @p y by @p z. No-op when
         * the dimensions already match.
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
         * @param slot Texture unit the forward shader samples (sampler3D).
         */
        void bindSlot(int i, uint32_t slot) const;

        /**
         * @brief Record that a bake filled every cell, making the volume
         * sampleable (isReady). Called by the baker after its last dispatch.
         */
        void markReady() { m_ready = true; }

        bool     isReady() const { return m_ready; }
        uint32_t sizeX()   const { return m_x; }
        uint32_t sizeY()   const { return m_y; }
        uint32_t sizeZ()   const { return m_z; }

    private:
        std::unique_ptr<Vkm::GL::Texture3D> m_sh[SH_COEFFS];
        uint32_t m_x = 0, m_y = 0, m_z = 0;
        bool     m_ready = false;
};

} // namespace Vkm::Engine
