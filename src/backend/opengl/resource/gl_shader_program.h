#pragma once

#include <string>
#include <vector>

#include "gl_shader.h"             // Core::Shader (vkmGL)
#include "resource/shader_asset.h"

namespace Engine {

/**
 * @brief OpenGL representation of a ShaderAsset, kept in sync via GLView.
 *
 * Inherits from Core::Shader so render passes can call bind/setUniform*
 * directly on the GLShader pointer - the wrapper only adds asset-aware
 * behaviour on top of the raw shader: sampler->slot bindings get applied
 * after every (re)compile, which means hot reload survives without each
 * pass having to re-apply them manually.
 *
 * Same lifecycle as GLMesh / GLMaterial / GLTexture - GLView owns one
 * per ShaderAsset handle and calls update() when the asset's version
 * bumps.
 */
class GLShader : public Core::Shader {
    public:
        explicit GLShader(const ShaderAsset& asset);
        /**
         * @brief Variant construction: same shader source as @p asset, plus a
         *
         * list of preprocessor #defines injected below the #version line.
         * Used by the per-material variant cache to gate optional PBR
         * features at compile time.
         */
        GLShader(const ShaderAsset& asset, std::vector<std::string> defines);
        ~GLShader() override;

        GLShader(const GLShader& other) = delete;
        GLShader& operator=(const GLShader& other) = delete;

        GLShader(GLShader && other) = delete;
        GLShader& operator=(GLShader && other) = delete;

        /// Recompile + re-apply sampler bindings. Catches compile errors so
        /// a bad edit doesn't propagate - the next successful edit recovers.
        void update(const ShaderAsset& asset);

        /// The variant defines this program was compiled with (empty for
        /// the default ubershader). The cache keys on the hash of these.
        const std::vector<std::string>& getDefines() const { return m_defines; }

    protected:
        /**
         * @brief Re-run the engine-side preprocessor so hot reload picks up edits
         *
         * to included files too, not just the top-level vert/frag shader.
         * Honors the variant's saved defines so the rebuilt program stays
         * the same variant after a source edit.
         */
        void reloadSource() override;

    private:
        void applySamplerBindings(const ShaderAsset& asset);

        /// Read vert/frag/geom from disk, resolve their `#include`s, and
        /// inject the supplied #defines below the #version line. Returns
        /// a GraphicsShaderSource ready to compile.
        static Core::GraphicsShaderSource preprocessSourceFor(
            const std::string& dirPath,
            const std::vector<std::string>& defines);

        std::vector<std::string> m_defines;  ///< Variant-defining #defines (empty = ubershader).
};

} // namespace Engine
