#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "storage/gl_ibl.h"

#include "logger.h"

namespace Vkm::Engine {

void GLIBL::createTargets() {
    if (m_envCube.getID() != 0) return;
    if (!m_captureFbo) m_captureFbo = std::make_unique<Vkm::GL::FrameBuffer>();

    // RGBA16F: rendered into, and GL makes RGB16F colour-renderable only optionally. Not
    // R11F_G11F_B10F: this cube is the skybox, and a sky gradient bands at 6-bit mantissas.
    m_envCube.create(ENV_SIZE, ENV_MIPS, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);
    m_irradiance.create(IRRADIANCE_SIZE, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, false);
    m_prefilter.create(PREFILTER_SIZE, PREFILTER_MIPS, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);

    LOG_INFO(
        "Targets allocated (env %d, irr %d, prefilter %d/%d mips)",
        ENV_SIZE,
        IRRADIANCE_SIZE,
        PREFILTER_SIZE,
        PREFILTER_MIPS
    );
}

void GLIBL::createBrdf() {
    if (m_brdf) return;
    if (!m_captureFbo) m_captureFbo = std::make_unique<Vkm::GL::FrameBuffer>();

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
}

} // namespace Vkm::Engine
