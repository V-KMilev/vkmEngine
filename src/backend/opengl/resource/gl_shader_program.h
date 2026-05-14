#pragma once

#include "gl_shader.h"             // Core::Shader (vkmGL)
#include "resource/shader_asset.h"

namespace Engine {

/**
 * @brief OpenGL representation of a ShaderAsset, kept in sync via GLView.
 *
 * Inherits from Core::Shader so render passes can call bind/setUniform*
 * directly on the GLShader pointer — the wrapper only adds asset-aware
 * behaviour on top of the raw shader: sampler→slot bindings get applied
 * after every (re)compile, which means hot reload survives without each
 * pass having to re-apply them manually.
 *
 * Same lifecycle as GLMesh / GLMaterial / GLTexture — GLView owns one
 * per ShaderAsset handle and calls update() when the asset's version
 * bumps.
 */
class GLShader : public Core::Shader {
    public:
        explicit GLShader(const ShaderAsset& asset);
        ~GLShader() override;

        GLShader(const GLShader& other) = delete;
        GLShader& operator=(const GLShader& other) = delete;

        GLShader(GLShader && other) = delete;
        GLShader& operator=(GLShader && other) = delete;

        /// Recompile + re-apply sampler bindings. Catches compile errors so
        /// a bad edit doesn't propagate — the next successful edit recovers.
        void update(const ShaderAsset& asset);

    private:
        void applySamplerBindings(const ShaderAsset& asset);
};

} // namespace Engine
