#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>
#include <glm/glm.hpp>

namespace Vkm::OpenGL {
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
 * them, GLPass::bindAmbient samples them.
 */
class GLIrradianceVolume {
    public:
        static constexpr int SH_COEFFS = 4;
            ///< SH-L1: 1 constant + 3 linear, a texture each.

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
         *        shaders/opengl/include/irradiance_volume.glsl.
         *
         * Sets u_hasIrradianceVolume to 1; a caller with no volume to lend sets it to 0 itself.
         *
         * @param shader Program whose uniforms are set; bound by the caller.
         * @param box    The world box the grid fills, what its light is scaled by, and how far
         *               inside the box it fades in.
         */
        void bindForShading(const Vkm::OpenGL::ShaderBase& shader, const IrradianceVolumeData& box) const;

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

        bool isReady() const { return m_ready; }

        /// Which finished bake the grid holds: a new value with every one, 0 before the first.
        uint32_t bakeId() const { return m_bakeId; }

        /// Which light the grid holds: a new value with every grid swapGrid() trades in.
        uint32_t lightId() const { return m_lightId; }

    private:
        std::unique_ptr<Vkm::OpenGL::Texture3D> m_sh[SH_COEFFS];
        bool                                    m_ready = false;
        glm::vec3 m_center{0.0f};       ///< The baked box, which placed() puts the grid over.
        glm::vec3 m_halfExtents{0.0f};
        uint32_t  m_bakeId  = 0;
        uint32_t  m_lightId = 0;
};

} // namespace Vkm::Engine
