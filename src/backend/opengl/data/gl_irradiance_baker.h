#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "gl_compute_shader.h"
#include "gl_frame_buffer.h"
#include "gl_render_buffer.h"
#include "gl_texture_cube.h"

#include "data/gl_scene_capture.h"

namespace Core {
    class Context;
}

namespace Engine {

class GLIrradianceVolume;
class GLView;
class GLIBL;
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
 * Owns bake-only programs + targets, so construct it where a GL context is
 * current. bake() re-binds the camera / light UBOs with its own data; run it at
 * the end of a frame, like the reflection-probe bake.
 */
class GLIrradianceBaker {
    public:
        static constexpr int CAPTURE_SIZE = 32;  ///< Per-face capture resolution.

        GLIrradianceBaker();
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
        void bake(Core::Context& gl, GLIrradianceVolume& volume,
                  const IrradianceVolumeData& data,
                  const RenderView& view, const GLView& glView, const GLIBL& globalIBL);

    private:
        /**
         * @brief Allocate the capture cube + FBO on first use.
         */
        void ensureTargets();

    private:
        Core::ComputeShader m_project;  ///< cube -> SH-L1 coefficients

        Core::TextureCube                   m_cube;   ///< small per-probe capture cube
        Core::FrameBuffer                   m_fbo;
        std::unique_ptr<Core::RenderBuffer> m_depth;

        GLSceneCapture m_capture;  ///< scene -> the six faces of m_cube
};

} // namespace Engine
