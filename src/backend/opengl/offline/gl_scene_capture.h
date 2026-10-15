#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "gl_frame_buffer.h"
#include "gl_shader.h"

#include "core/math/bounds.h"
#include "frame/gl_camera.h"
#include "frame/gl_ocean.h"
#include "frame/gl_lights.h"
#include "frame/gl_shadow_data.h"
#include "frame/gl_instance_batcher.h"
#include "system/render/data/irradiance_volume_data.h"

namespace Vkm::GL {
    class Context;
    class Sampler;
    class Texture2D;
    class UniformBuffer;
}

namespace Vkm::Engine {

class GLIBL;
class GLIrradianceVolume;
class GLMesh;
class GLObjectBuffer;
class GLView;
class ResourceManager;
struct RenderView;

/**
 * @brief Offline capture of the opaque scene + skybox into the six faces of a cube.
 *
 * Full forward PBR and the global sky, lit by direct lights, the global IBL and, where one is
 * lent (setAmbientVolume), an irradiance volume: probes, clusters and screen-space inputs off,
 * which is also the recursion guard. The destination arrives as a face-attach callback, as
 * GLCubeConvolver takes its own. The shadowed sun (lowestSlotDirectionalCaster) casts through a
 * map of the capture's own, since
 * the frame's atlas fits the camera and without one the sun lights a room's floor through its
 * roof; other lights capture unshadowed.
 *
 * begin() prepares what every face of every position shares, and captureCube() draws one cube
 * per call - or captureFace() one face, captureSurfels() what an irradiance probe sees as
 * surfaces - so a probe grid batches the scene once; the caller owns the FBO and the state around
 * the loop. beginLighting() prepares the lights and the sun's map alone, for a relight of
 * what was captured. Lent to every consumer (see GLBackend): the PBR ubershader is the engine's
 * most expensive program. It binds its own camera, lights and shadow blocks and the 2D shadow
 * slot, so capture at the end of a frame.
 */
class GLSceneCapture {
    public:
        /// Points the draws at a face of the destination: attaches it, or sets its viewport.
        using AttachFace = std::function<void(int face)>;

        /**
         * @brief Compile the capture programs, drawing the sky with @p cube.
         *
         * @param cube    The backend's unit cube, outliving this capture.
         * @param objects The backend's object buffer, outliving this capture; a capture draws
         *                through the frame's uploaded transforms, so it runs inside that frame.
         */
        GLSceneCapture(const GLMesh& cube, const GLObjectBuffer& objects);
        ~GLSceneCapture();

        GLSceneCapture(const GLSceneCapture& other) = delete;
        GLSceneCapture& operator=(const GLSceneCapture& other) = delete;

        GLSceneCapture(GLSceneCapture && other) = delete;
        GLSceneCapture& operator=(GLSceneCapture && other) = delete;

    public:
        /**
         * @brief Prepare the scene the captures after it draw.
         *
         * Takes the view's scene-wide objects, not the camera's, syncing their materials into
         * @p glView (GLView::sync skips most), and draws the sun's map fitted to @p region.
         * The captured surfaces take their ambient from the sky until setAmbientVolume().
         *
         * @param gl        Live GL context the shadow map is drawn on.
         * @param view      Supplies the objects and the lights.
         * @param glView    GPU resource mirror the capture draws through.
         * @param resources Resolves the materials the frame has not synced.
         * @param ibl       Global IBL: the ambient term while capturing, and the background.
         * @param faceSize  Face resolution, every face camera's viewport.
         * @param region    World box the captures describe (a probe's influence box, an irradiance
         *                  volume), which the sun's shadow covers; outside is unshadowed.
         */
        void begin(
            Vkm::GL::Context& gl,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& ibl,
            float faceSize,
            const Math::AABB& region
        );

