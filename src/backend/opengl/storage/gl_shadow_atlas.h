#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_frame_buffer.h"

namespace Vkm::GL {
    class Context;
    class Sampler;
    class Texture2D;
    class TextureCube;
}

namespace Vkm::Engine {

// SHADOW_ATLAS_COLS * SHADOW_ATLAS_ROWS must be >= Config::MAX_SHADOW_CASTERS_2D.
constexpr uint32_t SHADOW_ATLAS_COLS         = 3;
constexpr uint32_t SHADOW_ATLAS_ROWS         = 2;
// Floor for a clamped request: allocatable anywhere, and still a shadow.
constexpr uint32_t SHADOW_ATLAS_MIN_TILE_RES = 256;
constexpr uint32_t SHADOW_CUBE_RES           = 1024;

/**
 * @brief Shadow depth: a tiled 2D atlas (cascades and spots) and a set of cubes (point lights).
 *
 * A 2D caster slot indexes one square tile, sampled with a per-tile UV offset and scale; each
 * point caster has its own depth TextureCube, sampled by direction.
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
         * Idempotent for the resolution it settles on; a different one rebuilds the 2D atlas
         * (the cubes are built once), so sharpness can be traded for cost at runtime. What it
         * allocates is cleared to the far plane, so an undrawn tile or face reads as lit.
         *
         * @param tileRes One tile's edge in texels, from a hand-authored project.json; clamped so
         *                the grid fits GL_MAX_TEXTURE_SIZE, past which there would be no storage.
         * @param gl      Supplies that cached limit, and the clear.
         */
        void init(Vkm::GL::Context& gl, uint32_t tileRes);

        /**
         * @brief Bind the 2D atlas FBO.
         *
         * Nothing is cleared: a tile is cleared when drawn, and an undrawn one keeps what it holds.
         *
         * @param gl Context the framebuffer is bound through.
         */
        void begin2D(const Vkm::GL::Context& gl) const;

        /**
         * @brief Restrict draws to one 2D tile's viewport, clear it, and record @p signature for it.
         *
         * Recorded where the tile is cleared, so a tile is held only once something drew it.
         *
         * @param gl        Context the viewport goes through.
         * @param slot      The tile.
         * @param signature What the following draws put in it; 0 for a picture never kept.
         */
        void beginTile(Vkm::GL::Context& gl, uint32_t slot, uint64_t signature);

        /**
         * @brief Attach cube @p slot's @p face to the cube FBO, clear it, and record @p signature.
         *
         * @param gl        Context the viewport goes through.
         * @param slot      The point light's cube.
         * @param face      Its face, 0-5.
         * @param signature What the following draws put in it; 0 for a picture never kept.
         */
        void beginCubeFace(const Vkm::GL::Context& gl, uint32_t slot, uint32_t face, uint64_t signature);

        /**
         * @brief Whether 2D tile @p slot already holds the image @p signature names.
         *
         * A signature is what a drawn tile would look like (ShadowCasterBatch::signature), so equal
         * ones are the same picture. Zero is never held. A resize forgets every tile.
         *
         * @param slot      The 2D tile.
         * @param signature The picture the tile would be drawn as.
         * @return True when the tile can be kept; false when it must be drawn.
         */
        bool tileHolds(uint32_t slot, uint64_t signature) const;

        /// The cube-face counterpart of tileHolds, recorded by beginCubeFace().
        bool faceHolds(uint32_t slot, uint32_t face, uint64_t signature) const;

        /**
         * @brief Forget what every tile and face holds, so each is drawn again.
         *
         * For a replaced world or asset graph, or a reloaded program: either can hash the same
         * as before while drawing something else.
         */
        void forgetHeld();

        /**
         * @brief Bind the 2D atlas for a shader that samples it with sampler2DShadow.
         *
         * The comparison lives on a sampler bound to @p unit, not the texture, since passes
         * disagree on how they read this atlas.
         *
         * @param unit Texture unit to bind the atlas and its comparison sampler to.
         */
        void bind2D(uint32_t unit) const;

        /**
         * @brief Bind the 2D atlas for a shader that samples it with a plain sampler2D.
         *
         * @param unit Texture unit to bind the atlas and its non-comparing sampler to.
         */
        void bind2DRaw(uint32_t unit) const;

        /**
         * @brief Bind cube @p slot's depth map, and a comparing sampler, to a unit.
         *
         * Read through a samplerCubeShadow: a hardware compare over a 2x2 of one face, giving the
         * fraction lit.
         *
         * @param slot The point light's cube slot.
         * @param unit Texture unit to bind the cube and its sampler to.
         */
        void bindCube(uint32_t slot, uint32_t unit) const;

        /**
         * @brief Where a tile starts in the atlas: atlasUV = offset(slot) + localUV * scale().
         *
         * @param slot The 2D tile.
         * @return The tile's lowest corner, in atlas UV.
         */
        static glm::vec2 tileUVOffset(uint32_t slot);

        /// One tile's extent in atlas UV, the same for every tile.
        static glm::vec2 tileUVScale();

        /**
         * @brief One tile's edge in texels, as init() actually built it.
         *
         * May differ from the request, as the grid must fit GL_MAX_TEXTURE_SIZE; anything
         * measuring a shadow texel must measure this one.
         *
         * @return The tile edge in texels; zero before the first init().
         */
        uint32_t tileResolution() const { return m_tileRes; }

    private:
        uint32_t m_tileRes = 0;

        std::vector<uint64_t> m_tileSignature;  ///< Per 2D slot: what it holds, 0 = nothing worth keeping.
        std::vector<uint64_t> m_faceSignature;  ///< Per cube slot and face, slot * 6 + face.

        Vkm::GL::FrameBuffer m_fbo2D;
        Vkm::GL::FrameBuffer m_fboCube;

        std::unique_ptr<Vkm::GL::Texture2D>                m_atlas2D;
        std::vector<std::unique_ptr<Vkm::GL::TextureCube>> m_cubes;

        // Samplers rather than toggling the texture's compare mode: a sampler overrides the unit's
        // state, so one pass reads a shadow map while another reads depth. See bind2D.
        std::unique_ptr<Vkm::GL::Sampler> m_cmpSampler;
        std::unique_ptr<Vkm::GL::Sampler> m_rawSampler;
        std::unique_ptr<Vkm::GL::Sampler> m_cubeSampler;  ///< The comparison, for the point-light cubes.
};

} // namespace Vkm::Engine
