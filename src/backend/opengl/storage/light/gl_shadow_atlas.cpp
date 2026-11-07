#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "storage/light/gl_shadow_atlas.h"

#include <algorithm>

#include <GL/glew.h>
#include <glm/ext/scalar_integer.hpp>
#include <glm/gtc/round.hpp>

#include "vkm_log/logger.h"
#include "vkm_opengl/core/context.h"
#include "vkm_opengl/texture/sampler.h"
#include "vkm_opengl/texture/texture.h"
#include "vkm_opengl/texture/texture_cube_array.h"

#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "system/render/render_limits.h"

namespace Vkm::Engine {

static_assert(
    SHADOW_ATLAS_COLUMNS * SHADOW_ATLAS_ROWS >= Config::MAX_SHADOW_CASTERS_2D,
    "Shadow atlas too small to hold MAX_SHADOW_CASTERS_2D of its largest tiles"
);

namespace {

// A depth texture as the shadow maps are: filtered linearly, so a sampler2DShadow returns the
// bilinear-weighted fraction of four texels that passed, and clamped at its border only;
// clampToTile in shaders/opengl/include/shadows.glsl keeps taps in a tile.
std::unique_ptr<Vkm::OpenGL::Texture2D> depthMap(const char* name, uint32_t width, uint32_t height) {
    Vkm::OpenGL::Texture2DParams params;
    params.width           = width;
    params.height          = height;
    params.internalFormat  = GL_DEPTH_COMPONENT24;
    params.format          = GL_DEPTH_COMPONENT;
    params.type            = GL_FLOAT;
    params.minFilter       = Vkm::OpenGL::TextureMinFilter::Linear;
    params.magFilter       = Vkm::OpenGL::TextureMagFilter::Linear;
    params.wrapS           = Vkm::OpenGL::TextureWrap::ClampToEdge;
    params.wrapT           = Vkm::OpenGL::TextureWrap::ClampToEdge;
    params.generateMipmaps = false;
    return std::make_unique<Vkm::OpenGL::Texture2D>(name, params);
}

} // namespace

GLShadowAtlas::GLShadowAtlas()  = default;
GLShadowAtlas::~GLShadowAtlas() = default;

void GLShadowAtlas::init(Vkm::OpenGL::Context& gl, uint32_t heldRes) {
    // Clamped first, so the per-frame idempotence check compares what would be built.
    const uint32_t widest = static_cast<uint32_t>(std::max(gl.maxTextureSize(), 1));
    const uint32_t fits   = 2 * (widest / SHADOW_ATLAS_COLUMNS);
    // A power of two, as smaller tiles are its quarters.
    const uint32_t wanted = glm::floorPowerOfTwo(
        std::clamp(heldRes, SHADOW_ATLAS_MIN_HELD_RES, std::max(fits, SHADOW_ATLAS_MIN_HELD_RES))
    );

    if (m_atlas2D && heldResolution() == wanted) return;

    // After the return, so a setting that cannot be honored warns once per build, not per frame.
    if (wanted > heldRes) {
        LOG_WARNING("Shadow maps raised from %u to %u; below that a shadow stops being one", heldRes, wanted);
    } else if (wanted < heldRes) {
        LOG_WARNING(
            "Shadow maps lowered from %u to %u: a power of two, whose atlas fits the %d texels "
            "across this driver holds",
            heldRes,
            wanted,
            gl.maxTextureSize()
        );
    }

    m_tileRes = wanted / 2;

    const uint32_t atlasW = SHADOW_ATLAS_COLUMNS * m_tileRes;
    const uint32_t atlasH = SHADOW_ATLAS_ROWS * m_tileRes;

    m_atlas2D = depthMap("shadow_atlas_2d", atlasW, atlasH);
    // A new texture holds nothing: every tile is placed and drawn before it is trusted.
    m_tileSignature.assign(Config::MAX_SHADOW_CASTERS_2D, 0);
    m_tiles.assign(Config::MAX_SHADOW_CASTERS_2D, ShadowTileRect{});

    // A bound sampler replaces every sampling parameter of its unit, so both carry the atlas's
    // filtering and clamp. They outlive a resize: they describe how it is read, not what it holds.
    if (!m_cmpSampler) {
        Vkm::OpenGL::Sampler::Params read;
        read.minFilter = Vkm::OpenGL::TextureMinFilter::Linear;
        read.magFilter = Vkm::OpenGL::TextureMagFilter::Linear;
        read.wrapS     = Vkm::OpenGL::TextureWrap::ClampToEdge;
        read.wrapT     = Vkm::OpenGL::TextureWrap::ClampToEdge;

        m_rawSampler = std::make_unique<Vkm::OpenGL::Sampler>(read);

        read.compare = GLDepth::SHADOW_COMPARE;
        m_cmpSampler = std::make_unique<Vkm::OpenGL::Sampler>(read);

        // The cubes compared likewise, clamped on the third axis too, which a cube lookup can
        // reach at an edge.
        read.wrapR    = Vkm::OpenGL::TextureWrap::ClampToEdge;
        m_cubeSampler = std::make_unique<Vkm::OpenGL::Sampler>(read);
    }

    // Made when first drawn into (beginHeld), so a scene with no fixed caster under a sun has none.
    m_held.reset();

    m_fbo2D.bind();
    m_fbo2D.attachTexture(GL_DEPTH_ATTACHMENT, m_atlas2D->getID());
    m_fbo2D.setDrawBuffer(GL_NONE);
    m_fbo2D.setReadBuffer(GL_NONE);
    if (!m_fbo2D.isComplete()) {
        LOG_ERROR("GLShadowAtlas 2D framebuffer incomplete (%ux%u)", atlasW, atlasH);
    }
    // Cleared to the far plane, so an undrawn tile reads as lit, not as whatever the allocation
    // held. The cubes likewise.
    gl.setDepthWrite(true);
    gl.setViewport(0, 0, static_cast<int32_t>(atlasW), static_cast<int32_t>(atlasH));
    gl.clear(false, true, GLDepth::SHADOW_CLEAR);
    m_fbo2D.unbind();

    // Cubes use SHADOW_CUBE_RES, so they and their FBO are built once; a 2D resize leaves them be.
    if (!m_cubes) {
        m_cubes = std::make_unique<Vkm::OpenGL::TextureCubeArray>();
        m_cubes->create(
            static_cast<int>(SHADOW_CUBE_RES),
            1,
            static_cast<int>(Config::MAX_SHADOW_CASTERS_CUBE),
            GL_DEPTH_COMPONENT24
        );
        m_faceSignature.assign(Config::MAX_SHADOW_CASTERS_CUBE * 6, 0);

        // No color buffer; the draw/read buffer state set once persists across beginCubeFace.
        m_fboCube.bind();
        m_fboCube.setDrawBuffer(GL_NONE);
        m_fboCube.setReadBuffer(GL_NONE);
        gl.setViewport(0, 0, static_cast<int32_t>(SHADOW_CUBE_RES), static_cast<int32_t>(SHADOW_CUBE_RES));
        for (int layer = 0; layer < static_cast<int>(Config::MAX_SHADOW_CASTERS_CUBE); ++layer) {
            for (int face = 0; face < 6; ++face) {
                m_fboCube.attachLayer(GL_DEPTH_ATTACHMENT, m_cubes->getID(), 0, layer * 6 + face);
                gl.clear(false, true, GLDepth::SHADOW_CLEAR);
            }
        }
        m_fboCube.unbind();
    }
}

bool GLShadowAtlas::layout(const std::vector<uint32_t>& sizes) {
    if (sizes.size() > m_tiles.size()) return false;
    for (const uint32_t size : sizes) {
        if (!glm::isPowerOfTwo(size) || size < SHADOW_ATLAS_MIN_TILE_RES || size > m_tileRes) return false;
    }

    // Largest first, then by slot, each into the smallest spare square that holds it, split into
    // quarters until it fits. There is a block for each slot, so this never fails.
    m_order.resize(sizes.size());
    for (uint32_t i = 0; i < m_order.size(); ++i) m_order[i] = i;
    std::sort(m_order.begin(), m_order.end(), [&](uint32_t a, uint32_t b) {
        return sizes[a] != sizes[b] ? sizes[a] > sizes[b] : a < b;
    });

    m_spare.clear();
    for (uint32_t by = 0; by < SHADOW_ATLAS_ROWS; ++by) {
        for (uint32_t bx = 0; bx < SHADOW_ATLAS_COLUMNS; ++bx) {
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

void GLShadowAtlas::begin2D(const Vkm::OpenGL::Context& gl) const {
    m_fbo2D.bind();
    gl.setViewport(
        0,
        0,
        static_cast<int32_t>(SHADOW_ATLAS_COLUMNS * m_tileRes),
        static_cast<int32_t>(SHADOW_ATLAS_ROWS * m_tileRes)
    );
}

void GLShadowAtlas::clearRect(
    Vkm::OpenGL::Context& gl,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    const int32_t left = static_cast<int32_t>(x);
    const int32_t foot = static_cast<int32_t>(y);
    const int32_t wide = static_cast<int32_t>(width);
    const int32_t tall = static_cast<int32_t>(height);
    gl.setViewport(left, foot, wide, tall);
    // The clear is scissored to the rectangle: its neighbors may be holding a
    // picture from an earlier frame that nothing this frame will redraw.
    gl.setScissor(left, foot, wide, tall);
    gl.enableScissor(true);
    gl.clear(false, true, GLDepth::SHADOW_CLEAR);
    gl.enableScissor(false);
}

void GLShadowAtlas::beginTile(Vkm::OpenGL::Context& gl, uint32_t slot, uint64_t signature) {
    if (slot >= m_tiles.size() || m_tiles[slot].size == 0) return;
    m_tileSignature[slot] = signature;
    const ShadowTileRect& tile = m_tiles[slot];
    clearRect(gl, tile.x, tile.y, tile.size, tile.size);
}

void GLShadowAtlas::beginHeld(Vkm::OpenGL::Context& gl, uint32_t layer, const ShadowHeldRect& rect) {
    const uint32_t edge = heldResolution();
    // Read only where the plan drew (ShadowUBOData::heldCount), so nothing clears it whole.
    if (!m_held) {
        m_held = depthMap("shadow_held", SHADOW_HELD_SIDE * edge, SHADOW_HELD_SIDE * edge);
        m_fboHeld.attachTexture(GL_DEPTH_ATTACHMENT, m_held->getID());
        m_fboHeld.setDrawBuffer(GL_NONE);
        m_fboHeld.setReadBuffer(GL_NONE);
        m_held->bindSlot(GLBindings::ShadowTextureSlots::HELD);
        m_cmpSampler->bindSlot(GLBindings::ShadowTextureSlots::HELD);
    }
    m_fboHeld.bind();
    clearRect(
        gl,
        rect.x + edge * (layer % SHADOW_HELD_SIDE),
        rect.y + edge * (layer / SHADOW_HELD_SIDE),
        rect.width,
        rect.height
    );
}

glm::vec4 GLShadowAtlas::heldUV(uint32_t layer) {
    const float share = 1.0f / static_cast<float>(SHADOW_HELD_SIDE);
    return glm::vec4(
        static_cast<float>(layer % SHADOW_HELD_SIDE) * share,
        static_cast<float>(layer / SHADOW_HELD_SIDE) * share,
        share,
        share
    );
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
    const Vkm::OpenGL::Context& gl,
    uint32_t slot,
    uint32_t face,
    uint64_t signature
) {
    if (!m_cubes || slot >= Config::MAX_SHADOW_CASTERS_CUBE) return;
    m_faceSignature[static_cast<size_t>(slot) * 6 + face] = signature;
    m_fboCube.bind();
    m_fboCube.attachLayer(GL_DEPTH_ATTACHMENT, m_cubes->getID(), 0, static_cast<int>(slot * 6 + face));
    gl.setViewport(0, 0, static_cast<int32_t>(SHADOW_CUBE_RES), static_cast<int32_t>(SHADOW_CUBE_RES));
    gl.clear(false, true, GLDepth::SHADOW_CLEAR);
}

void GLShadowAtlas::bind2D(uint32_t unit) const {
    if (!m_atlas2D) return;
    m_atlas2D->bindSlot(unit);
    m_cmpSampler->bindSlot(unit);
    // Until there is a held layer, the atlas stands in for it, unread: the driver checks every
    // declared shadow sampler has a depth texture behind it whether or not it is read.
    (m_held ? *m_held : *m_atlas2D).bindSlot(GLBindings::ShadowTextureSlots::HELD);
    m_cmpSampler->bindSlot(GLBindings::ShadowTextureSlots::HELD);
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

glm::vec4 GLShadowAtlas::tileUV(uint32_t slot) const {
    if (slot >= m_tiles.size() || m_tiles[slot].size == 0) return glm::vec4(0.0f);
    const float wide = static_cast<float>(SHADOW_ATLAS_COLUMNS * m_tileRes);
    const float tall = static_cast<float>(SHADOW_ATLAS_ROWS * m_tileRes);
    const ShadowTileRect tile = m_tiles[slot];
    return glm::vec4(
        static_cast<float>(tile.x) / wide,
        static_cast<float>(tile.y) / tall,
        static_cast<float>(tile.size) / wide,
        static_cast<float>(tile.size) / tall
    );
}

} // namespace Vkm::Engine
