#pragma once

#include <cstdint>
#include <memory>

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::GL {
    class Texture2D;
    class VertexArray;
    class VertexBuffer;
}

namespace Vkm::Engine {

struct UIRect;
struct TextureAsset;
template <typename ResourceType> struct Handle;

/**
 * @brief Draws the UI overlay on top of the composited scene.
 *
 * Streams the frame's UIDrawData into a dynamic vertex buffer and draws it alpha-blended, depth
 * off, over the viewport rect. A no-op when the draw list is empty. Each command scissors to its
 * own rect, the one state this pass puts back itself: GLBackend::render does not reset it.
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

    private:
        /**
         * @brief Grow the dynamic vertex buffer to hold at least @p vertexCount vertices.
         *
         * Binds it to the VAO again when it reallocates.
         *
         * @param vertexCount The vertices this frame's overlay draws.
         */
        void ensureCapacity(uint32_t vertexCount);

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
        Vkm::GL::Shader                        m_shader;
        std::unique_ptr<Vkm::GL::VertexArray>  m_vao;
        std::unique_ptr<Vkm::GL::VertexBuffer> m_vbo;
        uint32_t                               m_capacity = 0;  ///< VBO capacity in vertices.

        /**
         * @brief One texel of zero distance, bound where a command has no atlas.
         *
         * A glyph whose atlas is not yet on the GPU reads zero coverage and draws nothing, while
         * solids in the same run still draw.
         */
        std::unique_ptr<Vkm::GL::Texture2D> m_noAtlas;
};

} // namespace Vkm::Engine
