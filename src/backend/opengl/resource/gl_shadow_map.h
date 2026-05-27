#pragma once

#include <cstdint>
#include <memory>

namespace Core {
    class FrameBuffer;
}

namespace Engine {

/**
 * @brief Multi-light shadow atlas backed by depth array textures.
 *
 * Owns two layered depth textures and a single FBO whose depth attachment
 * is re-pointed via glFramebufferTextureLayer per shadow draw:
 *
 *   - 2D array  (GL_TEXTURE_2D_ARRAY,        GL_DEPTH_COMPONENT24):
 *       one layer per directional/spot caster, sampled in PBR as
 *       sampler2DArrayShadow (hardware PCF via GL_COMPARE_REF_TO_TEXTURE).
 *
 *   - Cube array (GL_TEXTURE_CUBE_MAP_ARRAY, GL_DEPTH_COMPONENT24):
 *       one cube per point caster (6 internal layers per cube), sampled
 *       in PBR as samplerCubeArrayShadow.
 *
 * Border color is set to (1,1,1,1) on the 2D array so fragments outside
 * the shadow frustum return "fully lit" (depth = 1). Cube maps use
 * GL_CLAMP_TO_EDGE (cube wrapping handles face transitions internally).
 */
class GLShadowAtlas {
    public:
        GLShadowAtlas(
            uint32_t resolution2D,
            uint32_t resolutionCube,
            uint32_t max2DCasters,
            uint32_t maxCubeCasters
        );
        ~GLShadowAtlas();

        GLShadowAtlas(const GLShadowAtlas& other) = delete;
        GLShadowAtlas& operator=(const GLShadowAtlas& other) = delete;

        GLShadowAtlas(GLShadowAtlas && other) = delete;
        GLShadowAtlas& operator=(GLShadowAtlas && other) = delete;

    public:
        /// Reallocate the depth textures at @p res2D / @p resCube if either
        /// differs from the current size. Caster counts are preserved.
        /// Safe to call every frame: no-op when sizes match.
        ///
        /// Re-allocation invalidates any sampler binding into the old
        /// textures, so the next frame's read-side bind picks up the new
        /// id; passes that cache the texture id need to refresh it.
        void ensureResolution(uint32_t res2D, uint32_t resCube);

        /// Bind FBO, point depth attachment at 2D-array layer, set viewport, clear depth.
        void bind2DLayerForWriting(uint32_t layer) const;

        /// Bind FBO, point depth attachment at cube layer `cubeIndex` face `face`,
        /// set viewport, clear depth.
        void bindCubeFaceForWriting(uint32_t cubeIndex, uint32_t face) const;

        /// Restore the default framebuffer (binding 0).
        void unbindForWriting() const;

        /// Bind the 2D array depth texture to a texture slot for sampling.
        void bind2DForReading(uint32_t slot) const;

        /// Bind the cube array depth texture to a texture slot for sampling.
        void bindCubeForReading(uint32_t slot) const;

        /// Bind the 2D array to @p slot with a sampler-object override that
        /// disables texture comparison - the shader reads raw depth instead
        /// of hardware PCF. Used by PCSS's blocker search.
        void bind2DForReadingDepth(uint32_t slot) const;

        /// Bind the cube array to @p slot with the same compare-off sampler.
        void bindCubeForReadingDepth(uint32_t slot) const;

        uint32_t getResolution2D()   const { return m_res2D; }
        uint32_t getResolutionCube() const { return m_resCube; }
        uint32_t getMax2DCasters()   const { return m_max2D; }
        uint32_t getMaxCubeCasters() const { return m_maxCube; }

        uint32_t getTexture2DID()   const { return m_tex2D; }
        uint32_t getTextureCubeID() const { return m_texCube; }

    private:
        uint32_t m_res2D;
        uint32_t m_resCube;
        uint32_t m_max2D;
        uint32_t m_maxCube;

        uint32_t m_tex2D   = 0;
        uint32_t m_texCube = 0;

        /**
         * @brief Compare-off sampler shared by the depth-read bindings.
         *
         * GL_TEXTURE_COMPARE_MODE = GL_NONE. Bound to the depth slots via
         * bind*ForReadingDepth so the shader reads raw depth at those units
         * even though the texture is configured for hardware PCF on the
         * compare path. Shared across 2D + cube.
         */
        uint32_t m_samplerDepthNoCompare = 0;

        std::unique_ptr<Core::FrameBuffer> m_fbo;
};

} // namespace Engine
