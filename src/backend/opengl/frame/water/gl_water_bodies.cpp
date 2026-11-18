#include "frame/water/gl_water_bodies.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "vkm_opengl/buffer/shader_storage_buffer.h"

#include "convention/gl_bindings.h"

namespace Vkm::Engine {

GLWaterBodies::GLWaterBodies()  = default;
GLWaterBodies::~GLWaterBodies() = default;

void GLWaterBodies::update(const std::vector<Water>& waters) {
    // Never empty, so the table always exists to be bound.
    m_scratch.assign(std::max<size_t>(waters.size(), 1), WaterBodyGPU{});
    for (size_t i = 0; i < waters.size(); ++i) {
        const Water& water = waters[i];
        m_scratch[i].shares = glm::vec4(water.waves, water.ripples, water.flow, water.foam);
        m_scratch[i].shore  = glm::vec4(water.waterline, 0.0f, 0.0f, 0.0f);
    }
    // Compared as uploaded, so whatever a Water adds reaches the GPU the day it is packed here.
    const auto bytes = static_cast<uint32_t>(m_scratch.size() * sizeof(WaterBodyGPU));
    const bool held  = m_buffer && m_held.size() == m_scratch.size()
        && std::memcmp(m_held.data(), m_scratch.data(), bytes) == 0;
    if (!held) {
        if (m_buffer && m_buffer->getSize() == bytes) m_buffer->update(m_scratch.data(), bytes);
        else m_buffer = std::make_unique<Vkm::OpenGL::ShaderStorageBuffer>(m_scratch.data(), bytes);
        std::swap(m_held, m_scratch);
    }
    m_buffer->bindBase(GLBindings::SSBOBindingPoints::WATER_BODIES);
}

} // namespace Vkm::Engine
