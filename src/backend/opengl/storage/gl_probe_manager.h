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
class GLIrradianceVolume;
class ResourceManager;
class GLProbeBaker;
class GLProbeArray;
class GLSceneCapture;

/**
 * @brief One probe in the ProbeBlock UBO, std140 - must match ProbeEntry in shaders/forward/pbr.
 */
struct GpuProbe {
    glm::vec4 center;    ///< xyz world centre, w pad
    glm::vec4 extents;   ///< xyz half-extents, w pad
    glm::vec4 params;    ///< x falloff, y intensity, z layer index, w pad
};

/**
 * @brief The ProbeBlock UBO, std140 - must match ProbeBlock in shaders/forward/pbr.
 */
struct ProbeBlock {
    GpuProbe probes[GLBindings::ProbeTextureSlots::MAX_PROBES];
};

/**
 * @brief Owns the reflection-probe GPU pipeline: arrays, baker, UBO, bake state.
 *
 * bind() runs each frame before the passes, update() at frame end, invalidate() when the world
 * is replaced. All probe state lives here, so the backend never sees a probe handle or a bake
 * counter.
 */
class GLProbeManager {
    public:
        GLProbeManager();
        ~GLProbeManager();

        GLProbeManager(const GLProbeManager& other) = delete;
        GLProbeManager& operator=(const GLProbeManager& other) = delete;

        GLProbeManager(GLProbeManager && other) = delete;
        GLProbeManager& operator=(GLProbeManager && other) = delete;

    public:
        /**
         * @brief Create the baker + shared cube-map arrays.
         *
         * Must be called with a live GL context bound.
         *
         * @param capture   Scene capture the baker draws probes through.
         * @param convolver Cube convolver the baker filters them with.
         */
        void init(GLSceneCapture& capture, GLCubeConvolver& convolver);

        /**
         * @brief Collect the baked probes and bind them for the frame.
         *
         * @param view Supplies the probes.
         * @return How many probes were bound.
         */
        int bind(const RenderView& view);

        /**
         * @brief At frame end, bake again the probes that are new, moved, resized or bumped, or
         *        were captured under another bake of the irradiance volume.
         *
         * Capped per frame. Run after the passes: the baker rebinds the camera UBO and the
         * lights SSBO.
         *
         * @param gl        Live GL context the bake draws through.
         * @param view      Supplies the probes and the scene.
         * @param glView    GPU mirror the captures draw from.
         * @param resources Resolves what the captures draw.
         * @param ibl       The global environment, the captures' backdrop.
         * @param volume    The baked irradiance volume, at the view's box, lighting what the
         *                  captures see of it; null for none.
         */
        void update(
            Vkm::GL::Context& gl,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& ibl,
            const GLIrradianceVolume* volume
        );

        /**
         * @brief Forget every layer's bake state, forcing a re-bake.
         *
         * The re-bake test compares only a probe's pose, box and bakeVersion, which replacing the
         * scene leaves identical while changing everything the capture contains.
         */
        void invalidate();

    private:
        /**
         * @brief Per-layer bake state, for change-detected re-baking.
         */
        struct BakeState {
            bool      owned      = false;            ///< A probe in the scene holds this layer.
            uint32_t  owner      = 0;                ///< That probe's entity slot.
            bool      baked      = false;
            glm::vec3 position   = glm::vec3(0.0f);  ///< Position the layer was last baked at.
            /// Half-extents it was baked for: the key light's map is fitted to them.
            glm::vec3 box        = glm::vec3(0.0f);
            uint32_t  version    = 0;                ///< bakeVersion the layer was last baked at.
            uint32_t  volumeBake = 0;                ///< The irradiance volume's bakeId it was lit by.
        };

        /**
         * @brief Give every probe in @p view a cube-array layer, into m_layerOf.
         *
         * A layer belongs to a probe entity, not its list position: the list is a SparseSet's
         * packing order and moves when a probe is destroyed. A gone probe's layer is freed; a new
         * probe takes a free one unbaked; probes past the arrays' capacity get none.
         *
         * @param view Supplies the probes.
         */
        void assignLayers(const RenderView& view);

    private:
        std::unique_ptr<GLProbeBaker>           m_baker;        ///< Bakes probes at frame end.
        /// Shared irradiance + prefilter cube arrays.
        std::unique_ptr<GLProbeArray>           m_array;
        /// Per layer: owner, baked, last-baked position/version.
        std::vector<BakeState>                  m_state;
        /// Per view probe this frame: its layer, -1 for none.
        std::vector<int>                        m_layerOf;
        /// Scratch: per layer, whether its owner is in this frame's view.
        std::vector<uint8_t>                    m_seen;
        /// Scratch baked-probe indices, cleared each bind().
        std::vector<uint32_t>                   m_active;
        /// ProbeBlock: boxes + layers (UBOBindingPoints::PROBES).
        std::unique_ptr<Vkm::GL::UniformBuffer> m_ubo;
        /// Last uploaded block, for gating the upload.
        ProbeBlock                              m_lastBlock{};
};

} // namespace Vkm::Engine
