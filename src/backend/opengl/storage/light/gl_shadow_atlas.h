#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "vkm_opengl/target/frame_buffer.h"

#include "system/render/render_limits.h"

namespace Vkm::OpenGL {
    class Context;
    class Sampler;
    class Texture2D;
    class TextureCubeArray;
}

namespace Vkm::Engine {

// The 2D atlas is COLUMNS x ROWS square blocks, each the largest tile across, half a held layer's
// edge; smaller tiles are its power-of-two quarters. Six: the sun's movers' tiles and the spots'.
constexpr uint32_t SHADOW_ATLAS_COLUMNS       = 3;
constexpr uint32_t SHADOW_ATLAS_ROWS          = 2;
// The smallest tile: still a shadow.
constexpr uint32_t SHADOW_ATLAS_MIN_TILE_RES  = 256;
// The floor for a held layer's edge, so the largest tile is still a tile.
constexpr uint32_t SHADOW_ATLAS_MIN_HELD_RES  = 2 * SHADOW_ATLAS_MIN_TILE_RES;
constexpr uint32_t SHADOW_CUBE_RES            = 1024;

// How many cascades' held layers a side of their texture holds.
constexpr uint32_t SHADOW_HELD_SIDE = 2;
static_assert(SHADOW_HELD_SIDE * SHADOW_HELD_SIDE >= Config::NUM_CASCADES, "A held layer a cascade");

/**
 * @brief A rectangle of a held layer, in texels.
 */
struct ShadowHeldRect {
    uint32_t x      = 0;
    uint32_t y      = 0;
    uint32_t width  = 0;
    uint32_t height = 0;
};

/**
 * @brief Where one 2D tile sits in the atlas, in texels; a size of 0 is no tile.
 */
struct ShadowTileRect {
    uint32_t x    = 0;
    uint32_t y    = 0;
    uint32_t size = 0;

    bool operator==(const ShadowTileRect& other) const {
        return x == other.x && y == other.y && size == other.size;
    }

    bool operator!=(const ShadowTileRect& other) const { return !(*this == other); }
};

/**
 * @brief Shadow depth: a tiled 2D atlas (what moves under the sun, and spots), a cube array (point
 *        lights) and held layers (what is fixed under the sun, a cascade each, side by side in a
 *        texture made when first drawn into).
 *
 * A 2D caster slot indexes one square tile of its own power-of-two size, laid out each frame
 * and sampled with a per-tile UV offset and scale; a point caster's slot is a layer of one
 * depth cube-map array, sampled by direction and layer. A held layer wraps: a texel belongs to a
 * place in the light's fixed grid, whichever part of the grid the layer shows now (GLShadowPlan).
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
         * @brief Allocate the 2D atlas, the cube array, and their FBOs.
         *
         * Idempotent for the resolution it settles on; a different one rebuilds the 2D atlas
         * (the cubes are built once), so sharpness can be traded for cost at runtime. What it
         * allocates is cleared to the far plane, so an undrawn tile or face reads as lit.
         *
         * @param heldRes A held layer's edge in texels, from a hand-authored project.json; clamped
         *                so the atlas's blocks fit GL_MAX_TEXTURE_SIZE, past which there would be
         *                no storage.
         * @param gl      Supplies that cached limit, and the clear.
         */
        void init(Vkm::OpenGL::Context& gl, uint32_t heldRes);

        /**
         * @brief Bind the 2D atlas FBO.
         *
         * Nothing is cleared: a tile is cleared when drawn, and an undrawn one keeps what it holds.
         *
         * @param gl Context the framebuffer is bound through.
         */
        void begin2D(const Vkm::OpenGL::Context& gl) const;

        /**
         * @brief Place this frame's 2D tiles, one per slot, largest first.
         *
         * Each size is a power of two from SHADOW_ATLAS_MIN_TILE_RES to tileResolution(); the plan
         * sizes them so (GLShadowPlan). Equal sizes give the same places as last frame, so a held
         * tile stays held; a slot that moves forgets what it held.
         *
         * @param sizes Per 2D slot, its tile's edge in texels; at most a block's worth of slots.
         * @return False when a size is out of range or there are too many; nothing is placed then.
         */
        bool layout(const std::vector<uint32_t>& sizes);

        /**
         * @brief Restrict draws to one 2D tile's viewport, clear it, and record @p signature for it.
         *
         * Recorded where the tile is cleared, so a tile is held only once something drew it.
         *
         * @param gl        Context the viewport goes through.
         * @param slot      The tile.
         * @param signature What the following draws put in it; 0 for a picture never kept.
         */
        void beginTile(Vkm::OpenGL::Context& gl, uint32_t slot, uint64_t signature);

        /**
         * @brief Restrict draws to @p rect of held layer @p layer, and clear it.
         *
         * @param gl    Context the viewport goes through.
         * @param layer The layer: its cascade.
         * @param rect  Where in the layer.
         */
        void beginHeld(Vkm::OpenGL::Context& gl, uint32_t layer, const ShadowHeldRect& rect);

