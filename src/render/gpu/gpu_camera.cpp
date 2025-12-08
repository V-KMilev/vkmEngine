#include "gpu_camera.h"

#include "cpu_camera.h"
#include "gl_shader.h"

namespace Engine {

GPUCamera::GPUCamera(
    const CPUCamera& cpuCamera
) : m_source(&cpuCamera) {}

void GPUCamera::setSource(const CPUCamera& cpuCamera) {
    m_source = &cpuCamera;
}

void GPUCamera::upload(const Core::Shader& shader) const {
    if (!m_source) {
        return;
    }

    shader.setUniformMatrix4fv("u_viewMatrix", m_source->getViewMatrix());
    shader.setUniformMatrix4fv("u_projectionMatrix", m_source->getProjectionMatrix());
    shader.setUniformMatrix4fv("u_viewProjectionMatrix", m_source->getViewProjectionMatrix());
}

} // namespace Engine

