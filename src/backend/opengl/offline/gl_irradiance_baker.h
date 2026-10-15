#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "gl_compute_shader.h"
#include "gl_frame_buffer.h"
#include "gl_shader_storage_buffer.h"
#include "gl_texture.h"

#include "offline/gl_baked_from.h"
#include "offline/gl_capture_lighting.h"
#include "offline/gl_capture_settle.h"
#include "storage/gl_irradiance_volume.h"
#include "system/render/data/irradiance_volume_data.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLIBL;
class GLSceneCapture;
class GLView;
class ResourceManager;
struct RenderView;

/**
 * @brief Bakes an irradiance volume, and lights it again when the lights or the sky change.
 *
 * The bake captures what every grid probe sees as surfaces, not as light: a small cube of
 * surfels (shaders/irradiance/surfel.glsl) - the nearest surface through each texel, the colour
 * it bounces, its normal, distance and emission - drawn into the probe's tile of an atlas that
 * stays on the GPU. A compute pass then lights every surfel as the forward pass lights a diffuse
 * surface and projects what each probe sees to SH-L1 (shaders/irradiance/relight). Light is a
 * step apart from geometry, so a sun that moves, a lamp switched on or a sky that changes takes
 * that pass alone - relight() - never the scene drawn around every probe again.
 *
 * Lighting the grid is BOUNCES gathers. In each the surfels inside the box take their ambient
 * from the grid the gather before made, and the first from none: a wall is lit by what reaches
 * it, never by the open sky through it, and each gather adds a bounce. Outside the box surfels
 * take the sky's irradiance, and directions meeting no surface see the sky itself. The shadowed sun
 * casts through GLCaptureShadow's map.
 *
 * A probe seeing the backs of surfaces over more of its sphere than BACKFACE_LIMIT
 * (shaders/irradiance/relight) is inside geometry, and what it sees is the room on the far side.
 * The bake's first gather judges every probe, and as the verdicts depend on where probes stand,
 * not on the light, the blend that replaces each refused probe by its trusted neighbours
 * (probeFillWeights) is found once and applied on the GPU after every gather of every relight. A
 * volume where no probe could be trusted is not marked ready (see GLPass::bindAmbient), so the
 * global IBL stands in rather than a grid of guesses.
 *
 * The surfel cubes are deliberately small: the result is a 4-coefficient spherical average, so
 * face resolution buys almost nothing while multiplying the atlas by the probe count.
 *
 * Owns the programs, the atlas and the scratch grids, so construct it where a GL context is
 * current; the scene capture is borrowed (see GLSceneCapture). update() binds its own camera, lights
 * and shadow blocks; run it at the end of a frame.
 */
class GLIrradianceBaker {
    public:
        static constexpr int FACE_SIZE = 16;  ///< Per-face surfel resolution.

        /// Gathers per lighting: the most times light reaching a probe has bounced off a surface.
        static constexpr int BOUNCES = 6;

        /**
         * @brief Compile the relight and fill programs, capturing through @p capture.
         *
         * @param capture Shared scene capture, owned by the backend and outliving this baker.
         */
        explicit GLIrradianceBaker(GLSceneCapture& capture);
        ~GLIrradianceBaker();

        GLIrradianceBaker(const GLIrradianceBaker& other) = delete;
        GLIrradianceBaker& operator=(const GLIrradianceBaker& other) = delete;

        GLIrradianceBaker(GLIrradianceBaker && other) = delete;
        GLIrradianceBaker& operator=(GLIrradianceBaker && other) = delete;

    public:
        /**
         * @brief At frame end, bake the view's volume again when it changed, else light it again
         *        when @p lighting changed, a gather a frame.
         *
         * A box being dragged changes every frame: until it holds still the last bake stands, and a
         * new bake version is a request to bake now. A change of light during a relight asks for
         * another, which holds until one starts. A no-op for a view with no volume.
         *
         * @param gl        Live GL context the captures draw through.
         * @param volume    The grid the frame reads, resized to a bake's.
         * @param view      Frame snapshot supplying the volume, the geometry and the lights.
         * @param glView    GPU resource mirror for material/mesh lookups.
         * @param resources Resolves the materials the capture syncs.
         * @param globalIBL Baked environment: lights the surfels + fills the background.
         * @param lighting  What lights the surfels now.
         */
        void update(
            Vkm::GL::Context& gl,
            GLIrradianceVolume& volume,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& globalIBL,
            const CaptureLighting& lighting
        );

