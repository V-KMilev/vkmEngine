#pragma once

#include "gl_pass.h"

namespace Engine {

/**
 * @brief Resolves the multisample scene target into the single-sample sceneHDR
 * so the screen-space + post passes can sample it.
 *
 * Two instances run per frame: Geometry (depth + G-buffer, right after the
 * prepass, before GTAO reads them) and Colour (after the forward pass, before
 * the decal pass; re-resolves depth too when alpha-masked geometry wrote it
 * during forward). A no-op when MSAA is off - the geometry passes then render
 * straight into sceneHDR and there is nothing to resolve.
 */
class GLResolvePass : public GLPass {
    public:
        enum class Scope {
            Geometry,  ///< Depth + G-buffer (read by GTAO / contact shadows / decals / composite).
            Color,     ///< The lit HDR colour (read by the post chain: decals, fog, DoF, bloom, composite).
        };

        explicit GLResolvePass(Scope scope) : m_scope(scope) {}
        ~GLResolvePass() override = default;

        GLResolvePass(const GLResolvePass& other) = delete;
        GLResolvePass& operator=(const GLResolvePass& other) = delete;

        GLResolvePass(GLResolvePass && other) = delete;
        GLResolvePass& operator=(GLResolvePass && other) = delete;

        void execute(GLFrameContext& ctx) override;

    private:
        Scope m_scope;
};

} // namespace Engine
