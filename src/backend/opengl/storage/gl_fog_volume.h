#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

namespace Vkm::GL {
    class Texture3D;
}

namespace Vkm::Engine {

/**
 * @brief The froxel volumetric-fog volume: two view-frustum-aligned 3D textures.
 *
 * m_scatter holds per-froxel in-scattered light (rgb) + extinction (a), written
 * by the injection compute. m_integrated holds the front-to-back accumulated
 * scattering (rgb) + transmittance (a), written by the integration compute and
 * sampled through GLPass::bindFog. GPU-only; allocated lazily by the fog pass
 * on the first fog-enabled frame (and kept - fog toggles must not thrash
 * ~15 MB).
 */
class GLFogVolume {
    public:
        GLFogVolume();
        ~GLFogVolume();

        GLFogVolume(const GLFogVolume& other) = delete;
        GLFogVolume& operator=(const GLFogVolume& other) = delete;

        GLFogVolume(GLFogVolume && other) = delete;
        GLFogVolume& operator=(GLFogVolume && other) = delete;

    public:
        /**
         * @brief Allocate, or reallocate, the two froxel 3D textures.
         *
         * A no-op when the dimensions already match, so the fog pass may call
         * it every frame; reallocates when the fog quality changes. A live GL
         * context must exist.
         *
         * @param x Froxels across the screen.
         * @param y Froxels down it.
         * @param z Depth slices.
         */
        void resize(uint32_t x, uint32_t y, uint32_t z);

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
         * @param slot Texture unit shaders/fog.glsl samples (sampler3D).
         */
        void bindIntegratedSlot(uint32_t slot) const;

        /**
         * @brief Whether the volumes are allocated.
         *
         * The passes that read the fog are gated on GLFrameContext::fogReady,
         * not on this.
         */
        bool isReady() const { return static_cast<bool>(m_scatter); }

        /**
         * @brief How far from the eye the slices reach: the far bound of the last one.
         *
         * A reader maps a view depth to a slice through it, and a point beyond
         * it reads the last slice. Set by the fog pass for the frame it fills.
         */
        float depth() const { return m_depth; }

        /**
         * @brief Record how far this frame's slices reach.
         *
         * @param depth View depth of the last slice's far bound, in metres.
         */
        void setDepth(float depth) { m_depth = depth; }

    private:
        std::unique_ptr<Vkm::GL::Texture3D> m_scatter;
        std::unique_ptr<Vkm::GL::Texture3D> m_integrated;
        float m_depth = 1.0f;
};

} // namespace Vkm::Engine
