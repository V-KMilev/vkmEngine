#pragma once

#include <cstdint>
#include <vector>

#include "gl_stream_ring.h"

namespace Vkm::Engine {

struct RenderObjects;

/// An object's bit in its INSTANCE_FLAGS word: drawn through the viewmodel projection (ObjectDraw).
inline constexpr uint32_t INSTANCE_FLAG_VIEWMODEL = 1u;

/**
 * @brief The share of the depth range nearest the eye a viewmodel object's depth is squashed into.
 *
 * A perspective camera's only: an orthographic one draws a viewmodel at its own depth. The band
 * is put and found by shaders/camera.glsl's intoViewmodelBand and inViewmodelBand.
 */
inline constexpr float VIEWMODEL_DEPTH_SHARE = 0.05f;

/**
 * @brief Every object's model matrix, its flags, and first bone when the frame posed, for instanced draws.
 *
 * The whole set goes up each frame at each object's own index, so a draw carries only object
 * indices through the instance attribute. Whole, since there is no record of what changed to
 * trust (engine.md, dirty flags).
 */
class GLObjectBuffer {
    public:
        GLObjectBuffer();
        ~GLObjectBuffer();

        GLObjectBuffer(const GLObjectBuffer& other) = delete;
        GLObjectBuffer& operator=(const GLObjectBuffer& other) = delete;

        GLObjectBuffer(GLObjectBuffer && other) = delete;
        GLObjectBuffer& operator=(GLObjectBuffer && other) = delete;

    public:
        /**
         * @brief Write model matrices and flags, and first bones when @p posed, into @p stream.
         *
         * @param objects The objects this frame's index lists address.
         * @param posed   Whether the frame has a bone palette; without one no first bone is read.
         * @param stream  The frame's streaming memory (GLView::stream).
         */
        void upload(const RenderObjects& objects, bool posed, Vkm::GL::StreamRing& stream);

        /**
         * @brief Bind the buffers to their storage points.
         *
         * Before a pass's first instanced draw: another GLObjectBuffer may hold the same points.
         */
        void bind() const;

    private:
        // Read by every pass that draws objects, so copied into video memory once a frame.
        Vkm::GL::DeviceStream m_modelStream;
        Vkm::GL::DeviceStream m_skinFirstStream;
        Vkm::GL::DeviceStream m_flagStream;

        Vkm::GL::BufferSpan m_models;
        Vkm::GL::BufferSpan m_skinFirst;
        Vkm::GL::BufferSpan m_flags;  ///< An INSTANCE_FLAG_* word an object.

        std::vector<uint32_t> m_flagScratch;  ///< The flags as uploaded; capacity kept across frames.

        bool m_posed = false;  ///< The last upload carried first bones.
};

} // namespace Vkm::Engine
