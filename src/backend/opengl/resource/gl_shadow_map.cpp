#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_shadow_map.h"

#include <GL/glew.h>

#include "logger.h"

#include "gl_error_handle.h"
#include "gl_frame_buffer.h"

namespace Engine {

namespace {

void configureCompareSampling(GLenum target) {
    VKM_GL_CHECK(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    VKM_GL_CHECK(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    VKM_GL_CHECK(glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE));
    VKM_GL_CHECK(glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_LESS));
}

} // namespace

GLShadowAtlas::GLShadowAtlas(
    uint32_t resolution2D,
    uint32_t resolutionCube,
    uint32_t max2DCasters,
    uint32_t maxCubeCasters
)
    : m_res2D(resolution2D)
    , m_resCube(resolutionCube)
    , m_max2D(max2DCasters)
    , m_maxCube(maxCubeCasters)
{
    // 2D array depth texture - directional + spot maps live here, one per layer.
    VKM_GL_CHECK(glGenTextures(1, &m_tex2D));
    VKM_GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex2D));
    VKM_GL_CHECK(glTexImage3D(
        GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
        static_cast<GLsizei>(m_res2D),
        static_cast<GLsizei>(m_res2D),
        static_cast<GLsizei>(m_max2D),
        0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
    ));
    configureCompareSampling(GL_TEXTURE_2D_ARRAY);
    VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
    VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
    const float borderColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    VKM_GL_CHECK(glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor));
    VKM_GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));

    // Cube array depth texture - 6 layers per point light cube.
    if (m_maxCube > 0) {
        VKM_GL_CHECK(glGenTextures(1, &m_texCube));
        VKM_GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_texCube));
        VKM_GL_CHECK(glTexImage3D(
            GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT24,
            static_cast<GLsizei>(m_resCube),
            static_cast<GLsizei>(m_resCube),
            static_cast<GLsizei>(m_maxCube * 6),
            0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
        ));
        configureCompareSampling(GL_TEXTURE_CUBE_MAP_ARRAY);
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
        VKM_GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, 0));
    }

    // Single depth-only FBO. The attachment is re-pointed per draw via
    // glFramebufferTextureLayer; depth-only so color buffers are explicitly off.
    m_fbo = std::make_unique<Core::FrameBuffer>();
    m_fbo->bind();
    VKM_GL_CHECK(glDrawBuffer(GL_NONE));
    VKM_GL_CHECK(glReadBuffer(GL_NONE));
    m_fbo->unbind();

    // Compare-off sampler for PCSS blocker search. Linear filter still wanted
    // (the search averages 12+ taps; bilinear free-samples per tap), but the
    // shader is reading raw depth here so GL_TEXTURE_COMPARE_MODE must be off.
    // A sampler-object override leaves the texture's own compare state intact
    // so the regular sampler2DArrayShadow / samplerCubeArrayShadow paths keep
    // their hardware PCF.
    VKM_GL_CHECK(glGenSamplers(1, &m_samplerDepthNoCompare));
    VKM_GL_CHECK(glSamplerParameteri(m_samplerDepthNoCompare, GL_TEXTURE_COMPARE_MODE, GL_NONE));
    VKM_GL_CHECK(glSamplerParameteri(m_samplerDepthNoCompare, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    VKM_GL_CHECK(glSamplerParameteri(m_samplerDepthNoCompare, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    VKM_GL_CHECK(glSamplerParameteri(m_samplerDepthNoCompare, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    VKM_GL_CHECK(glSamplerParameteri(m_samplerDepthNoCompare, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    VKM_GL_CHECK(glSamplerParameteri(m_samplerDepthNoCompare, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
}

void GLShadowAtlas::ensureResolution(uint32_t res2D, uint32_t resCube) {
    if (res2D == 0)   res2D   = m_res2D;
    if (resCube == 0) resCube = m_resCube;
    if (res2D == m_res2D && resCube == m_resCube) return;

    LOG_INFO("ShadowAtlas: resizing 2D %u->%u, cube %u->%u",
        m_res2D, res2D, m_resCube, resCube);

    // Reallocate the 2D array.
    if (m_tex2D != 0) {
        VKM_GL_CHECK(glDeleteTextures(1, &m_tex2D));
        m_tex2D = 0;
    }
    m_res2D = res2D;
    VKM_GL_CHECK(glGenTextures(1, &m_tex2D));
    VKM_GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex2D));
    VKM_GL_CHECK(glTexImage3D(
        GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
        static_cast<GLsizei>(m_res2D),
        static_cast<GLsizei>(m_res2D),
        static_cast<GLsizei>(m_max2D),
        0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
    ));
    configureCompareSampling(GL_TEXTURE_2D_ARRAY);
    VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
    VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
    const float borderColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    VKM_GL_CHECK(glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor));
    VKM_GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));

    // Reallocate the cube array.
    if (m_texCube != 0) {
        VKM_GL_CHECK(glDeleteTextures(1, &m_texCube));
        m_texCube = 0;
    }
    m_resCube = resCube;
    if (m_maxCube > 0) {
        VKM_GL_CHECK(glGenTextures(1, &m_texCube));
        VKM_GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_texCube));
        VKM_GL_CHECK(glTexImage3D(
            GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT24,
            static_cast<GLsizei>(m_resCube),
            static_cast<GLsizei>(m_resCube),
            static_cast<GLsizei>(m_maxCube * 6),
            0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
        ));
        configureCompareSampling(GL_TEXTURE_CUBE_MAP_ARRAY);
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        VKM_GL_CHECK(glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
        VKM_GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, 0));
    }
}

GLShadowAtlas::~GLShadowAtlas() {
    if (m_tex2D != 0)   VKM_GL_CHECK(glDeleteTextures(1, &m_tex2D));
    if (m_texCube != 0) VKM_GL_CHECK(glDeleteTextures(1, &m_texCube));
    if (m_samplerDepthNoCompare != 0) {
        VKM_GL_CHECK(glDeleteSamplers(1, &m_samplerDepthNoCompare));
    }
    m_fbo.reset();
    LOG_TRACE("Destructed GLShadowAtlas");
}

void GLShadowAtlas::bind2DLayerForWriting(uint32_t layer) const {
    m_fbo->bind();
    VKM_GL_CHECK(glFramebufferTextureLayer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        m_tex2D, 0, static_cast<GLint>(layer)
    ));
    VKM_GL_CHECK(glViewport(0, 0,
        static_cast<GLsizei>(m_res2D),
        static_cast<GLsizei>(m_res2D)));
    VKM_GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));
}

