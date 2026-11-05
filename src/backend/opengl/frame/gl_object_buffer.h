#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "vkm_opengl/buffer/stream_ring.h"

namespace Vkm::Engine {

struct RenderObjects;

inline constexpr uint32_t INSTANCE_FLAG_VIEWMODEL = 1u;  ///< An object's bit in its INSTANCE_FLAGS word:
                                                         ///< drawn through the viewmodel projection
                                                         ///< (ObjectDraw).
inline constexpr uint32_t INSTANCE_WATER_SHIFT = 16u;    ///< Where in its INSTANCE_FLAGS word an object's
                                                         ///< ObjectDraw::water lies: the bits from here up.

/**
 * @brief Every object's model matrix, its flags, its first bone when the frame posed and its world
 *        box when the GPU cull reads one, for instanced draws.
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
         * @brief Write model matrices and flags, first bones when @p posed and world boxes when
         *        @p boxed, into @p stream.
         *
         * Only the objects RenderObjects::changed names, while the copies are of the frame before;
         * every one when they are not, or when the objects say all changed.
         *
         * @param objects The objects this frame's index lists address.
         * @param posed   Whether the frame has a bone palette; without one no first bone is read.
         * @param boxed   Whether the GPU occlusion cull runs; without it no world box is read.
         * @param stream  The frame's streaming memory (GLAssetMirror::stream).
         */
        void upload(const RenderObjects& objects, bool posed, bool boxed, Vkm::OpenGL::StreamRing& stream);

        /**
         * @brief Bind the buffers to their storage points.
         *
         * Before a pass's first instanced draw: another GLObjectBuffer may hold the same points.
         */
        void bind() const;

    private:
        static constexpr size_t   MAX_PATCH_RUNS = 64;   ///< Past this many runs of changed objects a whole
                                                         ///< upload is the cheaper: a run is a patch per
                                                         ///< stream.
        static constexpr uint32_t PATCH_GAP      = 128;  ///< Changed objects this near patch as one run: a
                                                         ///< patch costs more than the unchanged objects
                                                         ///< between copied with it.

    private:
        // Read by every pass that draws objects, so kept in video memory.
        Vkm::OpenGL::DeviceStream m_modelStream;
        Vkm::OpenGL::DeviceStream m_skinFirstStream;
        Vkm::OpenGL::DeviceStream m_flagStream;
        Vkm::OpenGL::DeviceStream m_boxStream;

        std::vector<uint32_t>                      m_flagScratch;  ///< The flags as uploaded; capacity kept
                                                                   ///< across frames.
        std::vector<std::pair<uint32_t, uint32_t>> m_runs;         ///< This frame's [first, end) patches.

        uint64_t m_serial = 0;      ///< RenderObjects::serial of the frame the copies are of.
        uint32_t m_count  = 0;      ///< Objects the copies hold.
        bool     m_posed  = false;  ///< The last upload carried first bones.
        bool     m_boxed  = false;  ///< The last upload carried world boxes.
};

} // namespace Vkm::Engine
