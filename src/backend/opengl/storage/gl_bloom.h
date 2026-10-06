#pragma once

#include <cstdint>

#include <GL/glew.h>

#include "gl_mip_chain_texture.h"

#include "gl_mip_levels.h"

namespace Vkm::Engine {

/**
 * @brief The bloom's mip chain (COD/Jimenez).
 *
 * RGBA16F at half the viewport, filled by GLBloomPass; mip 0 holds the final bloom. Owns only
 * the policy (size, mip count, format); the Vkm::GL::MipChainTexture owns the GL.
 */
class GLBloom {
    public:
        GLBloom() = default;
        ~GLBloom() = default;

        GLBloom(const GLBloom& other) = delete;
        GLBloom& operator=(const GLBloom& other) = delete;

        GLBloom(GLBloom && other) = delete;
        GLBloom& operator=(GLBloom && other) = delete;

    public:
        /**
         * @brief Allocate, or reallocate, the chain for a viewport size.
         *
         * The base is half the viewport; a no-op when it already matches.
         *
         * @param viewportWidth  The viewport's width in pixels.
         * @param viewportHeight Its height.
         */
        void resize(uint32_t viewportWidth, uint32_t viewportHeight) {
            const int w = static_cast<int>(viewportWidth) / 2;
            const int h = static_cast<int>(viewportHeight) / 2;
            if (w <= 0 || h <= 0) return;
            if (m_chain.isReady() && w == m_chain.mipWidth(0) && h == m_chain.mipHeight(0)) return;

            m_chain.create(
                w,
                h,
                mipLevelsDownToTwo(w, h, MAX_MIPS),
                GL_RGBA16F,
                GL_LINEAR_MIPMAP_NEAREST,
                GL_LINEAR
            );
        }

        /**
         * @brief Bind the chain for sampling; the shader selects a level via textureLod.
         *
         * @param slot Texture unit the chain is sampled from.
         */
        void bind(uint32_t slot) const { m_chain.bindSlot(slot); }

        /// Bind one level alone for sampling, read as level 0.
        void bindLevel(int mip, uint32_t slot) const { m_chain.bindLevel(mip, slot); }

        /// Bind one level as an image for a compute pass.
        void bindImage(int mip, uint32_t unit, GLenum access) const { m_chain.bindImage(mip, unit, access); }

        bool isReady() const          { return m_chain.isReady(); }
        int  mipCount() const         { return m_chain.mipCount(); }
        int  mipWidth(int mip) const  { return m_chain.mipWidth(mip); }
        int  mipHeight(int mip) const { return m_chain.mipHeight(mip); }

    private:
        static constexpr int MAX_MIPS = 6;

    private:
        Vkm::GL::MipChainTexture m_chain;
};

} // namespace Vkm::Engine
