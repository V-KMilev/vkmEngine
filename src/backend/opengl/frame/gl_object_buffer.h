#pragma once

#include <cstdint>
#include <utility>
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
 * Each object at its own index, so a draw carries only object indices through the instance
 * attribute.
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
         * Only the objects RenderObjects::changed names, while the copies are of the frame before;
         * every one when they are not, or when the objects say all changed.
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
        /// Past this many runs of changed objects a whole upload is the cheaper: a run is a patch
        /// per stream.
        static constexpr size_t MAX_PATCH_RUNS = 64;

    private:
        // Read by every pass that draws objects, so kept in video memory.
        Vkm::GL::DeviceStream m_modelStream;
        Vkm::GL::DeviceStream m_skinFirstStream;
        Vkm::GL::DeviceStream m_flagStream;

        std::vector<uint32_t> m_flagScratch;  ///< The flags as uploaded; capacity kept across frames.
        std::vector<std::pair<uint32_t, uint32_t>> m_runs;  ///< This frame's [first, end) patches.

        uint64_t m_serial = 0;      ///< RenderObjects::serial of the frame the copies are of.
        uint32_t m_count  = 0;      ///< Objects the copies hold.
        bool     m_posed  = false;  ///< The last upload carried first bones.
};

} // namespace Vkm::Engine
