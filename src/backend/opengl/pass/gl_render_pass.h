#pragma once

#include <string>

#include "system/render/render_pass.h"

namespace Engine {

class GLBackend;

/**
 * @brief OpenGL base for render passes: resolves the typed backend once.
 *
 * Every GL pass needs the same preamble - a per-pass GPU profile zone, a
 * backend-type guard, and the downcast to GLBackend. GLRenderPass performs
 * all three in a final execute() and dispatches to executeGL() with the
 * concrete backend already in hand, so subclasses implement executeGL()
 * instead of execute(). A non-OpenGL backend logs and skips the pass.
 */
class GLRenderPass : public RenderPass {
    public:
        GLRenderPass() = delete;
        ~GLRenderPass() override = default;

        GLRenderPass(const GLRenderPass& other) = delete;
        GLRenderPass& operator=(const GLRenderPass& other) = delete;

        GLRenderPass(GLRenderPass && other) = delete;
        GLRenderPass& operator=(GLRenderPass && other) = delete;

        GLRenderPass(const std::string& name) : RenderPass(name) {}

    public:
        /// Opens the per-pass GPU profile zone, guards the backend type, and
        /// downcasts to GLBackend, then calls executeGL. Not overridable.
        void execute(RenderGraphContext& ctx) final;

    protected:
        /// Pass body with the typed backend already resolved. Each concrete
        /// GL pass implements this in place of execute().
        virtual void executeGL(GLBackend& gl, RenderGraphContext& ctx) = 0;
};

} // namespace Engine