        /**
         * @brief Where a held layer sits in its texture's UV, as tileUV() of a tile.
         *
         * @param layer The layer: its cascade.
         * @return Its lowest corner in xy and its extent in zw.
         */
        static glm::vec4 heldUV(uint32_t layer);

        /**
         * @brief Attach layer @p slot's @p face to the cube FBO, clear it, and record @p signature.
         *
         * @param gl        Context the viewport goes through.
         * @param slot      The point light's cube.
         * @param face      Its face, 0-5.
         * @param signature What the following draws put in it; 0 for a picture never kept.
         */
        void beginCubeFace(const Vkm::OpenGL::Context& gl, uint32_t slot, uint32_t face, uint64_t signature);

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

        /// The 2D tile @p slot's lower-left corner in the atlas, in texels; the origin for one unplaced.
        glm::vec2 tileCorner(uint32_t slot) const {
            return slot < m_tiles.size() ? glm::vec2(m_tiles[slot].x, m_tiles[slot].y) : glm::vec2(0.0f);
        }

        /// The cube-face counterpart of tileHolds, recorded by beginCubeFace().
        bool faceHolds(uint32_t slot, uint32_t face, uint64_t signature) const;

        /**
         * @brief Forget what every tile and face hold, so each is drawn again.
         *
         * For a replaced world or asset graph, or a reloaded program: either can hash the same
         * as before while drawing something else.
         */
        void forgetHeld();

        /**
         * @brief Bind the 2D atlas for a shader that samples it with sampler2DShadow, and the held
         *        layers beside it, on their own unit.
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
         * @brief Bind the point lights' cube array, and a comparing sampler, to a unit.
         *
         * Read through a samplerCubeArrayShadow: a hardware compare over a 2x2 of one face, giving
         * the fraction lit. A light's slot is its layer, so one unit serves every point light.
         *
         * @param unit Texture unit to bind the array and its sampler to.
         */
        void bindCubes(uint32_t unit) const;

        /**
         * @brief Where a tile sits in atlas UV: atlasUV = xy + localUV * zw.
         *
         * @param slot The 2D tile, as layout() placed it.
         * @return Its lowest corner in xy and its extent in zw; zero for an unplaced slot.
         */
        glm::vec4 tileUV(uint32_t slot) const;

        /**
         * @brief The largest tile's edge in texels, as init() actually built it.
         *
         * May differ from the request, as the blocks must fit GL_MAX_TEXTURE_SIZE.
         *
         * @return The edge in texels; zero before the first init().
         */
        uint32_t tileResolution() const { return m_tileRes; }

        /// A held layer's edge in texels: twice the largest tile's.
        uint32_t heldResolution() const { return 2 * m_tileRes; }

    private:
        /// Restrict draws to a rectangle of the bound framebuffer and clear it to the far plane.
        static void clearRect(
            Vkm::OpenGL::Context& gl,
            uint32_t x,
            uint32_t y,
            uint32_t width,
            uint32_t height
        );

    private:
        uint32_t m_tileRes = 0;

        std::vector<ShadowTileRect> m_tiles;  ///< Per 2D slot, where layout() placed it.

        // layout()'s workspace, kept for its capacity.
        std::vector<uint32_t>       m_order;   ///< The slots, largest tile first.
        std::vector<ShadowTileRect> m_spare;   ///< Squares not yet taken.
        std::vector<ShadowTileRect> m_placed;  ///< Per 2D slot, where this layout puts it.

        std::vector<uint64_t> m_tileSignature;  ///< Per 2D slot: what it holds, 0 = nothing worth keeping.
        std::vector<uint64_t> m_faceSignature;  ///< Per cube slot and face, slot * 6 + face.

        Vkm::OpenGL::FrameBuffer m_fbo2D;
        Vkm::OpenGL::FrameBuffer m_fboHeld;
        Vkm::OpenGL::FrameBuffer m_fboCube;

        std::unique_ptr<Vkm::OpenGL::Texture2D>        m_atlas2D;
        std::unique_ptr<Vkm::OpenGL::Texture2D>        m_held;   ///< A layer per cascade, side by side.
        std::unique_ptr<Vkm::OpenGL::TextureCubeArray> m_cubes;  ///< A layer per point caster

        // Samplers rather than toggling the texture's compare mode: a sampler overrides the unit's
        // state, so one pass reads a shadow map while another reads depth. See bind2D.
        std::unique_ptr<Vkm::OpenGL::Sampler> m_cmpSampler;
        std::unique_ptr<Vkm::OpenGL::Sampler> m_rawSampler;
        std::unique_ptr<Vkm::OpenGL::Sampler> m_cubeSampler;  ///< The comparison, for the point-light cubes.
};

} // namespace Vkm::Engine
