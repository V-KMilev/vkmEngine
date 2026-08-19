#pragma once

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {
    struct GLFrameContext;
}

namespace Vkm::Engine {

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
        void beginFullscreen(Vkm::GL::Context& gl) const;

        /**
         * @brief Shared fullscreen-pass epilogue: re-enable depth testing (the
         * following geometry pass sets its own func / write / cull). Mirrors
         * beginFullscreen so the post passes don't each open-code the restore.
         */
        void endFullscreen(Vkm::GL::Context& gl) const;

        /**
         * @brief Move the scene into the colour chain if it is still on the
         * geometry target.
         *
         * An overlay pass blends over the current scene while sampling the
         * geometry target's depth / G-buffer, so it cannot draw into that
         * target - that would be read-while-write feedback. The chain scratches
         * are colour-only, so once the scene is on one there is nothing to do
         * and the blit is skipped. Callers blend into ctx.colorSrc afterwards
         * and do not flip: the promotion has already published the target they
         * draw into.
         *
         * @param ctx The frame context whose colour chain is promoted.
         */
        void promoteColorChain(GLFrameContext& ctx) const;

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

} // namespace Vkm::Engine
