#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "storage/gl_shadow_atlas.h"

#include <algorithm>

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "gl_error_handle.h"
#include "gl_sampler.h"
#include "gl_texture.h"
#include "gl_texture_cube.h"
#include "core/engine_config.h"

namespace Vkm::Engine {

static_assert(
    SHADOW_ATLAS_COLS * SHADOW_ATLAS_ROWS >= Config::MAX_SHADOW_CASTERS_2D,
    "Shadow atlas grid too small for MAX_SHADOW_CASTERS_2D"
);

GLShadowAtlas::GLShadowAtlas()  = default;
GLShadowAtlas::~GLShadowAtlas() = default;

void GLShadowAtlas::init(Vkm::GL::Context& gl, uint32_t tileRes) {
    // Clamped first, so the per-frame idempotence check compares what would be built.
    const uint32_t widest = static_cast<uint32_t>(std::max(gl.maxTextureSize(), 1));
    const uint32_t fits   = widest / std::max(SHADOW_ATLAS_COLS, SHADOW_ATLAS_ROWS);
    const uint32_t wanted =
        std::clamp(tileRes, SHADOW_ATLAS_MIN_TILE_RES, std::max(fits, SHADOW_ATLAS_MIN_TILE_RES));

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
            "Shadow atlas tiles lowered from %u to %u; %d across is the widest texture this driver holds",
            tileRes,
            wanted,
            gl.maxTextureSize()
        );
    }

    m_tileRes = wanted;

    const uint32_t atlasW = SHADOW_ATLAS_COLS * m_tileRes;
    const uint32_t atlasH = SHADOW_ATLAS_ROWS * m_tileRes;

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
    // A new texture holds nothing: every tile is drawn before it is trusted.
    m_tileSignature.assign(SHADOW_ATLAS_COLS * SHADOW_ATLAS_ROWS, 0);

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

        // The cubes are read one way only, as shadow maps; clamped on the
        // third axis too, which a cube lookup can reach at an edge.
        read.wrapR = Vkm::GL::TextureWrap::ClampToEdge;
        m_cubeSampler = std::make_unique<Vkm::GL::Sampler>(read);
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
    if (m_cubes.empty()) {
        for (uint32_t i = 0; i < Config::MAX_SHADOW_CASTERS_CUBE; ++i) {
            auto cube = std::make_unique<Vkm::GL::TextureCube>();
            cube->create(
                static_cast<int>(SHADOW_CUBE_RES),
                1,
                GL_DEPTH_COMPONENT24,
                GL_DEPTH_COMPONENT,
                GL_FLOAT,
                false
            );
            m_cubes.push_back(std::move(cube));
        }
        m_faceSignature.assign(Config::MAX_SHADOW_CASTERS_CUBE * 6, 0);

        // No colour buffer; the draw/read buffer state set once persists across beginCubeFace.
        m_fboCube.bind();
        m_fboCube.setDrawBuffer(GL_NONE);
        m_fboCube.setReadBuffer(GL_NONE);
        gl.setViewport(0, 0, static_cast<int32_t>(SHADOW_CUBE_RES), static_cast<int32_t>(SHADOW_CUBE_RES));
        for (const auto& cube : m_cubes) {
            for (int face = 0; face < 6; ++face) {
                cube->attachFace(GL_DEPTH_ATTACHMENT, face, 0);
                gl.clear(false, true, false);
            }
        }
        m_fboCube.unbind();
    }
}

void GLShadowAtlas::begin2D(const Vkm::GL::Context& gl) const {
    m_fbo2D.bind();
    const int32_t atlasW = static_cast<int32_t>(SHADOW_ATLAS_COLS * m_tileRes);
    const int32_t atlasH = static_cast<int32_t>(SHADOW_ATLAS_ROWS * m_tileRes);
    gl.setViewport(0, 0, atlasW, atlasH);
}

void GLShadowAtlas::beginTile(Vkm::GL::Context& gl, uint32_t slot, uint64_t signature) {
    if (slot < m_tileSignature.size()) m_tileSignature[slot] = signature;
    const int32_t tile = static_cast<int32_t>(m_tileRes);
    const int32_t x    = static_cast<int32_t>(slot % SHADOW_ATLAS_COLS) * tile;
    const int32_t y    = static_cast<int32_t>(slot / SHADOW_ATLAS_COLS) * tile;
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
    if (slot >= m_cubes.size()) return;
    m_faceSignature[static_cast<size_t>(slot) * 6 + face] = signature;
    m_fboCube.bind();
    m_cubes[slot]->attachFace(GL_DEPTH_ATTACHMENT, static_cast<int>(face), 0);
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

void GLShadowAtlas::bindCube(uint32_t slot, uint32_t unit) const {
    if (slot >= m_cubes.size()) return;
    m_cubes[slot]->bindSlot(unit);
    m_cubeSampler->bindSlot(unit);
}

glm::vec2 GLShadowAtlas::tileUVOffset(uint32_t slot) {
    const uint32_t col = slot % SHADOW_ATLAS_COLS;
    const uint32_t row = slot / SHADOW_ATLAS_COLS;
    return glm::vec2(
        static_cast<float>(col) / static_cast<float>(SHADOW_ATLAS_COLS),
        static_cast<float>(row) / static_cast<float>(SHADOW_ATLAS_ROWS)
    );
}

glm::vec2 GLShadowAtlas::tileUVScale() {
    return glm::vec2(
        1.0f / static_cast<float>(SHADOW_ATLAS_COLS),
        1.0f / static_cast<float>(SHADOW_ATLAS_ROWS)
    );
}

} // namespace Vkm::Engine
