#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "convention/gl_bindings.h"

namespace Vkm::GL {
    class Context;
    class UniformBuffer;
}

namespace Vkm::Engine {

struct RenderView;
class GLCubeConvolver;
class GLView;
class GLIBL;
class GLProbeBaker;
class GLProbeArray;
class GLSceneCapture;

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

        /**
         * @brief Create the baker + shared cube-map arrays.
         *
         * Must be called with a live GL context bound.
         *
         * @param capture   Shared scene capture the baker draws probes through.
         * @param convolver Shared cube convolver the baker filters them with.
         */
        void init(GLSceneCapture& capture, GLCubeConvolver& convolver);

        /**
         * @brief Collect the baked probes, pack the ProbeBlock UBO, and bind it +
         * the two cube-map arrays for the forward pass. Returns how many probes
         * were bound (0 = none); the caller forwards this to ctx.probeCount.
         */
        int bind(const RenderView& view);

        /**
         * @brief Frame-end: (re)bake probes that are new, moved, or version-bumped,
         * capped per frame so several changing at once don't hitch. Run after the
         * passes (the baker rebinds the camera / light UBOs).
         */
        void update(Vkm::GL::Context& gl, const RenderView& view, const GLView& glView, const GLIBL& ibl);

        /**
         * @brief Forget every layer's bake state, forcing a re-bake.
         *
         * A probe's cube map is a capture of scene geometry, but the re-bake
         * test only compares the probe's own position and bakeVersion. Replacing
         * the scene leaves both identical while changing everything the capture
         * contains, so the swap has to say so explicitly.
         */
        void invalidate();

    private:
        /**
         * @brief Per-layer bake state, for change-detected re-baking.
         */
        struct BakeState {
            bool      baked    = false;
            glm::vec3 position = glm::vec3(0.0f);  ///< Position the layer was last baked at.
            uint32_t  version  = 0;                ///< bakeVersion the layer was last baked at.
        };

        /**
         * @brief std140 ProbeBlock layout - must match shaders/forward/pbr.
         */
        struct GpuProbe {
            glm::vec4 center;    ///< xyz world centre, w pad
            glm::vec4 extents;   ///< xyz half-extents, w pad
            glm::vec4 params;    ///< x falloff, y intensity, z layer index, w pad
        };
        struct ProbeBlock {
            GpuProbe probes[GLBindings::ProbeTextureSlots::MAX_PROBES];
        };

    private:
        std::unique_ptr<GLProbeBaker>           m_baker;  ///< Bakes probes at frame end.
        std::unique_ptr<GLProbeArray>           m_array;  ///< Shared irradiance + prefilter cube arrays.
        std::vector<BakeState>                  m_state;  ///< Per layer: baked + last-baked position/version.
        std::vector<uint32_t>                   m_active; ///< Scratch baked-probe indices, cleared each bind().
        std::unique_ptr<Vkm::GL::UniformBuffer> m_ubo;    ///< ProbeBlock: boxes + layers (binding 4).
        ProbeBlock                              m_lastBlock{};  ///< Last uploaded block, for change-gated upload.
};

} // namespace Vkm::Engine