        /**
         * @brief Forget what the volume was baked from, so the next update() bakes it.
         *
         * For a replaced world: the same box and grid, a different scene.
         */
        void invalidate() { m_bakedFrom.invalidate(); }

    private:
        /**
         * @brief Everything a bake depends on.
         */
        struct Signature {
            glm::vec3 center{0.0f};
            glm::vec3 halfExtents{0.0f};
            uint32_t  resolutionX = 0, resolutionY = 0, resolutionZ = 0;
            uint32_t  bakeVersion = 0;

            /// The same box and grid, whichever bake asked for them.
            bool sameGrid(const Signature& other) const {
                return center      == other.center
                    && halfExtents == other.halfExtents
                    && resolutionX == other.resolutionX
                    && resolutionY == other.resolutionY
                    && resolutionZ == other.resolutionZ;
            }

            bool operator==(const Signature& other) const {
                return sameGrid(other) && bakeVersion == other.bakeVersion;
            }
        };

    private:
        /**
         * @brief Capture every probe of @p data and light the grid into @p volume.
         *
         * @param gl        Live GL context the captures draw through.
         * @param volume    Destination SH volume (resized to the grid by the caller).
         * @param data      The volume's world box + grid resolution.
         * @param view      Frame snapshot supplying the geometry + lights to capture.
         * @param glView    GPU resource mirror for material/mesh lookups.
         * @param resources Resolves the materials the capture syncs.
         * @param globalIBL Baked environment: lights the surfels + fills the background.
         */
        void bake(
            Vkm::GL::Context& gl,
            GLIrradianceVolume& volume,
            const IrradianceVolumeData& data,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& globalIBL
        );

        /**
         * @brief Take the next gather of lighting the last bake's surfels again, starting one when
         *        none is under way.
         *
         * A gather a call, under @p view's lights and @p globalIBL's sky as they are then, into
         * grids of the baker's own; after the last the result is traded into @p volume whole, so
         * the frame reads the old light until the new is complete. A sun moving every frame costs
         * a gather a frame, and the light trails it by BOUNCES frames. The verdicts and the repair
         * are the bake's, and the volume keeps its bakeId(): its probes are the same bake's.
         *
         * @param gl        Live GL context.
         * @param volume    The grid the bake filled.
         * @param data      The box + grid the bake captured.
         * @param view      Frame snapshot supplying the lights and the shadowed sun's casters.
         * @param glView    GPU resource mirror for the casters' meshes.
         * @param resources Resolves the materials the capture syncs.
         * @param globalIBL Baked environment: lights the surfels + fills the background.
         */
        void relight(
            Vkm::GL::Context& gl,
            GLIrradianceVolume& volume,
            const IrradianceVolumeData& data,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& globalIBL
        );

        /// Free the atlas and every grid once the scene has no volume: one added again bakes anew.
        void release(GLIrradianceVolume& volume);

        /// Whether a relight has gathers left to take.
        bool relighting() const { return m_nextBounce > 0; }

        /**
         * @brief Size the surfel atlas and its FBO for @p probes tiles, keeping a fit.
         *
         * @param probes Probes the atlas holds a tile for.
         */
        void ensureAtlas(uint32_t probes);

        /**
         * @brief Draw every probe's surfel cube into its tile of the atlas.
         *
         * @param gl   Live GL context the captures draw through.
         * @param data The volume's world box + grid resolution.
         */
        void captureSurfels(Vkm::GL::Context& gl, const IrradianceVolumeData& data);

