#include "resource/gl_shader_program.h"

#include <filesystem>

#include "logger.h"

#include "loader/shader_preprocessor.h"

namespace Engine {

namespace {
namespace fs = std::filesystem;
}

GLShader::GLShader(const ShaderAsset& asset)
    : Core::Shader(preprocessSourceFor(asset.path))
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

void GLShader::reloadSource() {
    // Re-run the include preprocessor instead of falling back to
    // Core::Shader's path-based reload. Touch on any of the source files
    // (top-level or included) and the next compile will pick up the edit.
    m_source = preprocessSourceFor(getPath());
}

void GLShader::applySamplerBindings(const ShaderAsset& asset) {
    if (asset.samplerBindings.empty()) return;
    bind();
    for (const auto& [uniformName, slot] : asset.samplerBindings) {
        setUniform1i(uniformName.c_str(), slot);
    }
}

Core::GraphicsShaderSource GLShader::preprocessSourceFor(const std::string& dirPath) {
    const fs::path dir(dirPath);
    const std::string vsPath = (dir / "vertexShader.shader").string();
    const std::string fsPath = (dir / "fragmentShader.shader").string();
    const std::string gsPath = (dir / "geometryShader.shader").string();

    std::string vs = preprocessShaderFile(vsPath);
    std::string fs = preprocessShaderFile(fsPath);
    std::string gs;
    if (std::filesystem::exists(gsPath)) {
        gs = preprocessShaderFile(gsPath);
    }
    return Core::GraphicsShaderSource(dirPath, std::move(vs), std::move(fs), std::move(gs));
}

} // namespace Engine
