#pragma once

#include <memory>

#include "vkm_opengl/shader/shader.h"

#include "pass/gl_pass.h"

namespace Vkm::OpenGL {
    class Texture2D;
    class VertexArray;
}

namespace Vkm::Engine {

struct UIRect;
struct UIDrawData;
struct TextureAsset;
template <typename ResourceType> struct Handle;

/**
 * @brief Draws the UI overlay on top of the composited scene.
 *
 * Writes the frame's UIDrawData into the frame's stream and draws it alpha-blended, depth off,
 * over the viewport rect, each command scissored to its own rect. A no-op when the draw list is
 * empty. It draws the game's UI (RenderView::ui) before the splash; the loading screen draws a
 * project's screen (RenderView::cover) through it after the splash (GLLoadingPass).
 */
class GLUIPass : public GLPass {
    public:
        GLUIPass();
        ~GLUIPass() override;

        GLUIPass(const GLUIPass& other) = delete;
        GLUIPass& operator=(const GLUIPass& other) = delete;

        GLUIPass(GLUIPass && other) = delete;
        GLUIPass& operator=(GLUIPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

        /**
         * @brief Draw @p list as execute() draws the game's UI.
         *
         * Leaves the scissor on, at the last command's clip.
         *
         * @param ctx  The frame.
         * @param list The draw list; null or empty draws nothing.
         */
        void draw(GLFrameContext& ctx, const UIDrawData* list);

    private:
        /**
         * @brief Bind a command's image to its unit, and say whether it is stored as sRGB.
         *
         * @param ctx   For the texture table.
         * @param image The run's image; empty when nothing in it samples one.
         */
        void bindImage(GLFrameContext& ctx, const Handle<TextureAsset>& image) const;

        /**
         * @brief Set the scissor box from a command's viewport-local clip rect.
         *
         * @param ctx  For the viewport rect the clip is relative to.
         * @param clip Clip rect in UI pixels, top-left origin.
         */
        void setScissorRect(GLFrameContext& ctx, const UIRect& clip) const;

    private:
        Vkm::OpenGL::Shader                       m_shader;
        std::unique_ptr<Vkm::OpenGL::VertexArray> m_vao;  ///< The formats; the frame's stream holds the
                                                          ///< vertices.

        std::unique_ptr<Vkm::OpenGL::Texture2D> m_noAtlas;  ///< One texel of zero distance, bound where a
                                                            ///< command has no atlas: a glyph whose atlas is
                                                            ///< not yet on the GPU draws nothing, while
                                                            ///< solids in the same run still draw.
};

} // namespace Vkm::Engine
