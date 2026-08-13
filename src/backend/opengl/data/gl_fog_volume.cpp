#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_fog_volume.h"

#include "texture/gl_texture_3d.h"

namespace Engine {

namespace {
std::unique_ptr<Core::Texture3D> makeVolume(const char* name, uint32_t x, uint32_t y, uint32_t z) {
    Core::Texture3DParams p;
    p.width  = x;
    p.height = y;
    p.depth  = z;
    p.internalFormat = GL_RGBA16F;
    p.format         = GL_RGBA;
    p.type           = GL_FLOAT;
    p.minFilter      = Core::TextureMinFilter::Linear;
    p.magFilter      = Core::TextureMagFilter::Linear;
    p.wrap           = Core::TextureWrap::ClampToEdge;
    return std::make_unique<Core::Texture3D>(name, p);
}
} // namespace

GLFogVolume::GLFogVolume()  = default;
GLFogVolume::~GLFogVolume() = default;

void GLFogVolume::resize(uint32_t x, uint32_t y, uint32_t z) {
    if (m_scatter && x == m_x && y == m_y && z == m_z) return;  // already this size
    m_x = x;
    m_y = y;
    m_z = z;
    m_scatter    = makeVolume("fog_scatter", x, y, z);
    m_integrated = makeVolume("fog_integrated", x, y, z);
}

void GLFogVolume::bindScatterImage(uint32_t unit, GLenum access) const {
    if (m_scatter) m_scatter->bindImage(unit, access);
}

void GLFogVolume::bindIntegratedImage(uint32_t unit, GLenum access) const {
    if (m_integrated) m_integrated->bindImage(unit, access);
}

void GLFogVolume::bindIntegratedSlot(uint32_t slot) const {
    if (m_integrated) m_integrated->bindSlot(slot);
}

} // namespace Engine
