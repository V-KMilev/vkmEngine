#include "storage/water/gl_water_shore.h"

#include <GL/glew.h>

#include "convention/gl_bindings.h"

namespace Vkm::Engine {

void GLWaterShore::allocate(uint32_t size) {
    if (m_map) return;
    const Vkm::OpenGL::Texture2DParams params = Vkm::OpenGL::renderTargetParams(
        size,
        size,
        GL_R16F,
        GL_RED,
        Vkm::OpenGL::TextureMinFilter::Linear,
        Vkm::OpenGL::TextureMagFilter::Linear
    );
    m_map = std::make_unique<Vkm::OpenGL::Texture2D>("water_shore", params);
}

void GLWaterShore::bindMapImage(uint32_t unit, GLenum access) const {
    m_map->bindImage(unit, access);
}

void GLWaterShore::bind() const {
    if (m_shored) m_map->bindSlot(GLBindings::WaterTextureSlots::SHORE);
}

} // namespace Vkm::Engine
