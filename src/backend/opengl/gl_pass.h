#pragma once

namespace Core {
    class Context;
}

namespace Engine {
    struct GLFrameContext;
}

namespace Engine {

/**
 * @brief One step of the OpenGL backend's frame.
 *
 * The backend owns an ordered list of these and runs them per frame. Kept
 * deliberately small: a pass reads what it needs from the per-frame
 * GLFrameContext (scene snapshot, GPU resource mirror, render targets) and
 * issues its draws - an ordered list, not a render graph.
 */
class GLPass {
    public:
        GLPass() = default;
        virtual ~GLPass() = default;

        GLPass(const GLPass& other) = delete;
        GLPass& operator=(const GLPass& other) = delete;

        GLPass(GLPass && other) = delete;
        GLPass& operator=(GLPass && other) = delete;

    public:
        virtual void execute(GLFrameContext& ctx) = 0;

    protected:
        /**
         * @brief Shared fullscreen-pass GL preamble: depth test, blending and face
         * culling all off. The post passes route through this so they can't
         * drift apart on which states they set.
         */
        void beginFullscreen(Core::Context& gl) const;

        /**
         * @brief Shared fullscreen-pass epilogue: re-enable depth testing (the
         * following geometry pass sets its own func / write / cull). Mirrors
         * beginFullscreen so the post passes don't each open-code the restore.
         */
        void endFullscreen(Core::Context& gl) const;

        /**
         * @brief Bind the default framebuffer with the view's window-space
         * viewport rect.
         *
         * viewportY arrives top-left origin (window/UI convention) while GL's
         * default framebuffer is bottom-left, so the rect is flipped against the
         * full surface height - otherwise output lands mirrored off the editor's
         * viewport panel. Shared by every pass that draws to the backbuffer
         * (composite, UI) so the flip lives in exactly one place.
         *
         * @param ctx The frame context whose view supplies the rect.
         */
        void bindBackbufferViewport(GLFrameContext& ctx) const;
};

} // namespace Engine
