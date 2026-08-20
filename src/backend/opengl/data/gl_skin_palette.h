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
 * @brief The frame's bone palettes in one storage buffer, for every pass that
 *        draws skinned geometry.
 *
 * RenderView flattens every skinned item's palette end to end and stamps each
 * item with where its own bones start, so this is one upload per frame and one
 * binding shared by the depth prepass, the forward pass and the shadow pass.
 * Nothing here knows which item is which: the base index travels with the
 * instance, not with the buffer.
 *
 * A storage buffer rather than a uniform array because there is no cap to
 * mirror - the whole scene's bones live here, and a uniform block would put a
 * MAX_BONES constant into the shaders and into engine_config.h for no gain.
 */
class GLSkinPalette {
    public:
        GLSkinPalette() = default;
        ~GLSkinPalette();

        GLSkinPalette(const GLSkinPalette& other) = delete;
        GLSkinPalette& operator=(const GLSkinPalette& other) = delete;

        GLSkinPalette(GLSkinPalette && other) = delete;
        GLSkinPalette& operator=(GLSkinPalette && other) = delete;

    public:
        /**
         * @brief Upload this frame's palettes, growing the buffer when needed.
         *
         * @param matrices RenderView::skinMatrices, in the order the view built
         *                 it - which is what every skinFirst indexes into.
         */
        void update(const std::vector<glm::mat4>& matrices);

        /**
         * @brief Bind the palettes to their SSBO point.
         *
         * A no-op on a frame with no skinned geometry, which is also a frame
         * where no program declaring the block is ever bound.
         */
        void bind() const;

        /// Matrices uploaded this frame.
        uint32_t count() const { return m_count; }

    private:
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_buffer;
        uint32_t m_capacity = 0;  ///< Bytes allocated.
        uint32_t m_count    = 0;  ///< Matrices uploaded this frame.
};

} // namespace Vkm::Engine
