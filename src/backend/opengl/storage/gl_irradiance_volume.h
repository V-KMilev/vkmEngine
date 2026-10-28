#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

namespace Vkm::GL {
    class ShaderBase;
    class Texture3D;
}

namespace Vkm::Engine {

struct IrradianceVolumeData;

/**
 * @brief The four SH-L1 coefficient grids of one baked irradiance volume, as read back.
 *
 * One array per coefficient, `x * y * z` cells, X fastest. `sh[0][cell].w` is the relight's
 * verdict on a probe: 1 trusted, 0 found inside geometry. The other alphas are padding.
 */
using ProbeGridSH = std::array<std::vector<glm::vec4>, 4>;

/**
 * @brief Whether the relight trusted a probe, read from its first coefficient.
 *
 * @param sh0 The probe's cell in `sh[0]`, as the bake reads it back.
 * @return True for a verdict of 1; a half survives rounding in the transfer.
 */
inline bool probeTrusted(const glm::vec4& sh0) {
    return sh0.w >= 0.5f;
}

/**
 * @brief GPU storage for one baked irradiance volume: SH-L1 on a probe grid.
 *
 * An RGBA16F 3D texture per coefficient, not packed along Z, so hardware trilinear blends
 * between probes rather than smearing one coefficient into the next. GLIrradianceBaker writes
 * them, GLPass::bindAmbient samples them; download() serves the bake's read of its probes'
 * verdicts.
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

        /// Free the four volumes, leaving it not ready, as for a scene with no volume left.
        void release();

        /**
         * @brief Bind one SH coefficient volume as a compute image.
         *
         * @param i      Coefficient index (0..SH_COEFFS-1).
         * @param unit   Image unit to bind on.
         * @param access GL access flag (write for the relight, read and write for the fill).
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
         * A synchronising read, for the bake's verdicts; @p sh comes back empty with no storage.
         *
         * @param sh Destination, resized to this volume's cell count.
         */
        void download(ProbeGridSH& sh) const;

        /**
         * @brief Record that the grid holds a finished bake of @p box.
         *
         * It takes a new bakeId(), by which a capture that read the grid knows it is stale.
         *
         * @param box The volume the bake filled the grid for.
         */
        void setBaked(const IrradianceVolumeData& box);

        /// Record that the grid holds no bake to show: the bake refused it, or the world changed.
        void setUnbaked() { m_ready = false; }

        /**
         * @brief @p asked, placed where the grid was baked.
         *
         * A grid shown while its box is moved, or while the next one bakes, stays where its light
         * was gathered.
         *
         * @param asked The volume as the frame asks for it, its intensity and fade.
         * @return @p asked over the baked box.
         */
        IrradianceVolumeData placed(const IrradianceVolumeData& asked) const;

        /**
         * @brief Trade grids with @p other: this shows what it held, and it what this did.
         *
         * Readiness and bakeId() stay with each, so a relight lit beside the frame's grid takes
         * its place whole; this takes a new lightId(). Both should hold the same grid size.
         *
         * @param other The grid traded with.
         */
        void swapGrid(GLIrradianceVolume& other);

        /// Probes in the grid, as the last resize() allocated it.
        uint32_t cellCount() const;

        bool isReady() const { return m_ready; }

        /// Which finished bake the grid holds: a new value with every one, 0 before the first.
        uint32_t bakeId() const { return m_bakeId; }

        /// Which light the grid holds: a new value with every grid swapGrid() trades in.
        uint32_t lightId() const { return m_lightId; }

    private:
        std::unique_ptr<Vkm::GL::Texture3D> m_sh[SH_COEFFS];
        bool      m_ready   = false;
        glm::vec3 m_center{0.0f};       ///< The baked box, which placed() puts the grid over.
        glm::vec3 m_halfExtents{0.0f};
        uint32_t  m_bakeId  = 0;
        uint32_t  m_lightId = 0;
};

} // namespace Vkm::Engine