        /**
         * @brief Prepare what lights the scene - the lights, and the sun's map fitted to @p region -
         *        and none of what a capture draws.
         *
         * For a pass that lights what was captured before (GLIrradianceBaker::relight). The map
         * is drawn again only when its ShadowKey changes - the sun, its direction rounded to steps
         * of about a fifth of a degree, its biases, @p region - or a capture elsewhere took its
         * slot (SHADOW_SLOTS), so a scene whose small lights move every frame does not draw its
         * casters every frame; a caster moving under a still sun keeps the shadow it cast, as the
         * captured surfaces keep their place.
         *
         * @param gl        Live GL context the shadow map is drawn on.
         * @param view      Supplies the lights and the casters.
         * @param glView    GPU resource mirror the casters draw through.
         * @param resources Resolves the materials the frame has not synced.
         * @param region    World box the sun's shadow covers.
         */
        void beginLighting(
            Vkm::GL::Context& gl,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const Math::AABB& region
        );

        /**
         * @brief Light the captured surfaces inside @p box from @p volume rather than the sky.
         *
         * Their diffuse ambient is the volume's wherever it covers them, with no fade at the
         * box's faces, so a wall on a face takes none of the sky, and their reflection of the sky
         * is dimmed by it as the frame dims it (Lazarov). Outside the box they keep the sky's.
         * Holds until the next begin().
         *
         * @param volume A grid other than the one being written, outliving the captures; null
         *               for the sky alone.
         * @param box    The world box @p volume fills.
         */
        void setAmbientVolume(const GLIrradianceVolume* volume, const IrradianceVolumeData& box);

        /**
         * @brief Draw the six faces of one cube centred on @p position.
         *
         * @param gl       Live GL context the capture draws on.
         * @param position World-space centre the six faces look out from.
         * @param farPlane Capture far plane; geometry beyond it misses the cube.
         * @param attach   Attaches the destination face + its viewport, once per face.
         */
        void captureCube(
            Vkm::GL::Context& gl,
            const glm::vec3& position,
            float farPlane,
            const AttachFace& attach
        );

        /**
         * @brief Draw face @p face of the cube captureCube() draws, alone.
         *
         * For a capture spread over frames; begin() must have run in the same frame, since the
         * frame between rebinds the camera, lights and shadow blocks.
         *
         * @param gl       Live GL context the capture draws on.
         * @param face     Cube face index (0..5).
         * @param position World-space centre the face looks out from.
         * @param farPlane Capture far plane; geometry beyond it misses the cube.
         * @param attach   Attaches the destination face + its viewport.
         */
        void captureFace(
            Vkm::GL::Context& gl,
            int face,
            const glm::vec3& position,
            float farPlane,
            const AttachFace& attach
        );

        /**
         * @brief Draw what the six faces around @p position see as surfels, to be lit later.
         *
         * The nearest surface through each texel, packed as shaders/irradiance/surfel.glsl says,
         * into the RGBA32UI target the caller attached. Culling off: a probe inside a solid sees
         * the back of what encloses it, and the surfel says so. No sky, and no clear: a probe's
         * faces are tiles of one atlas, cleared once by the caller to zero, which reads as sky.
         *
         * @param gl       Live GL context the capture draws on.
         * @param position World-space centre the six faces look out from.
         * @param farPlane Capture far plane; geometry beyond it misses the cube.
         * @param attach   Sets the face's viewport, once per face.
         */
        void captureSurfels(
            Vkm::GL::Context& gl,
            const glm::vec3& position,
            float farPlane,
            const AttachFace& attach
        );

        /**
         * @brief Draw the baked environment behind whatever the bound camera frames.
         *
         * At the far plane, depth writes and culling off, no sun disc (it would blow out a
         * capture). Leaves depth as the geometry after it wants: LESS, writes on.
         *
         * @param gl         Live context whose depth and cull state the backdrop sets.
         * @param ibl        The baked environment, found ready by the caller.
         * @param projection The bound camera's projection, which the sky draws by.
         * @param intensity  What the environment's radiance is scaled by.
         */
        void drawSky(
            Vkm::GL::Context& gl,
            const GLIBL& ibl,
            const glm::mat4& projection,
            float intensity = 1.0f
        );

