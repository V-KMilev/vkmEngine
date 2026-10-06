#pragma once

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLCubeConvolver;
class GLIBL;
class GLProbeArray;
class GLSceneCapture;
class GLView;
class ResourceManager;
struct ProbeData;
struct RenderView;

/**
 * @brief Bakes a reflection probe from the live scene.
 *
 * Renders the opaque scene and the global sky into the six env-cube faces, lit by direct lights
 * and the GLOBAL IBL only (other probes would recurse), the key light shadowed over the
 * influence box by a map of the capture's own, since the frame's atlas fits the camera. Then
 * GLCubeConvolver makes the irradiance cube and the GGX-prefiltered specular mips.
 *
 * The capture and convolver are the backend's, borrowed: both hold expensive programs. bake()
 * binds its own camera, lights and shadow blocks, so run it at the end of a frame.
 */
class GLProbeBaker {
    public:
        /**
         * @brief Bind the shared capture and convolution rig this baker draws with.
         *
         * @param capture   Scene capture; the backend's, and outlives this baker.
         * @param convolver Cube convolver; the backend's, and outlives this baker.
         */
        GLProbeBaker(GLSceneCapture& capture, GLCubeConvolver& convolver);
        ~GLProbeBaker();

        GLProbeBaker(const GLProbeBaker& other) = delete;
        GLProbeBaker& operator=(const GLProbeBaker& other) = delete;

        GLProbeBaker(GLProbeBaker && other) = delete;
        GLProbeBaker& operator=(GLProbeBaker && other) = delete;

    public:
        /**
         * @brief Bake @p probe into @p layer of @p arr.
         *
         * @param gl        Live GL context the capture draws through.
         * @param arr       The shared arrays the bake lands in.
         * @param layer     The probe's layer in them.
         * @param probe     Where it stands and what box it covers.
         * @param view      Supplies the scene and its lights.
         * @param glView    GPU mirror the capture draws from.
         * @param resources Resolves what the capture draws.
         * @param globalIBL The environment, the capture's backdrop and ambient.
         */
        void bake(
            Vkm::GL::Context& gl,
            GLProbeArray& arr,
            int layer,
            const ProbeData& probe,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& globalIBL
        );

    private:
        void captureFaces(
            Vkm::GL::Context& gl,
            GLProbeArray& arr,
            const ProbeData& probe,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& globalIBL
        );

        void convolve(Vkm::GL::Context& gl, GLProbeArray& arr, int layer);

    private:
        GLSceneCapture&  m_capture;    ///< Scene -> the six env-cube faces.
        GLCubeConvolver& m_convolver;  ///< Env cube -> irradiance + prefilter.
};

} // namespace Vkm::Engine
