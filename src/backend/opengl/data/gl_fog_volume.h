#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

namespace Core {
    class Texture3D;
}

namespace Engine {

/**
 * @brief The froxel volumetric-fog volume: two view-frustum-aligned 3D textures.
 *
 * m_scatter holds per-froxel in-scattered light (rgb) + extinction (a), written
 * by the injection compute. m_integrated holds the front-to-back accumulated
 * scattering (rgb) + transmittance (a), written by the integration compute and
 * sampled by the apply pass. GPU-only; allocated lazily by the fog pass on the
 * first fog-enabled frame (and kept - fog toggles must not thrash ~15 MB).
 */
class GLFogVolume {
    public:
        GLFogVolume();
        ~GLFogVolume();

        GLFogVolume(const GLFogVolume& other) = delete;
        GLFogVolume& operator=(const GLFogVolume& other) = delete;

        GLFogVolume(GLFogVolume && other) = delete;
        GLFogVolume& operator=(GLFogVolume && other) = delete;

        /**
         * @brief Allocate (or reallocate) the two froxel 3D textures at @p x by
         * @p y by @p z froxels. A no-op when the dimensions already match, so
         * the fog pass may call it every frame; reallocates when the fog
         * quality changes. A live GL context must exist.
         *
         * @param x Froxel grid width.
         * @param y Froxel grid height.
         * @param z Froxel grid depth (slices).
         */
        void resize(uint32_t x, uint32_t y, uint32_t z);

        /**
         * @brief Whether the volumes are allocated.
         *
         * The fog pass checks this before binding; nothing may bind the volumes
         * while it is false.
         */
        bool ready() const { return static_cast<bool>(m_scatter); }

        /**
         * @brief Bind the scatter volume as a compute image.
         *
         * @param unit   Image unit to bind on.
         * @param access GL access flag (write for inject, read for integrate).
         */
        void bindScatterImage(uint32_t unit, GLenum access) const;

        /**
         * @brief Bind the integrated volume as a compute image.
         *
         * @param unit   Image unit to bind on.
         * @param access GL access flag (write for the integration pass).
         */
        void bindIntegratedImage(uint32_t unit, GLenum access) const;

        /**
         * @brief Bind the integrated volume for sampling.
         *
         * @param slot Texture unit the fog-apply shader samples (sampler3D).
         */
        void bindIntegratedSlot(uint32_t slot) const;

    private:
        std::unique_ptr<Core::Texture3D> m_scatter;
        std::unique_ptr<Core::Texture3D> m_integrated;
        uint32_t m_x = 0;
        uint32_t m_y = 0;
        uint32_t m_z = 0;
};

} // namespace Engine
