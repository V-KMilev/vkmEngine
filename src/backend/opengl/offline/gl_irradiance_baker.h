#pragma once

#include <memory>

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
class ResourceManager;
struct RenderView;
struct IrradianceVolumeData;

/**
 * @brief Bakes an irradiance volume: a scene capture per grid probe, projected
 *        to SH-L1.
 *
 * Per probe: render the opaque scene (instanced, full PBR) plus the global skybox
 * into a small cube from that grid point, render a second cube saying which
 * surfaces face away from it, then dispatch a compute pass that integrates the
 * radiance against the SH basis, judges the probe by the backface mask, and
 * stores both in the volume at the probe's cell. The capture is
 * GLSceneCapture's, with the key light's shadow from a map fitted to the
 * volume's box.
 *
 * The capture faces are deliberately small: the result is a 4-coefficient
 * spherical average, so face resolution buys almost nothing while multiplying the
 * bake by the probe count.
 *
 * After the grid is filled, the probes the projection refused are replaced by a
 * blend of their trusted neighbours (dilateProbeGrid) and the repaired grid is
 * uploaded again. A volume where no probe at all could be trusted is not
 * marked ready (see GLPass::bindAmbient), so the global IBL stands in rather
 * than a grid of guesses.
 *
 * Owns the SH projection program + the capture targets, so construct it where a
 * GL context is current; the scene capture itself is borrowed (see
 * GLSceneCapture). bake() binds its own camera, lights and shadow blocks; run
 * it at the end of a frame.
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

    public:
        /**
         * @brief Bake every probe of @p data into @p volume.
         *
         * @param gl        Live GL context the captures draw through.
         * @param volume    Destination SH volume (resized to the grid by the caller).
         * @param data      The volume's world box + grid resolution.
         * @param view      Frame snapshot supplying the geometry + lights to capture.
         * @param glView    GPU resource mirror for material/mesh lookups.
         * @param resources Resolves the materials the capture syncs.
         * @param globalIBL Baked environment: lights the capture + fills the background.
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

    private:
        /**
         * @brief Allocate the capture cube + FBO on first use.
         */
        void ensureTargets();

    private:
        Vkm::GL::ComputeShader m_project;  ///< Cube -> SH-L1 coefficients + the probe's verdict.

        Vkm::GL::TextureCube                   m_cube;      ///< Small per-probe capture cube.
        Vkm::GL::TextureCube                   m_backface;  ///< Which surfaces face away, same faces.
        Vkm::GL::FrameBuffer                   m_fbo;
        std::unique_ptr<Vkm::GL::RenderBuffer> m_depth;

        GLSceneCapture& m_capture;  ///< Scene -> the six faces of m_cube.
};

} // namespace Vkm::Engine
