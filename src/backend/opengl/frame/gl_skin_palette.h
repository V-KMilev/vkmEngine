#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gl_stream_ring.h"

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
         * @brief Write this frame's palettes into @p stream.
         *
         * @param matrices RenderView::skinMatrices, indexed by skinFirst; null when nothing posed.
         * @param stream   The frame's streaming memory (GLView::stream).
         */
        void update(const std::vector<glm::mat4>* matrices, Vkm::GL::StreamRing& stream);

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
        Vkm::GL::DeviceStream m_device;  ///< Read by every skinned draw, so in video memory.
        uint32_t              m_count = 0;
};

} // namespace Vkm::Engine