        /**
         * @brief The rig's PBR program, lent to whatever else draws the scene's surfaces.
         *
         * Its uniforms are the last caller's: begin() puts every one back as linked before
         * setting its own, so a borrower sets each it reads before drawing, and never draws
         * between a begin() and the captures after it.
         *
         * @return The capture's forward PBR program.
         */
        Vkm::GL::Shader& pbrProgram() { return m_pbr; }

    private:
        /// What the sun's map was drawn for; drawn again when any of it changes.
        struct ShadowKey {
            Math::AABB region{};
            bool       casts      = false;
            uint32_t   light      = 0;  ///< The sun's entity slot.
            glm::vec3  direction{0.0f};
            float      bias       = 0.0f;
            float      normalBias = 0.0f;

            bool operator==(const ShadowKey& other) const {
                return region.min == other.region.min && region.max == other.region.max
                    && casts == other.casts && light == other.light && direction == other.direction
                    && bias == other.bias && normalBias == other.normalBias;
            }
        };

        /// The sun's depth over one region, and the ShadowBlock that reads it.
        struct ShadowSlot {
            std::optional<ShadowKey>                key;    ///< What it was drawn for; none yet.
            bool                                    casts = false;
            std::unique_ptr<Vkm::GL::Texture2D>     map;    ///< Made on first use.
            std::unique_ptr<Vkm::GL::UniformBuffer> ubo;
            ShadowUBOData                           last{};   ///< What ubo holds.
            uint64_t                                used = 0;  ///< When it was last bound.
        };

        /// Maps kept: one for the irradiance volume a relight lights and one for the probe being
        /// captured, so neither draws the other's away every frame both run.
        static constexpr int SHADOW_SLOTS = 2;

        /// A uniform of m_pbr's outside a block and not a sampler, which resetPbr() zeroes.
        struct PlainUniform {
            int32_t  location = -1;
            int32_t  count    = 0;  ///< Its array's length, 1 for a single value.
            uint32_t type     = 0;  ///< Its GL type.
        };

        /**
         * @brief Bind m_pbr and put every plain uniform back as linked: zero, as no shader
         *        gives one another value.
         *
         * Whatever else drew with the program last left its own there (pbrProgram()).
         */
        void resetPbr();

        /**
         * @brief What @p view's sun and @p region would draw the map for.
         *
         * @param view   Supplies the lights.
         * @param region World box the map covers.
         * @return The key.
         */
        static ShadowKey shadowKey(const RenderView& view, const Math::AABB& region);

        /**
         * @brief Gather the scene-wide objects a capture draws and the sun's map casts.
         *
         * @param view        Supplies the objects.
         * @param glView      GPU resource mirror whose materials are synced.
         * @param resources   Resolves the materials the frame has not synced.
         * @param castersOnly Gather the casters alone, the scene list's lead, for a map and no
         *                    capture.
         */
        void gatherScene(
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            bool castersOnly = false
        );

        /**
         * @brief Bind the lights, and shadow slot @p slot's map and ShadowBlock while it casts.
         *
         * @param slot Which of m_shadows.
         * @param view Supplies the lights.
         */
        void useShadow(int slot, const RenderView& view);

        /**
         * @brief Attach face @p face, clear it, and put the camera on it.
         *
         * @param gl       Live GL context.
         * @param face     Cube face index (0..5).
         * @param position World-space centre the face looks out from.
         * @param proj     The 90-degree projection shared by all six.
         * @param attach   Attaches the destination face + its viewport.
         */
        void beginFace(
            Vkm::GL::Context& gl,
            int face,
            const glm::vec3& position,
            const glm::mat4& proj,
            const AttachFace& attach
        );

        /**
         * @brief Draw and bind the sun's shadow over @p region, or none when no sun casts.
         *
         * Into the slot last drawn for the same sun and region, or else the one bound
         * longest ago. Binds the lights with their slots and a one-"cascade" ShadowBlock over the
         * frame's. Reads the casters gatherScene() gathered.
         *
         * @param gl     Live GL context.
         * @param view   Supplies the lights and the casters' bounds.
         * @param region World box the map covers, across the light.
         */
        void prepareShadow(Vkm::GL::Context& gl, const RenderView& view, const Math::AABB& region);