        /**
         * @brief Run every gather over the surfels at once and trade the last into @p volume.
         *
         * The first gather's verdicts give the repair; a grid where none was trusted ends the
         * lighting there.
         *
         * @param volume    The grid that takes the light.
         * @param data      The volume's world box + grid resolution.
         * @param globalIBL The sky the surfels are lit by and see.
         * @return False when no probe was trusted, leaving @p volume as it was.
         */
        bool light(GLIrradianceVolume& volume, const IrradianceVolumeData& data, const GLIBL& globalIBL);

        /**
         * @brief Light every probe once into gather @p bounce's grid, the surfels inside the box
         *        by the grid the gather before made.
         *
         * Each probe's verdict rides in sh0's alpha. The grids alternate, gather 0 in the first.
         *
         * @param bounce    Which gather: the first lights the surfels inside the box by nothing.
         * @param data      The volume's world box + grid resolution.
         * @param globalIBL The sky the surfels are lit by and see.
         */
        void gather(int bounce, const IrradianceVolumeData& data, const GLIBL& globalIBL);

        /**
         * @brief Replace @p grid's refused probes by the bake's blends of trusted ones.
         *
         * @param grid A gather's grid.
         */
        void repair(GLIrradianceVolume& grid);

        /**
         * @brief List, per probe cell, the lights whose reach meets it, and bind the lists.
         *
         * A surfel reads the directional lights, and the lights listed for its cell, or, outside
         * the box, those reaching out of it - never every light. Indices into the capture's light
         * upload, in order; a cell keeps the first Config::MAX_LIGHTS_PER_CLUSTER, as a forward
         * cluster does.
         *
         * @param view   Supplies the lights.
         * @param data   The volume's world box + grid resolution.
         * @param glView Its stream carries the lists, per-frame data as a relight under a moving
         *               sun makes them.
         */
        void listLights(const RenderView& view, const IrradianceVolumeData& data, GLView& glView);

        /**
         * @brief Read @p judged's verdicts back and record the repair they call for.
         *
         * @param judged The first gather's grid, unrepaired.
         * @param data   The volume's grid resolution.
         * @return How many probes were refused.
         */
        uint32_t recordRepair(const GLIrradianceVolume& judged, const IrradianceVolumeData& data);

    private:
        Vkm::GL::ComputeShader m_relight;  ///< Surfels -> SH-L1 coefficients + the probe's verdict.
        Vkm::GL::ComputeShader m_fill;     ///< Refused probes -> blends of trusted ones.

        std::unique_ptr<Vkm::GL::Texture2D> m_atlas;            ///< Every probe's surfels, a tile each.
        Vkm::GL::FrameBuffer                m_fbo;
        uint32_t                            m_tilesPerRow = 0;  ///< Probe tiles across the atlas.

        /// probeFillWeights, as the fill reads it.
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_repair;
        uint32_t                                      m_refused = 0;  ///< Probes the repair fills.

        std::vector<uint32_t>                      m_lightWords;   ///< As listLights() last built them.
        std::vector<uint32_t>                      m_directional;  ///< listLights() scratch, kept.
        std::vector<uint32_t>                      m_outside;
        std::vector<std::pair<uint32_t, uint32_t>> m_reaches;      ///< (cell, light) a sphere meets.
        std::vector<uint32_t>                      m_cellFill;     ///< Per cell, lights written.

        GLIrradianceVolume m_scratch[2];      ///< The gathers' grids, in turn: the last becomes the frame's.
        int                m_nextBounce = 0;  ///< The relight's next gather; 0 when none is under way.

        BakedFrom<Signature>     m_bakedFrom;  ///< What update() last baked.
        CaptureSettle<Signature> m_asked;      ///< What the frames ask a bake for.

        /// The lighting update() last saw; a change asks for a relight.
        BakedFrom<CaptureLighting> m_litBy;
        /// The light changed since the last relight began: one is owed.
        bool                       m_relightAsked = false;

        GLSceneCapture& m_capture;  ///< Scene -> the surfel tiles; its shadowed sun's map lights them.
};

} // namespace Vkm::Engine
