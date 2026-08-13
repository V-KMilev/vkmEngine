#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_irradiance_volume.h"

#include "texture/gl_texture_3d.h"

namespace Engine {

GLIrradianceVolume::GLIrradianceVolume()  = default;
GLIrradianceVolume::~GLIrradianceVolume() = default;

void GLIrradianceVolume::resize(uint32_t x, uint32_t y, uint32_t z) {
    if (x == 0 || y == 0 || z == 0) return;
    if (x == m_x && y == m_y && z == m_z && m_ready) return;

    m_x = x;
    m_y = y;
    m_z = z;

    for (int i = 0; i < SH_COEFFS; ++i) {
        Core::Texture3DParams p;
        p.width  = x;
        p.height = y;
        p.depth  = z;
        p.internalFormat = GL_RGBA16F;
        p.format         = GL_RGBA;
        p.type           = GL_FLOAT;
        // Linear so the lookup blends between neighbouring probes; clamp so a
        // sample at the box edge holds the border probe instead of wrapping.
        p.minFilter = Core::TextureMinFilter::Linear;
        p.magFilter = Core::TextureMagFilter::Linear;
        p.wrap      = Core::TextureWrap::ClampToEdge;
        m_sh[i] = std::make_unique<Core::Texture3D>("irradiance_sh", p);
    }

    // Contents are undefined until a bake fills every cell.
    m_ready = false;
}

void GLIrradianceVolume::bindImage(int i, uint32_t unit, GLenum access) const {
    if (i >= 0 && i < SH_COEFFS && m_sh[i]) m_sh[i]->bindImage(unit, access);
}

void GLIrradianceVolume::bindSlot(int i, uint32_t slot) const {
    if (i >= 0 && i < SH_COEFFS && m_sh[i]) m_sh[i]->bindSlot(slot);
}

} // namespace Engine
