#pragma once

#include <vector>

#include "offline/gl_cube_convolver.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLIBL;
class GLIrradianceVolume;
class GLProbeArray;
class GLSceneCapture;
class GLView;
class ResourceManager;
struct ProbeData;
struct RenderView;

/**
 * @brief Bakes a reflection probe from the live scene.
 *
 * Renders the opaque scene and the global sky into the six env-cube faces, lit by direct lights,
 * the GLOBAL IBL and the baked irradiance volume where it covers them (other probes would
 * recurse), the shadowed sun cast over the influence box by a map of the capture's own, since
 * the frame's atlas fits the camera. Then GLCubeConvolver makes the irradiance cube and the
 * GGX-prefiltered specular mips.
 *
 * bake() does it at once. beginRefresh() and advance() do it a step a frame - a face of the
 * capture, then GLCubeConvolver::slices's steps - into the arrays' staging layer, copied into the
 * probe's after the last: how a probe follows a change of light without a frame paying for it.
 *
 * The capture and convolver are the backend's, borrowed: both hold expensive programs. bake() and
 * advance() bind their own camera, lights and shadow blocks, so run them at the end of a frame.
 */
class GLProbeBaker {
    public:
        /// A refresh's steps before the convolution's: one a cube face.
        static constexpr int CAPTURE_STEPS = 6;

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
         * Ends a refresh under way: the bake draws into the env cube it was drawing into.
         *
         * @param gl        Live GL context the capture draws through.
         * @param arr       The shared arrays the bake lands in.
         * @param layer     The probe's layer in them.
         * @param probe     Where it stands and what box it covers.
         * @param view      Supplies the scene and its lights.
         * @param glView    GPU mirror the capture draws from.
         * @param resources Resolves what the capture draws.
         * @param globalIBL The environment, the capture's backdrop and ambient.
         * @param volume    The baked irradiance volume, at the view's box, lighting the captured
         *                  surfaces it covers; null for none.
         */
        void bake(
            Vkm::GL::Context& gl,
            GLProbeArray& arr,
            int layer,
            const ProbeData& probe,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& globalIBL,
            const GLIrradianceVolume* volume
        );

        /**
         * @brief Start refreshing a probe in @p arr's staging layer, a step a frame.
         *
         * Every advance() after it draws into the arrays' env cube; a bake() between ends it.
         *
         * @param arr The shared arrays, at the resolution the refresh is cut for.
         */
        void beginRefresh(const GLProbeArray& arr);

        /**
         * @brief How many advance() calls a refresh takes for a probe asking @p resolution.
         *
         * @param resolution The capture resolution asked for, clamped as the arrays clamp it.
         * @return CAPTURE_STEPS and the convolution's steps.
         */
        static int refreshSteps(int resolution);

        /**
         * @brief Take the next step of the refresh beginRefresh() started.
         *
         * @param gl        Live GL context the step draws through.
         * @param arr       The shared arrays.
         * @param layer     The probe's layer, which the finished refresh replaces.
         * @param probe     Where it stands and what box it covers.
         * @param view      Supplies the scene and its lights.
         * @param glView    GPU mirror the capture draws from.
         * @param resources Resolves what the capture draws.
         * @param globalIBL The environment, the capture's backdrop and ambient.
         * @param volume    The irradiance volume lighting the captured surfaces; null for none.
         * @return Whether that was the last step, and @p layer now holds the refresh.
         */
        bool advance(
            Vkm::GL::Context& gl,
            GLProbeArray& arr,
            int layer,
            const ProbeData& probe,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& globalIBL,
            const GLIrradianceVolume* volume
        );

        /// Whether a refresh has steps left.
        bool refreshing() const { return m_next < idle(); }

        /// Drop the refresh under way, if any.
        void endRefresh() { m_next = idle(); }

    private:
        /// m_next past the last step: the faces', then m_steps.
        int idle() const { return CAPTURE_STEPS + static_cast<int>(m_steps.size()); }

        /**
         * @brief Begin a capture of @p probe's scene, lit as it lights it.
         *
         * @param gl        Live GL context.
         * @param arr       The shared arrays, whose resolution is the face size.
         * @param probe     The probe captured.
         * @param view      Supplies the scene and its lights.
         * @param glView    GPU mirror the capture draws from.
         * @param resources Resolves what the capture draws.
         * @param globalIBL The environment, the capture's backdrop and ambient.
         * @param volume    The irradiance volume lighting the captured surfaces; null for none.
         */
        void beginCapture(
            Vkm::GL::Context& gl,
            const GLProbeArray& arr,
            const ProbeData& probe,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& globalIBL,
            const GLIrradianceVolume* volume
        );

        void captureFaces(
            Vkm::GL::Context& gl,
            GLProbeArray& arr,
            const ProbeData& probe,
            const RenderView& view,
            GLView& glView,
            const ResourceManager& resources,
            const GLIBL& globalIBL,
            const GLIrradianceVolume* volume
        );

        void convolve(Vkm::GL::Context& gl, GLProbeArray& arr, int layer);

    private:
        GLSceneCapture&  m_capture;    ///< Scene -> the six env-cube faces.
        GLCubeConvolver& m_convolver;  ///< Env cube -> irradiance + prefilter.

        /// The refresh's convolution, after its CAPTURE_STEPS.
        std::vector<GLCubeConvolver::Step> m_steps;
        /// The refresh's next step, the faces first; idle() when none runs.
        int m_next = CAPTURE_STEPS;
};

} // namespace Vkm::Engine
