#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "convention/gl_bindings.h"
#include "offline/gl_capture_lighting.h"
#include "offline/gl_capture_settle.h"

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
class GLProbeBaker;
class GLProbeArray;
class GLSceneCapture;

/**
 * @brief One probe in the ProbeBlock UBO, std140 - must match ProbeEntry in shaders/probes.glsl.
 */
struct GpuProbe {
    glm::vec4 center;    ///< xyz world centre, w pad
    glm::vec4 extents;   ///< xyz half-extents, w pad
    glm::vec4 params;    ///< x falloff, y intensity, z layer index, w pad
};

/**
 * @brief The ProbeBlock UBO, std140 - must match ProbeBlock in shaders/probes.glsl.
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
        /// Seconds a refreshed probe rests before it may be refreshed again, so a light that never
        /// holds still does not keep one probe refreshing back to back.
        static constexpr double REFRESH_REST = 1.0;

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
         * @brief At frame end, bake again the probes that are new, bumped, or moved or resized
         *        and holding still, or were captured under another bake of the irradiance volume;
         *        else refresh one captured under other light, a step a frame.
         *
         * A bake is at once, one a frame. A refresh fills the staging layer and replaces the
         * probe's after its last step (GLProbeBaker::advance), one probe at a time, the
         * longest-waiting for its distance from the eye first; a bake in between ends it, to
         * begin again in its turn. Run after the passes:
         * the baker rebinds the camera UBO and the lights SSBO.
         *
         * @param gl       Live GL context the bake draws through.
         * @param view     Supplies the probes and the scene.
         * @param glView   GPU mirror the captures draw from.
         * @param ibl      The global environment, the captures' backdrop.
         * @param volume   The baked irradiance volume, at the view's box, lighting what the
         *                 captures see of it; null for none.
         * @param lighting What lights the captures now but for the lights, which are each probe's
         *                 own: those whose radius reaches within half its width of its box. A
         *                 probe captured under other light is refreshed.
         */
        void update(
            Vkm::GL::Context& gl,
            const RenderView& view,
            GLView& glView,
            const GLIBL& ibl,
            const GLIrradianceVolume* volume,
            const CaptureLighting& lighting
        );

        /**
         * @brief Forget every layer's bake state, forcing a re-bake.
         *
         * Nothing the re-bake and refresh tests compare changes when the scene is replaced, while
         * everything the capture contains does.
         */
        void invalidate();

    private:
        /// Metres from the eye over which a probe waiting to be refreshed counts half as urgent.
        static constexpr float REFRESH_NEAR = 10.0f;

        /// Where a probe stands and the box it reflects.
        struct Placement {
            glm::vec3 position{0.0f};
            glm::vec3 box{0.0f};

            /// The same place: within a millimetre, so a probe settled to the bit is not moving.
            bool operator==(const Placement& other) const {
                return glm::distance(position, other.position) <= 1e-3f && box == other.box;
            }
            bool operator!=(const Placement& other) const { return !(*this == other); }
        };

        /**
         * @brief Per-layer bake state, for change-detected re-baking.
         */
        struct BakeState {
            bool      owned      = false;            ///< A probe in the scene holds this layer.
            uint32_t  owner      = 0;                ///< That probe's entity slot.
            bool      baked      = false;
            /// Where the layer was last baked: the shadowed sun's map is fitted to its box.
            Placement placed;
            uint32_t  version    = 0;                ///< bakeVersion the layer was last baked at.
            uint32_t  volumeBake = 0;                ///< The irradiance volume's bakeId it was lit by.
            uint64_t  captured   = 0;                ///< The update() it was last captured in.
            double    refreshed  = -REFRESH_REST;    ///< m_seconds its last refresh ended at.
            CaptureLighting lighting;                ///< What lit it when it was captured.
            /// Where the probe stands, so one being moved bakes once it settles, not every
            /// frame it moves.
            CaptureSettle<Placement> seen;
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

        /**
         * @brief Take a step of refreshing a probe captured under other light than @p lighting.
         *
         * @param gl     Live GL context the step draws through.
         * @param view   Supplies the probes and the scene.
         * @param glView GPU mirror the captures draw from.
         * @param ibl    The global environment, the captures' backdrop.
         * @param volume The irradiance volume lighting what the captures see; null for none.
         */
        void refresh(
            Vkm::GL::Context& gl,
            const RenderView& view,
            GLView& glView,
            const GLIBL& ibl,
            const GLIrradianceVolume* volume
        );

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

        /// What lights each of the view's probes now, its own lights among it (update()).
        std::vector<CaptureLighting>            m_lighting;

        int             m_refreshLayer = -1;  ///< The layer a refresh under way replaces; -1 for none.
        CaptureLighting m_refreshLighting;    ///< What lit the scene when it began.
        uint64_t        m_frame        = 0;   ///< Counts update()s, for BakeState::captured.
        double          m_seconds      = 0.0; ///< Sums the views' frameSeconds, for REFRESH_REST.
};

} // namespace Vkm::Engine
