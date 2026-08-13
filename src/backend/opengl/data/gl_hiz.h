#pragma once

#include <algorithm>
#include <cstdint>

#include <GL/glew.h>

#include "gl_mip_chain_texture.h"

namespace Engine {

/**
 * @brief Hierarchical depth pyramid: the farthest depth over each screen region.
 *
 * A single-channel mip chain where every texel holds the **maximum** depth of
 * the region below it - the farthest of the nearest surfaces. That is the value
 * an occlusion test needs: something whose nearest point is further than the
 * maximum over its screen footprint has an occluder in front of every pixel it
 * covers, so it cannot be seen.
 *
 * Level 0 is half the viewport. The scene's own depth buffer is the exact
 * answer, but a candidate spanning a handful of pixels is served just as well
 * by a coarser level, and starting at half-res halves the build cost and the
 * memory for a precision nobody reads. Deeper levels each halve again, down to
 * a few texels, so the cull can pick a level where any footprint costs a small
 * fixed number of taps.
 *
 * **Odd dimensions are the trap.** Halving an odd level leaves a row or column
 * unaccounted for, and a maximum that missed a texel is *too small* - it
 * under-reports how far the occluders are and culls things that are visible.
 * The reduction shader includes the extra row/column when the parent is odd,
 * which is why the level sizes are explicit here rather than assumed.
 */
class GLHiZ {
    public:
        GLHiZ() = default;
        ~GLHiZ() = default;

        GLHiZ(const GLHiZ& other) = delete;
        GLHiZ& operator=(const GLHiZ& other) = delete;

        GLHiZ(GLHiZ&& other) = delete;
        GLHiZ& operator=(GLHiZ&& other) = delete;

    public:
        /**
         * @brief (Re)allocate the pyramid for a viewport size. Base is half-res.
         *
         * No-op when the half-res base already matches, so a steady viewport
         * allocates once.
         *
         * @param viewportWidth  Full viewport width in pixels.
         * @param viewportHeight Full viewport height in pixels.
         */
        void resize(uint32_t viewportWidth, uint32_t viewportHeight) {
            const int w = std::max(static_cast<int>(viewportWidth)  / 2, 1);
            const int h = std::max(static_cast<int>(viewportHeight) / 2, 1);
            if (m_chain.isReady() && w == m_baseW && h == m_baseH) return;
            m_baseW = w;
            m_baseH = h;

            // Down to a couple of texels: the cull picks the level where a
            // footprint fits in one tap, and a full-screen box wants the top.
            int mips = 1;
            while (std::max(w >> mips, 1) > 2 || std::max(h >> mips, 1) > 2) {
                ++mips;
                if (mips >= MAX_MIPS) break;
            }

            // R32F and NEAREST throughout: these are depths to compare, not a
            // signal to filter. Interpolating two of them would invent an
            // occluder depth that no surface has.
            m_chain.create(w, h, mips, GL_R32F, GL_RED, GL_FLOAT,
                           GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST);
            m_ready = false;
        }

        /// True once a frame has built the pyramid; false after a resize.
        bool isBuilt() const { return m_ready && m_chain.isReady(); }
        void markBuilt()     { m_ready = true; }

        bool isReady()  const { return m_chain.isReady(); }
        int  mipCount() const { return m_chain.mipCount(); }
        int  width (int mip) const { return m_chain.mipWidth(mip); }
        int  height(int mip) const { return m_chain.mipHeight(mip); }

        /// Bind the pyramid for sampling; the shader selects a level via textureLod.
        void bind(uint32_t slot) const { m_chain.bindSlot(slot); }

        /**
         * @brief Limit sampling to levels below @p mip while writing into it.
         *
         * The reduction reads level N-1 and writes level N of the same texture,
         * which is only defined while the sampled range excludes the attached
         * level.
         */
        void restrictSampling(int mip) const { m_chain.restrictSampling(mip); }
        void allowAllSampling()        const { m_chain.allowAllSampling(); }

        void bindFbo()   const { m_chain.bindFbo(); }
        void unbindFbo() const { m_chain.unbindFbo(); }
        void attachMip(int mip) const { m_chain.attachMip(mip); }

    private:
        static constexpr int MAX_MIPS = 16;   // 2^16 texels is past any viewport

    private:
        Core::MipChainTexture m_chain;
        int  m_baseW = 0;
        int  m_baseH = 0;
        bool m_ready = false;
};

} // namespace Engine
