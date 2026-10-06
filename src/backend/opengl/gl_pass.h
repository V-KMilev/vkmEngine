#pragma once

#include <cstdint>

namespace Vkm::GL {
    class Context;
    class Shader;
    class ShaderBase;
}

namespace Vkm::Engine {

struct GLFrameContext;
struct RenderView;

/**
 * @brief One step of the OpenGL backend's frame.
 *
 * The backend runs an ordered list of these each frame, not a render graph; a pass reads what
 * it needs from GLFrameContext.
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
         * @brief Move the scene into the colour chain if it is still on the geometry target.
         *
         * An overlay samples the geometry target's depth and G-buffer, so drawing there would be
         * read-while-write feedback. Callers blend into ctx.colorSrc afterwards and do not flip.
         *
         * @param ctx Whose colour chain is promoted.
         */
        void promoteColorChain(GLFrameContext& ctx) const;

        /**
         * @brief Bind the default framebuffer with the view's window-space viewport rect.
         *
         * @param ctx Whose view supplies the rect.
         */
        void bindBackbufferViewport(GLFrameContext& ctx) const;

        /**
         * @brief The bottom edge, in default-framebuffer pixels, of a row span of the viewport.
         *
         * Spans arrive top-left origin but GL counts from the bottom-left, so each flips against
         * the full surface height, or it lands mirrored off the editor's viewport panel.
         *
         * @param view   Supplies the viewport rect and surface height.
         * @param top    First row, in pixels down from the viewport's top.
         * @param height Height in pixels.
         * @return Lowest row, counted up from the surface's bottom.
         */
        static int32_t backbufferBottom(const RenderView& view, int32_t top, int32_t height);

        /**
         * @brief Give the bound @p shader this frame's froxel fog, or tell it there is none.
         *
         * For a shader including shaders/fog.glsl: binds the volume only when the fog compute
         * filled it this frame, and sets u_hasFog to match.
         *
         * @param ctx    For the fog volume and whether it is ready.
         * @param shader The program that draws; it must be bound.
         */
        void bindFog(GLFrameContext& ctx, const Vkm::GL::Shader& shader) const;

        /**
         * @brief Give the bound @p shader this frame's GTAO, or tell it there is none.
         *
         * Binds the AO target only when the GTAO pass filled it this frame, and sets u_hasAO to
         * match.
         *
         * @param ctx     For the AO target and whether it is ready.
         * @param shader  The program that draws; it must be bound.
         * @param sampled False tells the shader there is none even so: a draw GTAO did not see.
         */
        void bindAO(GLFrameContext& ctx, const Vkm::GL::Shader& shader, bool sampled = true) const;

        /**
         * @brief Give the bound @p shader the sky's irradiance cube and the baked irradiance volume.
         *
         * For a shader including shaders/ambient.glsl. Binds what the frame has and sets
         * u_hasIBL, u_hasIrradianceVolume, the intensity and the volume's box to match.
         *
         * @param ctx    For the baked environment and the volume.
         * @param shader The program that draws or dispatches; it must be bound.
         */
        void bindAmbient(GLFrameContext& ctx, const Vkm::GL::ShaderBase& shader) const;

        /**
         * @brief Let draws write the roughness beside the reflection weight, or not.
         *
         * A blended surface writes zero into the reflection inputs at its opacity, dimming the
         * surface behind's reflection; that surface's roughness must not dim, so blended draws
         * mask it. GLBackend::render does not reset a colour mask, and one left on would stop
         * next frame's clear, so a pass that masks it unmasks it before returning.
         *
         * @param writable False to protect the roughness, true to put it back.
         */
        void setReflectRoughnessWritable(bool writable) const;
};

} // namespace Vkm::Engine
