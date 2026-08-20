#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief One rig's range in the frame's pose arrays, plus what the pose implies
 *        about its own extent.
 *
 * `first` and `count` address both `PoseBuffer::global()` and
 * `PoseBuffer::palette()` - the two are parallel and always the same length.
 *
 * The bounds fields are what the pose knows about how far the character has
 * moved from its bind shape: the box of the posed bone origins, and the largest
 * scale any bone carries. Neither is the character's bounding box on its own -
 * skin hangs off a bone by a distance only the mesh knows - so they are
 * published raw here and inflated by the consumer that knows the mesh.
 */
struct PoseSlice {
    uint32_t first = 0;
    uint32_t count = 0;

    glm::vec3 originMin{0.0f};  ///< AABB of the posed bone origins, rig model space.
    glm::vec3 originMax{0.0f};

    float maxBoneScale = 1.0f;  ///< Largest scale any bone carries in this pose.
};

/**
 * @brief Writable view of one slice, handed to whatever composes the pose.
 *
 * The pointers are valid only until the buffer is resized, which is why every
 * slice is allocated before any is written.
 */
struct PoseWrite {
    PoseSlice* slice   = nullptr;
    glm::mat4* global  = nullptr;
    glm::mat4* palette = nullptr;
};

/**
 * @brief Every rig's pose for one frame, published on FrameContext::poses.
 *
 * A per-frame product rather than a component, for the same reason
 * `ctx.visibility` is one: it is rebuilt from scratch each frame, no one
 * authors it, and it belongs to a rig that several mesh entities read. Keeping
 * it out of the ECS also keeps ten kilobytes of matrices out of every SparseSet
 * slot and every serialized row.
 *
 * Two arrays, not one. `global` is the pose - each bone's transform in rig model
 * space - and `palette` is `global[b] * inverseBind[b]`, the form the vertex
 * stage wants. The palette is derived from the pose and never overwrites it:
 * recovering the pose from the palette would mean inverting the bind matrices
 * per bone, and the pose is what an attachment, a socket or a physics body
 * reads.
 *
 * The class owns the coupling between the three vectors - a slice's range is
 * only meaningful while `global` and `palette` are the same length - so the
 * arrays are private and grown through addSlice().
 */
class PoseBuffer {
    public:
        /// Entity index that no rig drives.
        static constexpr uint32_t NO_POSE = 0xFFFFFFFFu;

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
         * Resizes the pose arrays, so every PoseWrite handed out earlier is
         * invalidated: allocate every slice before composing any of them.
         *
         * @param boneCount Number of bones the rig has.
         * @return Index of the new slice, for writeTo() and mapEntity().
         */
        uint32_t addSlice(uint32_t boneCount);

        /**
         * @brief Record that the entity in slot @p entityIndex is driven by @p slice.
         *
         * @param entityIndex Entity slot index (EntityId::index).
         * @param slice Slice index returned by addSlice().
         */
        void mapEntity(uint32_t entityIndex, uint32_t slice);

        /**
         * @brief Writable view of @p slice, for composing its pose.
         *
         * Writes nothing itself, so distinct slices may be handed out and filled
         * from several threads at once - which is what lets the evaluation pass
         * run in parallel once allocation is done.
         *
         * @param slice Slice index returned by addSlice().
         * @return Pointers into this buffer's arrays, valid until the next addSlice().
         */
        PoseWrite writeTo(uint32_t slice);

        /**
         * @brief The slice driving the entity in slot @p entityIndex.
         *
         * Total: an entity no rig drives, and an index past anything this frame
         * touched, both answer null.
         *
         * @param entityIndex Entity slot index (EntityId::index).
         * @return The slice, or nullptr when nothing poses that entity.
         */
        const PoseSlice* sliceOf(uint32_t entityIndex) const;

        const std::vector<PoseSlice>& slices()  const { return m_slices; }
        const std::vector<glm::mat4>& global()  const { return m_global; }
        const std::vector<glm::mat4>& palette() const { return m_palette; }

    private:
        std::vector<glm::mat4> m_global;   ///< Model-space bone transforms: the pose.
        std::vector<glm::mat4> m_palette;  ///< global[b] * inverseBind[b], parallel to m_global.
        std::vector<PoseSlice> m_slices;
        std::vector<uint32_t>  m_sliceOfEntity;  ///< Entity slot -> slice, NO_POSE elsewhere.
};

} // namespace Vkm::Engine
