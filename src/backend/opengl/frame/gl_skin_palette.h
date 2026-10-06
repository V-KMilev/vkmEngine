#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace Vkm::GL {
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

/**
 * @brief The frame's bone palettes in one storage buffer, for skinned draws.
 *
 * RenderView carries every rig's palette end to end and stamps each item with its base, so this
 * is one upload and one binding a frame; the base travels with the instance. A storage buffer
 * because bones have no cap: a uniform block would need a MAX_BONES.
 */
class GLSkinPalette {
    public:
        GLSkinPalette();
        ~GLSkinPalette();

        GLSkinPalette(const GLSkinPalette& other) = delete;
        GLSkinPalette& operator=(const GLSkinPalette& other) = delete;

        GLSkinPalette(GLSkinPalette && other) = delete;
        GLSkinPalette& operator=(GLSkinPalette && other) = delete;

    public:
        /**
         * @brief Upload this frame's palettes, growing the buffer when needed.
         *
         * @param matrices RenderView::skinMatrices, indexed by skinFirst; null when nothing posed.
         */
        void update(const std::vector<glm::mat4>* matrices);

        /**
         * @brief Bind the palettes to their SSBO point.
         *
         * Only on a frame that posed something (count() > 0); otherwise it holds an earlier
         * frame's bones.
         */
        void bind() const;

        /// Matrices uploaded this frame.
        uint32_t count() const { return m_count; }

    private:
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_buffer;
        uint32_t m_capacity = 0;  ///< Bytes allocated.
        uint32_t m_count    = 0;
};

} // namespace Vkm::Engine