    private:
        Vkm::GL::Shader m_pbr;           ///< Capture geometry (full forward PBR).
        Vkm::GL::Shader m_skybox;        ///< Capture background.
        Vkm::GL::Shader m_surfels;       ///< Capture what a probe sees, as surfels.
        Vkm::GL::Shader m_shadowDepth;   ///< The sun's depth; the shadow pass's program.
        Vkm::GL::Shader m_shadowMasked;  ///< The same, cut by an alpha-masked caster's material.

        const GLMesh&         m_cube;     ///< Unit cube the skybox draws; the backend's.
        const GLObjectBuffer& m_objects;  ///< The frame's transforms the batch indexes; the backend's.

        GLCamera          m_camera;         ///< Per-face camera UBO.
        GLOcean           m_ocean;          ///< No sea: a `waves` surface is an ordinary one here.
        GLLights          m_lights;         ///< The frame's lights, the sun alone shadowed.
        GLInstanceBatcher m_batcher;        ///< Instanced capture draws.
        GLInstanceBatcher m_casterBatcher;  ///< Its casters, for the sun's map.

        std::vector<uint32_t> m_opaque;      ///< begin()'s opaque scene-wide objects.
        std::vector<uint32_t> m_casters;     ///< The opaque objects that cast, a prefix of m_opaque.
        std::vector<uint32_t> m_overRegion;  ///< The casters over the map's region, which it draws.
        std::vector<int>      m_lightSlots;  ///< Per light: 0 for the sun while it casts, else -1.

        Vkm::GL::FrameBuffer              m_shadowFbo;
        ShadowSlot                        m_shadows[SHADOW_SLOTS];
        std::unique_ptr<Vkm::GL::Sampler> m_shadowSampler;    ///< Compares, as the atlas's does.
        uint64_t                          m_shadowClock = 0;  ///< Counts binds, for ShadowSlot::used.

        std::vector<PlainUniform> m_pbrUniforms;     ///< m_pbr's plain uniforms, listed once a link.
        uint32_t                  m_pbrListed = 0;   ///< The program m_pbrUniforms lists, 0 for none.
        std::vector<GLfloat>      m_pbrZeros;        ///< Zeros enough for the largest of them.
        std::vector<GLint>        m_pbrIntZeros;     ///< The same, as ints.
        std::vector<GLuint>       m_pbrUintZeros;    ///< The same, as unsigned ints.

        const GLView* m_glView   = nullptr;  ///< begin()'s scene, drawn by the captures after it.
        const GLIBL*  m_ibl      = nullptr;
        float         m_faceSize = 0.0f;     ///< begin()'s face resolution: every face camera's viewport.

        const GLIrradianceVolume* m_volume = nullptr;  ///< The captured surfaces' ambient; null for the sky.
        IrradianceVolumeData      m_volumeBox{};       ///< Where m_volume lies, as the captures read it.
};

/**
 * @brief Bind @p pbr and set the uniforms every offline draw shares.
 *
 * The per-frame inputs (AO, scene colour copy, clusters, probes, irradiance volume, fog) exist
 * for no offscreen draw, so each is off - the volume until GLSceneCapture::setAmbientVolume lends
 * one; u_probeCount = 0 also stops probe bakes recursing.
 * u_iblIntensity is one, not the scene's: the shader scales probe and volume reads by it, so a
 * capture would carry it twice. Set, because unset is 0 and drops the indirect term. The render
 * size is the camera's (GLCamera::update).
 *
 * @param pbr Forward PBR program the offline draw uses.
 * @param ibl Global IBL, bound as the ambient term once it is ready.
 */
void bindOfflinePbrUniforms(Vkm::GL::Shader& pbr, const GLIBL& ibl);

} // namespace Vkm::Engine
