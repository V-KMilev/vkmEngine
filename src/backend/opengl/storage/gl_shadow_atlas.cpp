#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "storage/gl_shadow_atlas.h"

#include <algorithm>

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "gl_error_handle.h"
#include "gl_sampler.h"
#include "gl_texture.h"
#include "gl_texture_cube_array.h"
#include "core/engine_config.h"

namespace Vkm::Engine {

static_assert(
    SHADOW_ATLAS_BLOCKS * SHADOW_ATLAS_BLOCKS * 4 >= Config::MAX_SHADOW_CASTERS_2D,
    "Shadow atlas too small to hold MAX_SHADOW_CASTERS_2D half-size tiles"
);

namespace {

bool isPowerOfTwo(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

uint32_t floorPowerOfTwo(uint32_t v) {
    uint32_t p = 1;
    while (p <= v / 2) p *= 2;
    return p;
}

} // namespace

GLShadowAtlas::GLShadowAtlas()  = default;
GLShadowAtlas::~GLShadowAtlas() = default;

void GLShadowAtlas::init(Vkm::GL::Context& gl, uint32_t tileRes) {
    // Clamped first, so the per-frame idempotence check compares what would be built.
    const uint32_t widest = static_cast<uint32_t>(std::max(gl.maxTextureSize(), 1));
    const uint32_t fits   = widest / SHADOW_ATLAS_BLOCKS;
    // A power of two, as smaller tiles are its quarters.
    const uint32_t wanted = floorPowerOfTwo(
        std::clamp(tileRes, SHADOW_ATLAS_MIN_BLOCK_RES, std::max(fits, SHADOW_ATLAS_MIN_BLOCK_RES))
    );

    if (m_atlas2D && m_tileRes == wanted) return;

    // After the return, so a setting that cannot be honoured warns once per build, not per frame.
    if (wanted > tileRes) {
        LOG_WARNING(
            "Shadow atlas tiles raised from %u to %u; below that a shadow stops being one",
            tileRes,
            wanted
        );
    } else if (wanted < tileRes) {
        LOG_WARNING(
            "Shadow atlas tiles lowered from %u to %u: a power of two, %u of them within the %d "
            "texels across this driver holds",
            tileRes,
            wanted,
            SHADOW_ATLAS_BLOCKS,
            gl.maxTextureSize()
        );
    }

    m_tileRes = wanted;

    const uint32_t atlasW = SHADOW_ATLAS_BLOCKS * m_tileRes;
    const uint32_t atlasH = SHADOW_ATLAS_BLOCKS * m_tileRes;

    // Linear: a sampler2DShadow returns the bilinear-weighted fraction of four texels that passed.
    // Clamped at the atlas border only; clampToTile in shaders/shadows.glsl keeps taps in a tile.
    Vkm::GL::Texture2DParams params;
    params.width           = atlasW;
    params.height          = atlasH;
    params.internalFormat  = GL_DEPTH_COMPONENT24;
    params.format          = GL_DEPTH_COMPONENT;
    params.type            = GL_FLOAT;
    params.minFilter       = Vkm::GL::TextureMinFilter::Linear;
    params.magFilter       = Vkm::GL::TextureMagFilter::Linear;
    params.wrapS           = Vkm::GL::TextureWrap::ClampToEdge;
    params.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
    params.generateMipmaps = false;
    m_atlas2D = std::make_unique<Vkm::GL::Texture2D>("shadow_atlas_2d", params);
    // A new texture holds nothing: every tile is placed and drawn before it is trusted.
    m_tileSignature.assign(Config::MAX_SHADOW_CASTERS_2D, 0);
    m_tiles.assign(Config::MAX_SHADOW_CASTERS_2D, ShadowTileRect{});

    // A bound sampler replaces every sampling parameter of its unit, so both carry the atlas's
    // filtering and clamp. They outlive a resize: they describe how it is read, not what it holds.
    if (!m_cmpSampler) {
        Vkm::GL::Sampler::Params read;
        read.minFilter = Vkm::GL::TextureMinFilter::Linear;
        read.magFilter = Vkm::GL::TextureMagFilter::Linear;
        read.wrapS     = Vkm::GL::TextureWrap::ClampToEdge;
        read.wrapT     = Vkm::GL::TextureWrap::ClampToEdge;

        m_rawSampler = std::make_unique<Vkm::GL::Sampler>(read);

        read.compare = Vkm::GL::TextureCompare::LessEqual;
        m_cmpSampler = std::make_unique<Vkm::GL::Sampler>(read);

        // The cubes likewise, clamped on the third axis too, which a cube lookup can reach at
        // an edge: compared for the shadow, raw for the soft path's blocker search.
        read.wrapR = Vkm::GL::TextureWrap::ClampToEdge;
        m_cubeSampler = std::make_unique<Vkm::GL::Sampler>(read);
        read.compare = Vkm::GL::TextureCompare::None;
        m_cubeRawSampler = std::make_unique<Vkm::GL::Sampler>(read);
    }

    m_fbo2D.bind();
    m_fbo2D.attachTexture2D(GL_DEPTH_ATTACHMENT, m_atlas2D->getID());
    m_fbo2D.setDrawBuffer(GL_NONE);
    m_fbo2D.setReadBuffer(GL_NONE);
    if (!m_fbo2D.isComplete()) {
        LOG_ERROR("GLShadowAtlas 2D framebuffer incomplete (%ux%u)", atlasW, atlasH);
    }
    // Cleared to the far plane, so an undrawn tile reads as lit, not as whatever the allocation
    // held. The cubes likewise.
    gl.setDepthWrite(true);
    gl.setViewport(0, 0, static_cast<int32_t>(atlasW), static_cast<int32_t>(atlasH));
    gl.clear(false, true, false);
    m_fbo2D.unbind();

    // Cubes use SHADOW_CUBE_RES, so they and their FBO are built once; a 2D resize leaves them be.
    if (!m_cubes) {
        m_cubes = std::make_unique<Vkm::GL::TextureCubeArray>();
        m_cubes->create(
            static_cast<int>(SHADOW_CUBE_RES),
            1,
            static_cast<int>(Config::MAX_SHADOW_CASTERS_CUBE),
            GL_DEPTH_COMPONENT24
        );
        m_faceSignature.assign(Config::MAX_SHADOW_CASTERS_CUBE * 6, 0);

        // No colour buffer; the draw/read buffer state set once persists across beginCubeFace.
        m_fboCube.bind();
        m_fboCube.setDrawBuffer(GL_NONE);
        m_fboCube.setReadBuffer(GL_NONE);
        gl.setViewport(0, 0, static_cast<int32_t>(SHADOW_CUBE_RES), static_cast<int32_t>(SHADOW_CUBE_RES));
        for (int layer = 0; layer < static_cast<int>(Config::MAX_SHADOW_CASTERS_CUBE); ++layer) {
            for (int face = 0; face < 6; ++face) {
                m_cubes->attachFace(GL_DEPTH_ATTACHMENT, layer, face, 0);
                gl.clear(false, true, false);
            }
        }
        m_fboCube.unbind();
    }
}

bool GLShadowAtlas::layout(const std::vector<uint32_t>& sizes) {
    if (sizes.size() > m_tiles.size()) return false;
    uint64_t area = 0;
    for (const uint32_t size : sizes) {
        if (!isPowerOfTwo(size) || size < SHADOW_ATLAS_MIN_TILE_RES || size > m_tileRes) return false;
        area += static_cast<uint64_t>(size) * size;
    }
    if (area > capacity(m_tileRes)) return false;

    // Largest first, then by slot, each into the smallest spare square that holds it, split into
    // quarters until it fits. Power-of-two squares placed so never fail while their area fits.
    m_order.resize(sizes.size());
    for (uint32_t i = 0; i < m_order.size(); ++i) m_order[i] = i;
    std::sort(m_order.begin(), m_order.end(), [&](uint32_t a, uint32_t b) {
        return sizes[a] != sizes[b] ? sizes[a] > sizes[b] : a < b;
    });

    m_spare.clear();
    for (uint32_t by = 0; by < SHADOW_ATLAS_BLOCKS; ++by) {
        for (uint32_t bx = 0; bx < SHADOW_ATLAS_BLOCKS; ++bx) {
            m_spare.push_back({bx * m_tileRes, by * m_tileRes, m_tileRes});
        }
    }
    m_placed.assign(m_tiles.size(), ShadowTileRect{});
    for (const uint32_t slot : m_order) {
        const uint32_t size = sizes[slot];
        auto best = m_spare.end();
        for (auto it = m_spare.begin(); it != m_spare.end(); ++it) {
            if (it->size >= size && (best == m_spare.end() || it->size < best->size)) best = it;
        }
        if (best == m_spare.end()) return false;
        ShadowTileRect square = *best;
        m_spare.erase(best);
        while (square.size > size) {
            const uint32_t half = square.size / 2;
            m_spare.push_back({square.x + half, square.y, half});
            m_spare.push_back({square.x, square.y + half, half});
            m_spare.push_back({square.x + half, square.y + half, half});
            square.size = half;
        }
        m_placed[slot] = square;
    }

    for (size_t slot = 0; slot < m_tiles.size(); ++slot) {
        if (m_placed[slot] != m_tiles[slot]) m_tileSignature[slot] = 0;
    }
    m_tiles.swap(m_placed);
    return true;
}

void GLShadowAtlas::begin2D(const Vkm::GL::Context& gl) const {
    m_fbo2D.bind();
    const int32_t side = static_cast<int32_t>(SHADOW_ATLAS_BLOCKS * m_tileRes);
    gl.setViewport(0, 0, side, side);
}

void GLShadowAtlas::beginTile(Vkm::GL::Context& gl, uint32_t slot, uint64_t signature) {
    if (slot >= m_tiles.size() || m_tiles[slot].size == 0) return;
    m_tileSignature[slot] = signature;
    const int32_t tile = static_cast<int32_t>(m_tiles[slot].size);
    const int32_t x    = static_cast<int32_t>(m_tiles[slot].x);
    const int32_t y    = static_cast<int32_t>(m_tiles[slot].y);
    gl.setViewport(x, y, tile, tile);
    // The clear is scissored to the tile: its neighbours may be holding a
    // picture from an earlier frame that nothing this frame will redraw.
    gl.setScissor(x, y, tile, tile);
    gl.enableScissor(true);
    gl.clear(false, true, false);
    gl.enableScissor(false);
}

bool GLShadowAtlas::tileHolds(uint32_t slot, uint64_t signature) const {
    return slot < m_tileSignature.size() && signature != 0 && m_tileSignature[slot] == signature;
}

bool GLShadowAtlas::faceHolds(uint32_t slot, uint32_t face, uint64_t signature) const {
    const size_t index = static_cast<size_t>(slot) * 6 + face;
    return index < m_faceSignature.size() && signature != 0 && m_faceSignature[index] == signature;
}

void GLShadowAtlas::forgetHeld() {
    std::fill(m_tileSignature.begin(), m_tileSignature.end(), 0);
    std::fill(m_faceSignature.begin(), m_faceSignature.end(), 0);
}

void GLShadowAtlas::beginCubeFace(
    const Vkm::GL::Context& gl,
    uint32_t slot,
    uint32_t face,
    uint64_t signature
) {
    if (!m_cubes || slot >= Config::MAX_SHADOW_CASTERS_CUBE) return;
    m_faceSignature[static_cast<size_t>(slot) * 6 + face] = signature;
    m_fboCube.bind();
    m_cubes->attachFace(GL_DEPTH_ATTACHMENT, static_cast<int>(slot), static_cast<int>(face), 0);
    gl.setViewport(0, 0, static_cast<int32_t>(SHADOW_CUBE_RES), static_cast<int32_t>(SHADOW_CUBE_RES));
    gl.clear(false, true, false);
}

void GLShadowAtlas::bind2D(uint32_t unit) const {
    if (!m_atlas2D) return;
    m_atlas2D->bindSlot(unit);
    m_cmpSampler->bindSlot(unit);
}

void GLShadowAtlas::bind2DRaw(uint32_t unit) const {
    if (!m_atlas2D) return;
    m_atlas2D->bindSlot(unit);
    m_rawSampler->bindSlot(unit);
}

void GLShadowAtlas::bindCubes(uint32_t unit) const {
    if (!m_cubes) return;
    m_cubes->bindSlot(unit);
    m_cubeSampler->bindSlot(unit);
}

void GLShadowAtlas::bindCubesRaw(uint32_t unit) const {
    if (!m_cubes) return;
    m_cubes->bindSlot(unit);
    m_cubeRawSampler->bindSlot(unit);
}

glm::vec4 GLShadowAtlas::tileUV(uint32_t slot) const {
    if (slot >= m_tiles.size() || m_tiles[slot].size == 0) return glm::vec4(0.0f);
    const float          side = static_cast<float>(SHADOW_ATLAS_BLOCKS * m_tileRes);
    const ShadowTileRect tile = m_tiles[slot];
    return glm::vec4(
        static_cast<float>(tile.x) / side,
        static_cast<float>(tile.y) / side,
        static_cast<float>(tile.size) / side,
        static_cast<float>(tile.size) / side
    );
}

uint64_t GLShadowAtlas::capacity(uint32_t tileRes) {
    const uint64_t side = static_cast<uint64_t>(SHADOW_ATLAS_BLOCKS) * tileRes;
    return side * side;
}

} // namespace Vkm::Engine
