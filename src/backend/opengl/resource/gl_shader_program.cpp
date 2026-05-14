#include "resource/gl_shader_program.h"

#include "logger.h"

namespace Engine {

GLShader::GLShader(const ShaderAsset& asset)
    : Core::Shader(asset.path)
{
    applySamplerBindings(asset);
}

GLShader::~GLShader() {
    LOG_TRACE("Destructed GLShader");
}

void GLShader::update(const ShaderAsset& asset) {
    try {
        recompile();
        applySamplerBindings(asset);
        LOG_INFO("GLShader: recompiled '%s'", getName().c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("GLShader: recompile '%s' failed: %s",
            getName().c_str(), e.what());
    }
}

void GLShader::applySamplerBindings(const ShaderAsset& asset) {
    if (asset.samplerBindings.empty()) return;
    bind();
    for (const auto& [uniformName, slot] : asset.samplerBindings) {
        setUniform1i(uniformName.c_str(), slot);
    }
}

} // namespace Engine
