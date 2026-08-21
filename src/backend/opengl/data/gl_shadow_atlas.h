#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_frame_buffer.h"

namespace Vkm::GL {
    class Context;
    class Texture2D;
    class TextureCube;
}

namespace Vkm::Engine {

// SHADOW_ATLAS_COLS * SHADOW_ATLAS_ROWS must be >= Config::MAX_SHADOW_CASTERS_2D (6).
constexpr uint32_t SHADOW_ATLAS_COLS     = 3;
constexpr uint32_t SHADOW_ATLAS_ROWS     = 2;
constexpr uint32_t SHADOW_ATLAS_TILE_RES = 4096;
constexpr uint32_t SHADOW_CUBE_RES       = 1024;

/**
 * @brief Depth atlases for the shadow pass: one tiled 2D atlas (directional
 *        cascades + spot maps) plus a small set of cube maps (point lights).
 *
 * The 2D atlas is a single depth Texture2D split into a grid of square tiles; a
 * caster "slot" indexes one tile and is sampled with a per-tile UV offset/scale.
 * Cube maps are individual depth TextureCubes, one per point caster, sampled by
 * direction. All depth-only (no color attachment).
 */
class GLShadowAtlas {
    public:
        GLShadowAtlas();
        ~GLShadowAtlas();

        GLShadowAtlas(const GLShadowAtlas& other) = delete;
        GLShadowAtlas& operator=(const GLShadowAtlas& other) = delete;

        GLShadowAtlas(GLShadowAtlas && other) = delete;
        GLShadowAtlas& operator=(GLShadowAtlas && other) = delete;

    public:
        /**
         * @brief Allocate the 2D atlas, the cube maps, and their FBOs.
         *
         * Idempotent for a given @p tileRes: a call with the same resolution is a
         * no-op, but a different one rebuilds the 2D atlas at the new size (the
         * cube maps are tile-res-independent and built once). Lets the editor
         * trade shadow sharpness for shadow-pass cost at runtime.
         */
        void init(uint32_t tileRes = SHADOW_ATLAS_TILE_RES);

        /**
         * @brief Bind the 2D atlas FBO and clear its whole depth buffer once.
         */
        void begin2D(const Vkm::GL::Context& gl) const;

        /**
         * @brief Restrict subsequent draws to one 2D tile's viewport.
         */
        void setTileViewport(const Vkm::GL::Context& gl, uint32_t slot) const;

        /**
         * @brief Attach cube @p slot's @p face to the cube FBO and clear it.
         */
        void beginCubeFace(const Vkm::GL::Context& gl, uint32_t slot, uint32_t face) const;

        /**
         * @brief Bind the 2D atlas depth texture to a sampler unit for
         *        depth-compare sampling.
         *
         * Leaves the texture in comparison mode, which is what a shader
         * declaring it as sampler2DShadow requires. Every bind sets the mode
         * explicitly, so neither entry point depends on which ran last.
         *
         * @param unit Texture unit index.
         */
        void bind2D(uint32_t unit) const;

        /**
         * @brief Bind the 2D atlas depth texture to a sampler unit for raw
         *        depth reads.
         *
         * Clears the comparison mode, for the debug view that displays stored
         * depth through a plain sampler2D - sampling a texture in comparison
         * mode with a non-shadow sampler is undefined.
         *
         * @param unit Texture unit index.
         */
        void bind2DRaw(uint32_t unit) const;

        /**
         * @brief Bind cube @p slot's depth map to a sampler unit.
         */
        void bindCube(uint32_t slot, uint32_t unit) const;

        /**
         * @brief Per-tile sampling transform: atlasUV = offset(slot) + localUV * scale().
         */
        static glm::vec2 tileUVOffset(uint32_t slot);
        static glm::vec2 tileUVScale();

    private:
        uint32_t m_tileRes = 0;

        Vkm::GL::FrameBuffer m_fbo2D;
        Vkm::GL::FrameBuffer m_fboCube;

        std::unique_ptr<Vkm::GL::Texture2D>                m_atlas2D;
        std::vector<std::unique_ptr<Vkm::GL::TextureCube>> m_cubes;
};

} // namespace Vkm::Engine
