#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_shader.h"

#include "data/gl_camera.h"
#include "data/gl_lights.h"
#include "data/gl_shadow_data.h"
#include "data/gl_instance_batcher.h"
#include "data/gl_cube_convolver.h"

namespace Core {
    class Context;
}

namespace Engine {

class GLProbeArray;
class GLView;
class GLIBL;
struct RenderView;

/**
 * @brief Bakes a reflection probe from the live scene.
 *
 * Per probe: render the opaque scene (instanced, full PBR) + the global skybox
 * from the probe position into the six env-cube faces, lit by direct lights +
 * the GLOBAL IBL only - never other probes, which would recurse - and with
 * shadows off (the frame's shadow atlas is fit to the camera, not the probe).
 * Then convolve the env cube into the diffuse irradiance cube and GGX-prefilter
 * it into the specular mips, reusing the ibl/irradiance + ibl/prefilter shaders.
 *
 * Owns the bake-only programs + a unit cube, so construct it where a GL context
 * is current (GLBackend::init). bake() re-binds the camera / lights UBOs with
 * its own per-face / no-shadow data; the next rendered frame re-uploads its own,
 * so run the bake at the end of a frame.
 */
class GLProbeBaker {
    public:
        GLProbeBaker();
        ~GLProbeBaker();

        GLProbeBaker(const GLProbeBaker& other) = delete;
        GLProbeBaker& operator=(const GLProbeBaker& other) = delete;

        GLProbeBaker(GLProbeBaker && other) = delete;
        GLProbeBaker& operator=(GLProbeBaker && other) = delete;

        /// Bake the probe at @p layer of @p arr from @p position.
        void bake(Core::Context& gl, GLProbeArray& arr, int layer, const glm::vec3& position,
                  const RenderView& view, const GLView& glView, const GLIBL& globalIBL);

    private:
        void captureFaces(Core::Context& gl, GLProbeArray& arr, const glm::vec3& position,
                          const RenderView& view, const GLView& glView, const GLIBL& globalIBL);
        void convolve(Core::Context& gl, GLProbeArray& arr, int layer);

        Core::Shader m_pbr;         ///< capture geometry (full forward PBR)
        Core::Shader m_skybox;      ///< capture background

        GLCubeConvolver m_convolver;  ///< env cube -> irradiance + prefilter (and the unit cube)

        GLCamera          m_camera;    ///< per-face camera UBO (binding 2)
        GLLights          m_lights;    ///< no-shadow lights UBO (binding 1)
        GLShadowData      m_noShadow;  ///< default-built: slotForLight() == -1 for every light
        GLInstanceBatcher m_batcher;   ///< instanced capture draws

        std::vector<const DrawableData*> m_opaque;
};

} // namespace Engine
