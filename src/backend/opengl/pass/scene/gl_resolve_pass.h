#pragma once

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Resolves the multisample scene target into the single-sample sceneHDR for sampling.
 *
 * Geometry scope runs after the prepass; Colour after lighting, re-resolving depth when
 * alpha-masked geometry wrote it. A no-op when MSAA is off.
 *
 * A fullscreen draw, not a blit, which averages: depth and G-buffer take one sample (an average
 * of two surfaces is neither); colour averages through a tonemap so a bright sample cannot
 * dominate. One draw also writes every image of a scope.
 */
class GLResolvePass : public GLPass {
    public:
        enum class Scope {
            Geometry,  ///< Depth + G-buffer, for the screen-space passes.
            Color,     ///< The lit HDR colour and the reflection inputs, for the post chain.
        };

        explicit GLResolvePass(Scope scope);
        ~GLResolvePass() override;

        GLResolvePass(const GLResolvePass& other) = delete;
        GLResolvePass& operator=(const GLResolvePass& other) = delete;

        GLResolvePass(GLResolvePass && other) = delete;
        GLResolvePass& operator=(GLResolvePass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Scope           m_scope;
        Vkm::GL::Shader m_shader;  ///< The scope's own resolve program.
};

} // namespace Vkm::Engine
