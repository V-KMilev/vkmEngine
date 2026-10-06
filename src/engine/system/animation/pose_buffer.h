#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief One rig's range in the frame's pose arrays, plus what the pose implies about its extent.
 *
 * `first` and `count` address both `PoseBuffer::global()` and `PoseBuffer::palette()`, which are
 * parallel. The bounds are raw: skin hangs off a bone by a distance only the mesh knows, so the consumer
 * that knows the mesh inflates them.
 */
struct PoseSlice {
    uint32_t first = 0;
    uint32_t count = 0;

    glm::vec3 originMin{0.0f};  ///< AABB of the posed bone origins, rig model space.
    glm::vec3 originMax{0.0f};

    float maxBoneScale = 1.0f;  ///< Largest scale any bone carries in this pose, never below 1.
};

/**
 * @brief Writable view of one slice, handed to whatever composes the pose.
 *
 * The pointers are valid only until the buffer is resized.
 */
struct PoseWrite {
    PoseSlice* slice   = nullptr;
    glm::mat4* global  = nullptr;
    glm::mat4* palette = nullptr;
};

/**
 * @brief Every rig's pose for one frame, published on FrameContext::poses.
 *
 * A per-frame product, not a component: rebuilt whenever the rigs are posed, authored by no one, and
 * read by several mesh entities per rig. `global` is the pose in rig model space; `palette` is
 * `global[b] * inverseBind[b]` for the vertex stage, kept beside the pose rather than overwriting it.
 */
class PoseBuffer {
    public:
        PoseBuffer() = default;
        ~PoseBuffer() = default;

        PoseBuffer(const PoseBuffer& other) = delete;
        PoseBuffer& operator=(const PoseBuffer& other) = delete;

        PoseBuffer(PoseBuffer && other) = delete;
        PoseBuffer& operator=(PoseBuffer && other) = delete;

    public:
        /**
         * @brief Drop last frame's slices and mapping, keeping the capacity.
         */
        void clear();

        /**
         * @brief Reserve @p boneCount consecutive bones for one rig.
         *
         * Invalidates every PoseWrite handed out earlier: allocate every slice before composing any.
         *
         * @param boneCount Bones the rig has.
         * @return Index of the new slice, for writeTo() and mapEntity().
         */
        uint32_t addSlice(uint32_t boneCount);

        /**
         * @brief Record that @p entity is driven by @p slice.
         *
         * @param entity Entity the slice poses, generation included.
         * @param slice Slice index returned by addSlice().
         */
        void mapEntity(EntityId entity, uint32_t slice);

        /**
         * @brief Writable view of @p slice, for composing its pose.
         *
         * Writes nothing, so distinct slices may be filled from several threads at once.
         *
         * @param slice Slice index returned by addSlice().
         * @return Pointers into this buffer's arrays, valid until the next addSlice().
         */
        PoseWrite writeTo(uint32_t slice);

        /**
         * @brief The slice driving @p entity.
         *
         * Keyed on the whole id, not the slot: the map is read every frame until the next tick, and a
         * slot reused in between must not skin a new entity from another rig's matrices.
         *
         * @param entity Entity to look up.
         * @return The slice, or nullptr when nothing poses that entity.
         */
        const PoseSlice* sliceOf(EntityId entity) const;

        const std::vector<PoseSlice>& slices()  const { return m_slices; }
        const std::vector<glm::mat4>& global()  const { return m_global; }
        const std::vector<glm::mat4>& palette() const { return m_palette; }

    private:
        /**
         * @brief Which entity a slot's slice was mapped for, and the slice.
         */
        struct Mapping {
            EntityId entity{};
            uint32_t slice = 0;
        };

    private:
        std::vector<glm::mat4> m_global;   ///< Model-space bone transforms: the pose.
        std::vector<glm::mat4> m_palette;  ///< global[b] * inverseBind[b], parallel to m_global.
        std::vector<PoseSlice> m_slices;
        std::vector<Mapping>   m_sliceOfEntity;  ///< By entity slot; a null entity where nothing is posed.
};

/**
 * @brief Write into @p out's slice the bounds the pose composed into it implies.
 *
 * Every composer ends with this, so a skinned mesh is bounded alike whichever posed it; a stale bound
 * would cull a mesh as it starts moving. A slice with no bones has its origins at zero.
 *
 * @param out The slice just composed; its `global` poses are final.
 */
void finishSlice(const PoseWrite& out);

} // namespace Vkm::Engine
