#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_ibl.h"

#include "logger.h"

namespace Vkm::Engine {

void GLIBL::createTargets() {
    if (m_captureFbo) return;  // already allocated

    m_captureFbo = std::make_unique<Vkm::GL::FrameBuffer>();

    m_envCube.create(ENV_SIZE,        ENV_MIPS,       GL_RGB16F, GL_RGB, GL_FLOAT, true);
    m_irradiance.create(IRRADIANCE_SIZE, 1,           GL_RGB16F, GL_RGB, GL_FLOAT, false);
    m_prefilter.create(PREFILTER_SIZE,  PREFILTER_MIPS, GL_RGB16F, GL_RGB, GL_FLOAT, true);

    Vkm::GL::Texture2DParams brdf;
    brdf.width           = BRDF_SIZE;
    brdf.height          = BRDF_SIZE;
    brdf.internalFormat  = GL_RG16F;
    brdf.format          = GL_RG;
    brdf.type            = GL_FLOAT;
    brdf.wrapS           = Vkm::GL::TextureWrap::ClampToEdge;
    brdf.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
    brdf.minFilter       = Vkm::GL::TextureMinFilter::Linear;
    brdf.magFilter       = Vkm::GL::TextureMagFilter::Linear;
    brdf.generateMipmaps = false;
    m_brdf = std::make_unique<Vkm::GL::Texture2D>("ibl_brdf_lut", brdf);

    LOG_INFO("Targets allocated (env %d, irr %d, prefilter %d/%d mips, brdf %d)",
        ENV_SIZE, IRRADIANCE_SIZE, PREFILTER_SIZE, PREFILTER_MIPS, BRDF_SIZE);
}

void GLIBL::uploadEquirect(uint32_t width, uint32_t height, const float* rgb) {
    Vkm::GL::Texture2DParams p;
    p.width           = width;
    p.height          = height;
    p.internalFormat  = GL_RGB16F;
    p.format          = GL_RGB;
    p.type            = GL_FLOAT;
    p.wrapS           = Vkm::GL::TextureWrap::ClampToEdge;
    p.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
    p.minFilter       = Vkm::GL::TextureMinFilter::Linear;
    p.magFilter       = Vkm::GL::TextureMagFilter::Linear;
    p.generateMipmaps = false;
    p.data            = rgb;
    m_equirect = std::make_unique<Vkm::GL::Texture2D>("ibl_equirect", p);
}

} // namespace Vkm::Engine
