#include "gl_ibl.h"

#include "logger.h"

namespace Engine {

void GLIBL::createTargets() {
    if (m_captureFbo) return;  // already allocated

    m_captureFbo = std::make_unique<Core::FrameBuffer>();

    m_envCube.create(ENV_SIZE,        ENV_MIPS,       GL_RGB16F, GL_RGB, GL_FLOAT, true);
    m_irradiance.create(IRRADIANCE_SIZE, 1,           GL_RGB16F, GL_RGB, GL_FLOAT, false);
    m_prefilter.create(PREFILTER_SIZE,  PREFILTER_MIPS, GL_RGB16F, GL_RGB, GL_FLOAT, true);

    Core::Texture2DParams brdf;
    brdf.width           = BRDF_SIZE;
    brdf.height          = BRDF_SIZE;
    brdf.internalFormat  = GL_RG16F;
    brdf.format          = GL_RG;
    brdf.type            = GL_FLOAT;
    brdf.wrapS           = Core::TextureWrap::ClampToEdge;
    brdf.wrapT           = Core::TextureWrap::ClampToEdge;
    brdf.minFilter       = Core::TextureMinFilter::Linear;
    brdf.magFilter       = Core::TextureMagFilter::Linear;
    brdf.generateMipmaps = false;
    m_brdf = std::make_unique<Core::Texture2D>("ibl_brdf_lut", brdf);

    LOG_INFO("GLIBL targets allocated (env %d, irr %d, prefilter %d/%d mips, brdf %d)",
        ENV_SIZE, IRRADIANCE_SIZE, PREFILTER_SIZE, PREFILTER_MIPS, BRDF_SIZE);
}

void GLIBL::uploadEquirect(uint32_t width, uint32_t height, const float* rgb) {
    Core::Texture2DParams p;
    p.width           = width;
    p.height          = height;
    p.internalFormat  = GL_RGB16F;
    p.format          = GL_RGB;
    p.type            = GL_FLOAT;
    p.wrapS           = Core::TextureWrap::ClampToEdge;
    p.wrapT           = Core::TextureWrap::ClampToEdge;
    p.minFilter       = Core::TextureMinFilter::Linear;
    p.magFilter       = Core::TextureMagFilter::Linear;
    p.generateMipmaps = false;
    p.data            = rgb;
    m_equirect = std::make_unique<Core::Texture2D>("ibl_equirect", p);
}

bool GLIBL::needsBake(const std::string& path) const {
    if (path.empty()) return false;
    return !m_ready || path != m_bakedPath;
}

void GLIBL::markBaked(const std::string& path) {
    m_bakedPath = path;
    m_ready = true;
}

} // namespace Engine
