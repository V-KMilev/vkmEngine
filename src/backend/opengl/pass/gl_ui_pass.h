#pragma once

#include <cstdint>
#include <memory>

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::GL {
    class Texture2D;
    class VertexArray;
}

namespace Vkm::Engine {

struct UIRect;
struct TextureAsset;
template <typename ResourceType> struct Handle;

/**
 * @brief Draws the UI overlay on top of the composited scene.
 *
 * Writes the frame's UIDrawData into the frame's stream and draws it alpha-blended, depth off,
 * over the viewport rect, each command scissored to its own rect. A no-op when the draw list is
 * empty. Two run: the game's UI before the splash, and the loading screen (RenderView::cover)
 * after it.
 */
class GLUIPass : public GLPass {
    public:
        /// Which of the frame's draw lists the pass draws.
        enum class List : uint8_t {
            Main,   ///< RenderView::ui: the game's own.
            Cover,  ///< RenderView::cover, over the splash.
        };

        explicit GLUIPass(List list = List::Main);
        ~GLUIPass() override;

        GLUIPass(const GLUIPass& other) = delete;
        GLUIPass& operator=(const GLUIPass& other) = delete;

        GLUIPass(GLUIPass && other) = delete;
        GLUIPass& operator=(GLUIPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

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
        List                                  m_list;
        Vkm::GL::Shader                       m_shader;
        std::unique_ptr<Vkm::GL::VertexArray> m_vao;  ///< The formats; the frame's stream holds the vertices.

        /**
         * @brief One texel of zero distance, bound where a command has no atlas.
         *
         * A glyph whose atlas is not yet on the GPU reads zero coverage and draws nothing, while
         * solids in the same run still draw.
         */
        std::unique_ptr<Vkm::GL::Texture2D> m_noAtlas;
};

} // namespace Vkm::Engine
