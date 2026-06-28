#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_shadow_atlas.h"

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "texture/gl_texture.h"
#include "texture/gl_texture_cube.h"
#include "core/engine_config.h"

namespace Engine {

// The 2D atlas must have a tile for every directional/spot caster the shadow
// planner can hand out. Enforce the capacity invariant at compile time so a bump
// to MAX_SHADOW_CASTERS_2D in engine_config.h fails the build instead of silently
// handing out off-atlas slots.
static_assert(SHADOW_ATLAS_COLS * SHADOW_ATLAS_ROWS >= Config::MAX_SHADOW_CASTERS_2D,
              "Shadow atlas grid too small for MAX_SHADOW_CASTERS_2D");

GLShadowAtlas::GLShadowAtlas()  = default;
GLShadowAtlas::~GLShadowAtlas() = default;

void GLShadowAtlas::init(uint32_t tileRes) {
    // Already built at this resolution - nothing to do. A different resolution
    // falls through and rebuilds the 2D atlas below.
    if (m_atlas2D && m_tileRes == tileRes) return;

    m_tileRes = tileRes;

    const uint32_t atlasW = SHADOW_ATLAS_COLS * m_tileRes;
    const uint32_t atlasH = SHADOW_ATLAS_ROWS * m_tileRes;

    // One depth texture for the whole 2D atlas. Nearest filtering - PCF is done
    // in the shader; clamp so off-tile samples read the edge. Reassigning the
    // unique_ptr frees any previous atlas, so a resolution change is a rebuild.
    Core::Texture2DParams params;
    params.width          = atlasW;
    params.height         = atlasH;
    params.internalFormat = GL_DEPTH_COMPONENT24;
    params.format         = GL_DEPTH_COMPONENT;
    params.type           = GL_FLOAT;
    params.minFilter      = Core::TextureMinFilter::Nearest;
    params.magFilter      = Core::TextureMagFilter::Nearest;
    params.wrapS          = Core::TextureWrap::ClampToEdge;
    params.wrapT          = Core::TextureWrap::ClampToEdge;
    params.generateMipmaps = false;
    m_atlas2D = std::make_unique<Core::Texture2D>("shadow_atlas_2d", params);

    m_fbo2D.bind();
    m_fbo2D.attachTexture2D(GL_DEPTH_ATTACHMENT, m_atlas2D->getID());
    m_fbo2D.setDrawBuffer(GL_NONE);
    m_fbo2D.setReadBuffer(GL_NONE);
    if (!m_fbo2D.isComplete()) {
        LOG_ERROR("GLShadowAtlas 2D framebuffer incomplete (%ux%u)", atlasW, atlasH);
    }
    m_fbo2D.unbind();

    // Cube maps are tile-res-independent (they use SHADOW_CUBE_RES), so build the
    // set and its depth-only FBO once - a 2D resolution change leaves them be.
    if (m_cubes.empty()) {
        for (uint32_t i = 0; i < Config::MAX_SHADOW_CASTERS_CUBE; ++i) {
            auto cube = std::make_unique<Core::TextureCube>();
            cube->create(static_cast<int>(SHADOW_CUBE_RES), 1,
                         GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, false);
            m_cubes.push_back(std::move(cube));
        }

        // Depth-only cube FBO: no color buffer. Set once; the draw/read buffer
        // state persists across the per-face attachments done in beginCubeFace.
        m_fboCube.bind();
        m_fboCube.setDrawBuffer(GL_NONE);
        m_fboCube.setReadBuffer(GL_NONE);
        m_fboCube.unbind();
    }
}

void GLShadowAtlas::begin2D(const Core::Context& gl) const {
    m_fbo2D.bind();
    const int32_t atlasW = static_cast<int32_t>(SHADOW_ATLAS_COLS * m_tileRes);
    const int32_t atlasH = static_cast<int32_t>(SHADOW_ATLAS_ROWS * m_tileRes);
    gl.setViewport(0, 0, atlasW, atlasH);
    gl.clear(false, true, false);
}

void GLShadowAtlas::setTileViewport(const Core::Context& gl, uint32_t slot) const {
    const int32_t tile = static_cast<int32_t>(m_tileRes);
    const int32_t x    = static_cast<int32_t>(slot % SHADOW_ATLAS_COLS) * tile;
    const int32_t y    = static_cast<int32_t>(slot / SHADOW_ATLAS_COLS) * tile;
    gl.setViewport(x, y, tile, tile);
}

void GLShadowAtlas::beginCubeFace(const Core::Context& gl, uint32_t slot, uint32_t face) const {
    if (slot >= m_cubes.size()) return;
    m_fboCube.bind();
    m_cubes[slot]->attachFace(GL_DEPTH_ATTACHMENT, static_cast<int>(face), 0);
    gl.setViewport(0, 0, static_cast<int32_t>(SHADOW_CUBE_RES), static_cast<int32_t>(SHADOW_CUBE_RES));
    gl.clear(false, true, false);
}

void GLShadowAtlas::bind2D(uint32_t unit) const {
    if (m_atlas2D) m_atlas2D->bindSlot(unit);
}

void GLShadowAtlas::bindCube(uint32_t slot, uint32_t unit) const {
    if (slot < m_cubes.size()) m_cubes[slot]->bindSlot(unit);
}

glm::vec2 GLShadowAtlas::tileUVOffset(uint32_t slot) {
    const uint32_t col = slot % SHADOW_ATLAS_COLS;
    const uint32_t row = slot / SHADOW_ATLAS_COLS;
    return glm::vec2(static_cast<float>(col) / static_cast<float>(SHADOW_ATLAS_COLS),
                     static_cast<float>(row) / static_cast<float>(SHADOW_ATLAS_ROWS));
}

glm::vec2 GLShadowAtlas::tileUVScale() {
    return glm::vec2(1.0f / static_cast<float>(SHADOW_ATLAS_COLS), 1.0f / static_cast<float>(SHADOW_ATLAS_ROWS));
}

} // namespace Engine
