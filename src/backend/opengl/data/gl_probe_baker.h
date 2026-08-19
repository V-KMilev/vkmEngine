#pragma once

#include <glm/glm.hpp>

namespace Vkm::GL {
    class Context;
}

namespace Engine {

class GLCubeConvolver;
class GLIBL;
class GLProbeArray;
class GLSceneCapture;
class GLView;
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
 * The capture and the convolution machinery are the backend's, borrowed rather
 * than owned - both hold expensive programs (the forward PBR ubershader among
 * them) that no baker has a reason to compile a second copy of. bake() re-binds
 * the camera / lights UBOs with its own per-face / no-shadow data; the next
 * rendered frame re-uploads its own, so run the bake at the end of a frame.
 */
class GLProbeBaker {
    public:
        /**
         * @brief Bind the shared capture + convolution rig this baker draws with.
         *
         * @param capture   Shared scene capture, owned by the backend and outliving this baker.
         * @param convolver Shared cube convolver, owned by the backend and outliving this baker.
         */
        GLProbeBaker(GLSceneCapture& capture, GLCubeConvolver& convolver);
        ~GLProbeBaker();

        GLProbeBaker(const GLProbeBaker& other) = delete;
        GLProbeBaker& operator=(const GLProbeBaker& other) = delete;

        GLProbeBaker(GLProbeBaker && other) = delete;
        GLProbeBaker& operator=(GLProbeBaker && other) = delete;

        /**
         * @brief Bake the probe at @p layer of @p arr from @p position.
         *
         * @param gl        Live GL context the capture + convolution passes run on.
         * @param arr       Cube-map array whose layer receives the baked irradiance + prefilter.
         * @param layer     Array layer (probe slot) to write.
         * @param position  World-space centre the six faces are captured from.
         * @param view      Render view supplying the scene drawables and lights to capture.
         * @param glView    GPU-side mirror of the scene used to issue the capture draws.
         * @param globalIBL Global IBL used as the ambient term while capturing.
         */
        void bake(Vkm::GL::Context& gl, GLProbeArray& arr, int layer, const glm::vec3& position,
                  const RenderView& view, const GLView& glView, const GLIBL& globalIBL);

    private:
        void captureFaces(Vkm::GL::Context& gl, GLProbeArray& arr, const glm::vec3& position,
                          const RenderView& view, const GLView& glView, const GLIBL& globalIBL);
        void convolve(Vkm::GL::Context& gl, GLProbeArray& arr, int layer);

    private:
        GLSceneCapture&  m_capture;    ///< scene -> the six env-cube faces
        GLCubeConvolver& m_convolver;  ///< env cube -> irradiance + prefilter
};

} // namespace Engine