void GLShadowAtlas::bindCubeFaceForWriting(uint32_t cubeIndex, uint32_t face) const {
    m_fbo->bind();
    VKM_GL_CHECK(glFramebufferTextureLayer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        m_texCube, 0,
        static_cast<GLint>(cubeIndex * 6u + face)
    ));
    VKM_GL_CHECK(glViewport(0, 0,
        static_cast<GLsizei>(m_resCube),
        static_cast<GLsizei>(m_resCube)));
    VKM_GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));
}

void GLShadowAtlas::unbindForWriting() const {
    m_fbo->unbind();
}

void GLShadowAtlas::bind2DForReading(uint32_t slot) const {
    VKM_GL_CHECK(glActiveTexture(GL_TEXTURE0 + slot));
    VKM_GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex2D));
}

void GLShadowAtlas::bindCubeForReading(uint32_t slot) const {
    VKM_GL_CHECK(glActiveTexture(GL_TEXTURE0 + slot));
    VKM_GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_texCube));
}

void GLShadowAtlas::bind2DForReadingDepth(uint32_t slot) const {
    VKM_GL_CHECK(glActiveTexture(GL_TEXTURE0 + slot));
    VKM_GL_CHECK(glBindTexture(GL_TEXTURE_2D_ARRAY, m_tex2D));
    VKM_GL_CHECK(glBindSampler(slot, m_samplerDepthNoCompare));
}

void GLShadowAtlas::bindCubeForReadingDepth(uint32_t slot) const {
    VKM_GL_CHECK(glActiveTexture(GL_TEXTURE0 + slot));
    VKM_GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_texCube));
    VKM_GL_CHECK(glBindSampler(slot, m_samplerDepthNoCompare));
}

} // namespace Engine
