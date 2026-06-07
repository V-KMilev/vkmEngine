#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief CPU-side mirror of the Hi-Z (max-Z) pyramid for occlusion testing.
 *
 * The render graph builds the GPU pyramid each frame in GLHiZPass.
 * The backend reads back one mip level at end-of-frame and pushes the
 * result here so the next frame's visibility system can AABB-test
 * against it. One-frame-latent occlusion: testing against the previous
 * frame's depth avoids the sync stall a same-frame readback would cost
 * and is accurate enough whenever camera motion is continuous.
 *
 * Stores positive view-space distance-from-camera per cell. The
 * companion @ref viewProj is the view-projection matrix the pyramid
 * was rendered with - needed because the AABB test re-projects fresh
 * world-space corners into the same screen space the cells live in.
 *
 * Singleton because the producer is backend-side (GLHiZPass) and the
 * consumer is the visibility System; passing a pointer through every
 * layer would invent an asymmetric dependency.
 */
class OcclusionOracle {
    public:
        OcclusionOracle(const OcclusionOracle& other) = delete;
        OcclusionOracle& operator=(const OcclusionOracle& other) = delete;

        OcclusionOracle(OcclusionOracle && other) = delete;
        OcclusionOracle& operator=(OcclusionOracle && other) = delete;

    public:
        static OcclusionOracle& get();

        struct Frame {
            std::vector<float>  pyramid;     ///< [width * height] cells, positive distance.
            std::uint32_t       width  = 0;  ///< Mip dimensions (NOT viewport).
            std::uint32_t       height = 0;
            glm::mat4           view{1.0f};      ///< World -> view (for distance-from-camera).
            glm::mat4           viewProj{1.0f}; ///< World -> clip (for screen-space bounds).
            bool                ready = false;
        };

        /// Producer side - called by the backend after Hi-Z is built.
        void publish(
            std::vector<float> pyramid,
            std::uint32_t      width,
            std::uint32_t      height,
            const glm::mat4&   view,
            const glm::mat4&   viewProj
        );

        /// Consumer side - returns the most recent snapshot. Empty when
        /// the producer hasn't published yet.
        Frame snapshot() const;

        /// Invalidate the snapshot (e.g. on scene swap / teleport so the
        /// stale viewProj isn't used).
        void invalidate();

    private:
        OcclusionOracle() = default;

        Frame m_current;
};

} // namespace Engine
