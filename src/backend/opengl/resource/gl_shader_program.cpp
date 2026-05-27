#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_shader_program.h"

#include <filesystem>

#include "logger.h"

#include "debug/shader_error_log.h"
#include "loader/shader_preprocessor.h"

namespace Engine {

namespace {
namespace fs = std::filesystem;
}

GLShader::GLShader(const ShaderAsset& asset)
    : Core::Shader(preprocessSourceFor(asset.path, {}))
    , m_defines()
{
    applySamplerBindings(asset);
}

GLShader::GLShader(const ShaderAsset& asset, std::vector<std::string> defines)
    : Core::Shader(preprocessSourceFor(asset.path, defines))
    , m_defines(std::move(defines))
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
        LOG_INFO("Recompiled '%s'", getName().c_str());
        ShaderErrorLog::get().clearFor(getName());
    } catch (const std::exception& e) {
        LOG_ERROR("Recompile '%s' failed: %s",
            getName().c_str(), e.what());
        ShaderErrorLog::get().push(getName(), /*defines=*/{}, e.what());
    }
}

void GLShader::reloadSource() {
    // Re-run the include preprocessor instead of falling back to
    // Core::Shader's path-based reload. Touch on any of the source files
    // (top-level or included) and the next compile will pick up the edit.
    // Carry forward this variant's #defines so a hot reload of the base
    // shader source doesn't accidentally rebuild as the ubershader.
    m_source = preprocessSourceFor(getPath(), m_defines);
}

void GLShader::applySamplerBindings(const ShaderAsset& asset) {
    if (asset.samplerBindings.empty()) return;
    bind();
    for (const auto& [uniformName, slot] : asset.samplerBindings) {
        // Per-material variants legitimately strip samplers whose feature
        // wasn't enabled at compile time (e.g. u_sceneColor when the variant
        // has no HAS_TRANSMISSION). Skip silently instead of triggering
        // vkmGL's "uniform does not exist" warning per stripped slot.
        if (hasUniform(uniformName.c_str())) {
            setUniform1i(uniformName.c_str(), slot);
        }
    }
}

Core::GraphicsShaderSource GLShader::preprocessSourceFor(
    const std::string& dirPath,
    const std::vector<std::string>& defines)
{
    const fs::path dir(dirPath);
    const std::string vsPath = (dir / "vertex.shader").string();
    const std::string fsPath = (dir / "fragment.shader").string();
    const std::string gsPath = (dir / "geometry.shader").string();

    std::string vs = preprocessShaderFile(vsPath, defines);
    std::string fs = preprocessShaderFile(fsPath, defines);
    std::string gs;
    if (std::filesystem::exists(gsPath)) {
        gs = preprocessShaderFile(gsPath, defines);
    }
    return Core::GraphicsShaderSource(dirPath, std::move(vs), std::move(fs), std::move(gs));
}

} // namespace Engine
