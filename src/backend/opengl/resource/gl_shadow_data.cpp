#include "gl_shadow_data.h"

#include "logger.h"

#include "gl_uniform_buffer.h"

namespace Engine {

static_assert(sizeof(Shadow2DCasterGPU)   == 80, "Shadow2DCasterGPU must be 80 bytes (std140)");
static_assert(sizeof(ShadowCubeCasterGPU) == 16, "ShadowCubeCasterGPU must be 16 bytes (std140)");
static_assert(offsetof(Shadow2DCasterGPU, lightSpace) == 0,  "lightSpace std140 offset");
static_assert(offsetof(Shadow2DCasterGPU, params)     == 64, "params std140 offset");
static_assert(offsetof(ShadowUBOData, casters2D)   == 16, "casters2D std140 offset");

GLShadowData::GLShadowData() {
    m_ubo = std::make_unique<Core::UniformBuffer>(
        &m_data, sizeof(ShadowUBOData), GL_DYNAMIC_DRAW);
}

GLShadowData::~GLShadowData() {
    m_ubo.reset();
    LOG_TRACE("Destructed GLShadowData");
}

void GLShadowData::clear() {
    m_data.count2D   = 0;
    m_data.countCube = 0;
}

void GLShadowData::setCaster2D(uint32_t slot, const Shadow2DCasterGPU& caster) {
    if (slot >= Config::MaxShadowCasters2D) return;
    m_data.casters2D[slot] = caster;
}

void GLShadowData::setCasterCube(uint32_t slot, const ShadowCubeCasterGPU& caster) {
    if (slot >= Config::MaxShadowCastersCube) return;
    m_data.castersCube[slot] = caster;
}

void GLShadowData::setCounts(uint32_t count2D, uint32_t countCube) {
    m_data.count2D   = static_cast<int>(count2D);
    m_data.countCube = static_cast<int>(countCube);
}

void GLShadowData::uploadAndBind() {
    m_ubo->update(&m_data, sizeof(ShadowUBOData));
    m_ubo->bindBase(GLConfig::UBOBindingPoints::Shadow);
}

} // namespace Engine
