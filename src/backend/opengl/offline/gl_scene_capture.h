#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_frame_buffer.h"
#include "gl_shader.h"

#include "core/math/bounds.h"
#include "frame/gl_camera.h"
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
 * GLCubeConvolver takes its own. The key light casts through a map of the capture's own, since
 * the frame's atlas fits the camera and without one the sun lights a room's floor through its
 * roof; other lights capture unshadowed.
 *
 * begin() prepares what every face of every position shares, and captureCube() draws one cube
 * per call, so a probe grid batches the scene once; the caller owns the FBO and the state around
 * the loop. Lent to every consumer (see GLBackend): the PBR ubershader is the engine's most
 * expensive program. It binds its own camera, lights and shadow blocks and the 2D shadow slot,
 * so capture at the end of a frame.
 */
class GLSceneCapture {
    public:
        using AttachFace = std::function<void(int face)>;  ///< Attaches the destination face as colour 0.

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
         * @brief Prepare the scene every following captureCube() draws.
         *
         * Takes the view's scene-wide objects, not the camera's, syncing their materials into
         * @p glView (GLView::sync skips most), and draws the key light's map fitted to @p region.
         * The captured surfaces take their ambient from the sky until setAmbientVolume().
         *
         * @param gl        Live GL context the shadow map is drawn on.
         * @param view      Supplies the objects and the lights.
         * @param glView    GPU resource mirror the capture draws through.
         * @param resources Resolves the materials the frame has not synced.
         * @param ibl       Global IBL: the ambient term while capturing, and the background.
         * @param faceSize  Face resolution, every face camera's viewport.
         * @param region    World box the captures describe (a probe's influence box, an irradiance
         *                  volume), which the key light's shadow covers; outside is unshadowed.
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
         * @brief Draw the same six faces as a backface mask: 1 where the nearest surface faces away.
         *
         * captureCube() culls back faces, so from inside a solid it sees through the walls. This
         * draws with culling off, so a probe enclosed by geometry has a mask of mostly 1. No sky:
         * a direction reaching it keeps the cleared 0. Position-only, so it costs a fraction of
         * the radiance capture.
         *
         * @param gl       Live GL context the capture draws on.
         * @param position World-space centre the six faces look out from.
         * @param farPlane Capture far plane; must match captureCube()'s to see the same geometry.
         * @param attach   Attaches the destination face + its viewport, once per face.
         */
        void captureBackfaceCube(
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
         * @brief The offline rig, for a consumer that draws its own scene rather than a cube.
         *
         * Call bindOfflinePbrUniforms() with your own framing before drawing, as begin() does, and
         * never draw between a begin() and its captureCube()s: a shared program's uniforms are the
         * last caller's.
         *
         * @return The capture's forward PBR program.
         */
        Vkm::GL::Shader& pbrProgram() { return m_pbr; }

    private:
        /**
         * @brief Attach face @p face, clear it, and put the camera on it.
         *
         * Shared by both capture passes, so one framing is kept in step with GLCubemap::faceView.
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
         * @brief Draw and bind the key light's shadow over @p region, or none when it casts none.
         *
         * Binds the lights with their slots and a one-"cascade" ShadowBlock over the frame's.
         * Reads the casters begin() gathered.
         *
         * @param gl     Live GL context.
         * @param view   Supplies the lights and the casters' bounds.
         * @param region World box the map covers, across the light.
         */
        void prepareShadow(Vkm::GL::Context& gl, const RenderView& view, const Math::AABB& region);

    private:
        Vkm::GL::Shader m_pbr;           ///< Capture geometry (full forward PBR).
        Vkm::GL::Shader m_skybox;        ///< Capture background.
        Vkm::GL::Shader m_backface;      ///< Capture which surfaces face away.
        Vkm::GL::Shader m_shadowDepth;   ///< The key light's depth; the shadow pass's program.
        Vkm::GL::Shader m_shadowMasked;  ///< The same, cut by an alpha-masked caster's material.

        const GLMesh&         m_cube;     ///< Unit cube the skybox draws; the backend's.
        const GLObjectBuffer& m_objects;  ///< The frame's transforms the batch indexes; the backend's.

        GLCamera          m_camera;         ///< Per-face camera UBO.
        GLLights          m_lights;         ///< The frame's lights, the key light alone shadowed.
        GLInstanceBatcher m_batcher;        ///< Instanced capture draws.
        GLInstanceBatcher m_casterBatcher;  ///< Its casters, for the key light's map.

        std::vector<uint32_t> m_opaque;      ///< begin()'s opaque scene-wide objects.
        std::vector<uint32_t> m_casters;     ///< The opaque objects that cast, a prefix of m_opaque.
        std::vector<int>      m_lightSlots;  ///< Per light: 0 for the key light while it casts, else -1.

        Vkm::GL::FrameBuffer                    m_shadowFbo;
        /// The key light's depth over the region, made on first use.
        std::unique_ptr<Vkm::GL::Texture2D>     m_shadowMap;
        std::unique_ptr<Vkm::GL::Sampler>       m_shadowSampler;  ///< Compares, as the atlas's does.
        std::unique_ptr<Vkm::GL::UniformBuffer> m_shadowUbo;      ///< The ShadowBlock the captures read.
        ShadowUBOData                           m_shadowLast{};   ///< What m_shadowUbo holds.

        const GLView* m_glView   = nullptr;  ///< begin()'s scene, drawn by every captureCube().
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
