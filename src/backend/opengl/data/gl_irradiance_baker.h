#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "gl_compute_shader.h"
#include "gl_frame_buffer.h"
#include "gl_render_buffer.h"
#include "gl_texture_cube.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLIBL;
class GLIrradianceVolume;
class GLSceneCapture;
class GLView;
struct RenderView;
struct IrradianceVolumeData;

/**
 * @brief Bakes an irradiance volume: a scene capture per grid probe, projected
 * to SH-L1.
 *
 * Per probe: render the opaque scene (instanced, full PBR) plus the global skybox
 * into a small cube from that grid point, then dispatch a compute pass that
 * integrates the cube against the SH basis and stores the coefficients in the
 * volume at the probe's cell. The capture mirrors GLProbeBaker's - no shadows
 * (the camera's atlas doesn't cover these viewpoints) and probes disabled, which
 * is also the recursion guard.
 *
 * The capture faces are deliberately small: the result is a 4-coefficient
 * spherical average, so face resolution buys almost nothing while multiplying the
 * bake by the probe count.
 *
 * Owns the SH projection program + the capture targets, so construct it where a
 * GL context is current; the scene capture itself is the backend's, borrowed so
 * the forward PBR ubershader is compiled once for every offline consumer. bake()
 * re-binds the camera / light UBOs with its own data; run it at the end of a
 * frame, like the reflection-probe bake.
 */
class GLIrradianceBaker {
    public:
        static constexpr int CAPTURE_SIZE = 32;  ///< Per-face capture resolution.

        /**
         * @brief Compile the SH projection program, drawing through @p capture.
         *
         * @param capture Shared scene capture, owned by the backend and outliving this baker.
         */
        explicit GLIrradianceBaker(GLSceneCapture& capture);
        ~GLIrradianceBaker();

        GLIrradianceBaker(const GLIrradianceBaker& other) = delete;
        GLIrradianceBaker& operator=(const GLIrradianceBaker& other) = delete;

        GLIrradianceBaker(GLIrradianceBaker && other) = delete;
        GLIrradianceBaker& operator=(GLIrradianceBaker && other) = delete;

        /**
         * @brief Bake every probe of @p data into @p volume.
         *
         * @param volume    Destination SH volume (resized to the grid by the caller).
         * @param data      The volume's world box + grid resolution.
         * @param view      Frame snapshot supplying the geometry + lights to capture.
         * @param glView    GPU resource mirror for material/mesh lookups.
         * @param globalIBL Baked environment: lights the capture + fills the background.
         */
        void bake(Vkm::GL::Context& gl, GLIrradianceVolume& volume,
                  const IrradianceVolumeData& data,
                  const RenderView& view, const GLView& glView, const GLIBL& globalIBL);

    private:
        /**
         * @brief Allocate the capture cube + FBO on first use.
         */
        void ensureTargets();

    private:
        Vkm::GL::ComputeShader m_project;  ///< cube -> SH-L1 coefficients

        Vkm::GL::TextureCube                   m_cube;   ///< small per-probe capture cube
        Vkm::GL::FrameBuffer                   m_fbo;
        std::unique_ptr<Vkm::GL::RenderBuffer> m_depth;

        GLSceneCapture& m_capture;  ///< scene -> the six faces of m_cube
};

} // namespace Vkm::Engine
