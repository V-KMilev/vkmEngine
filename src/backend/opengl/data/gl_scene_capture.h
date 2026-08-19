#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_shader.h"

#include "data/gl_camera.h"
#include "data/gl_lights.h"
#include "data/gl_shadow_data.h"
#include "data/gl_instance_batcher.h"

namespace Vkm::GL {
    class Context;
}

namespace Engine {

class GLIBL;
class GLMesh;
class GLView;
struct RenderView;

/**
 * @brief Offline capture of the opaque scene + skybox into the six faces of a cube.
 *
 * Both bakers fill a cube from a world position the same way: render the frame's
 * opaque drawables with the full forward PBR shader plus the global skybox, lit
 * by the direct lights and the global IBL only - shadows off (the frame's atlas
 * is fit to the camera, not to these viewpoints) and probes, clusters and every
 * screen-space input off, which is also the recursion guard. Only the
 * destination differs, so it arrives as a face-attach callback, the way
 * GLCubeConvolver takes its own.
 *
 * begin() prepares what every face of every position shares (the no-shadow light
 * set, the opaque batch, the PBR uniforms); captureCube() then draws one cube per
 * call, so a grid of probes batches the scene once. The caller owns the capture
 * FBO and the state around the loop.
 *
 * Owns the bake-only programs + a unit cube, so construct it where a GL context
 * is current. The forward PBR ubershader is the most expensive program in the
 * engine, so the backend owns ONE of these and lends it to every baker rather
 * than each keeping a private copy. It re-binds the camera / lights UBOs with
 * its own per-face data; the next rendered frame re-uploads its own, so capture
 * at the end of a frame.
 */
class GLSceneCapture {
    public:
        GLSceneCapture();
        ~GLSceneCapture();

        GLSceneCapture(const GLSceneCapture& other) = delete;
        GLSceneCapture& operator=(const GLSceneCapture& other) = delete;

        GLSceneCapture(GLSceneCapture && other) = delete;
        GLSceneCapture& operator=(GLSceneCapture && other) = delete;

    public:
        using AttachFace = std::function<void(int face)>;  ///< Attaches the destination face as colour 0.

        /**
         * @brief Prepare the scene every following captureCube() draws.
         *
         * @param view     Frame snapshot supplying the drawables, the lights and the sky intensity.
         * @param glView   GPU resource mirror the capture draws through.
         * @param ibl      Global IBL: the ambient term while capturing, and the background.
         * @param faceSize Capture face resolution, reported to the shader as u_screenSize.
         */
        void begin(const RenderView& view, const GLView& glView, const GLIBL& ibl, float faceSize);

        /**
         * @brief Draw the six faces of one cube centred on @p position.
         *
         * @param gl       Live GL context the capture draws on.
         * @param position World-space centre the six faces look out from.
         * @param farPlane Capture far plane; geometry beyond it misses the cube.
         * @param attach   Attaches the destination face + its viewport, once per face.
         */
        void captureCube(Vkm::GL::Context& gl, const glm::vec3& position, float farPlane,
                         const AttachFace& attach);

    private:
        Vkm::GL::Shader m_pbr;     ///< capture geometry (full forward PBR)
        Vkm::GL::Shader m_skybox;  ///< capture background

        std::unique_ptr<GLMesh> m_cube;  ///< unit cube the skybox draws

        GLCamera          m_camera;    ///< per-face camera UBO (binding 2)
        GLLights          m_lights;    ///< no-shadow lights SSBO (binding 0)
        GLShadowData      m_noShadow;  ///< default-built: slotForLight() == -1 for every light
        GLInstanceBatcher m_batcher;   ///< instanced capture draws

        std::vector<const DrawableData*> m_opaque;

        const GLView* m_glView = nullptr;  ///< begin()'s scene, drawn by every captureCube().
        const GLIBL*  m_ibl    = nullptr;
};

/**
 * @brief Bind @p pbr and set the uniforms every offline draw shares.
 *
 * The forward pass feeds the PBR shader a frame's worth of screen-space inputs -
 * AO, the scene colour copy, the cluster light grid, probes, the irradiance
 * volume - none of which exist off-screen, so every one of them is switched off
 * here; u_probeCount = 0 is also what stops a probe bake recursing into probes.
 * u_iblIntensity has to be set explicitly: the shader does
 * `ambient *= u_iblIntensity` and an unset uniform is 0, which would drop the
 * whole indirect term.
 *
 * @param pbr          Forward PBR program the offline draw uses.
 * @param ibl          Global IBL, bound as the ambient term once it is ready.
 * @param iblIntensity Strength of that indirect term.
 * @param faceSize     Square render size, reported as u_screenSize.
 */
void bindOfflinePbrUniforms(Vkm::GL::Shader& pbr, const GLIBL& ibl, float iblIntensity, float faceSize);

} // namespace Engine
