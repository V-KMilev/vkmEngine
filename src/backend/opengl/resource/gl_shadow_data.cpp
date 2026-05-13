#include "gl_shadow_data.h"

#include <cstring>

#include "logger.h"

#include "gl_uniform_buffer.h"

namespace Engine {

static_assert(sizeof(ShadowCasterGPUData) == 80, "ShadowCasterGPUData must be 80 bytes (std140 packing)");
static_assert(offsetof(ShadowCasterGPUData, lightSpace) == 0,  "lightSpace std140 offset");
static_assert(offsetof(ShadowCasterGPUData, params)     == 64, "params std140 offset");
static_assert(offsetof(ShadowUBOData, casters) == 16, "casters array follows std140-padded casterCount");

GLShadowData::GLShadowData() {
    // Eager allocation: keeps binding point 3 valid for the forward pass even
    // before the shadow pass runs the first time (or if it's removed entirely).
    m_ubo = std::make_unique<Core::UniformBuffer>(
        &m_data, sizeof(ShadowUBOData), GL_DYNAMIC_DRAW);
    m_lastData = m_data;
}

GLShadowData::~GLShadowData() {
    m_ubo.reset();
    LOG_TRACE("Destroying GLShadowData");
}

void GLShadowData::clear() {
    m_data.casterCount = 0;
    // Zero out caster entries so memcmp-based dirty checks see stable bytes.
    for (auto& c : m_data.casters) c = ShadowCasterGPUData{};
}

bool GLShadowData::addCaster(const ShadowCasterGPUData& caster) {
    if (m_data.casterCount >= static_cast<int>(GLConfig::Limits::MaxShadowCasters)) {
        return false;
    }
    m_data.casters[m_data.casterCount++] = caster;
    return true;
}

void GLShadowData::upload() {
    if (std::memcmp(&m_data, &m_lastData, sizeof(ShadowUBOData)) == 0) return;
    m_ubo->update(&m_data, sizeof(ShadowUBOData));
    m_lastData = m_data;
}

void GLShadowData::bind(uint32_t bindingPoint) const {
    if (m_ubo) m_ubo->bindBase(bindingPoint);
}

} // namespace Engine
