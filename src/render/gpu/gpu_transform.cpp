#include "gpu_transform.h"
#include "cpu_transform.h"
#include "gl_shader.h"

namespace Engine {

GPUTransform::GPUTransform(
    const CPUTransform& cpuTransform
) : m_source(&cpuTransform) {
}

void GPUTransform::setSource(const CPUTransform& cpuTransform) {
    m_source = &cpuTransform;
}

void GPUTransform::upload(const Core::Shader& shader) const {
    if (!m_source) {
        return;
    }

    shader.setUniformMatrix4fv("u_modelMatrix", m_source->getModelMatrix());
}

} // namespace Engine
