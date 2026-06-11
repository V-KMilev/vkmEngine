#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace Core {
    class Context;
    class UniformBuffer;
}

namespace Engine {

struct RenderView;
class GLView;
class GLIBL;
class GLProbeBaker;
class GLProbeArray;

/**
 * @brief Owns the reflection-probe GPU pipeline: arrays, baker, UBO, bake state.
 *
 * One per backend. Holds the shared cube-map arrays and the baker, plus the
 * per-layer bake bookkeeping that drives change-detected re-baking. The backend
 * drives it with three calls: init() once (context live), bind() each frame
 * before the passes (selects + uploads the active probes), and update() at frame
 * end (re-bakes what moved or changed). All probe raw-state lives here, so the
 * backend never sees a probe handle or a bake counter.
 */
class GLProbeManager {
    public:
        GLProbeManager();
        ~GLProbeManager();

        GLProbeManager(const GLProbeManager& other) = delete;
        GLProbeManager& operator=(const GLProbeManager& other) = delete;

        GLProbeManager(GLProbeManager && other) = delete;
        GLProbeManager& operator=(GLProbeManager && other) = delete;

        /// Create the baker + shared cube-map arrays. Call with a live GL context.
        void init();

        /// Select the nearest baked probes, pack the ProbeBlock UBO, and bind it +
        /// the two cube-map arrays for the forward pass. Returns how many probes
        /// were bound (0 = none); the caller forwards this to ctx.probeCount.
        int bind(const RenderView& view);

        /// Frame-end: (re)bake probes that are new, moved, or version-bumped,
        /// capped per frame so several changing at once don't hitch. Run after the
        /// passes (the baker rebinds the camera / light UBOs).
        void update(Core::Context& gl, const RenderView& view, const GLView& glView, const GLIBL& ibl);

    private:
        /// Per-layer bake state, for change-detected re-baking.
        struct BakeState {
            bool      baked    = false;
            glm::vec3 position = glm::vec3(0.0f);  ///< Position the layer was last baked at.
            uint32_t  version  = 0;                ///< bakeVersion the layer was last baked at.
        };

        std::unique_ptr<GLProbeBaker>        m_baker;  ///< Bakes probes at frame end.
        std::unique_ptr<GLProbeArray>        m_array;  ///< Shared irradiance + prefilter cube arrays.
        std::vector<BakeState>               m_state;  ///< Per layer: baked + last-baked position/version.
        std::unique_ptr<Core::UniformBuffer> m_ubo;    ///< ProbeBlock: boxes + layers (binding 4).
};

} // namespace Engine
