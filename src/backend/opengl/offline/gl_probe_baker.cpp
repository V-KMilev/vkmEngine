#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "offline/gl_probe_baker.h"

#include <glm/glm.hpp>

#include "logger.h"

#include "gl_context.h"

#include "convention/gl_bindings.h"
#include "offline/gl_scene_capture.h"
#include "storage/gl_probe_array.h"
#include "system/render/data/probe_data.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

/// A refresh's convolution, from the arrays' env cube into their staging layer.
class StagingTarget final : public GLCubeConvolver::Target {
    public:
        StagingTarget(Vkm::GL::Context& gl, GLProbeArray& arr) : m_gl(gl), m_arr(arr) {}
        ~StagingTarget() override = default;

        StagingTarget(const StagingTarget& other) = delete;
        StagingTarget& operator=(const StagingTarget& other) = delete;

        StagingTarget(StagingTarget && other) = delete;
        StagingTarget& operator=(StagingTarget && other) = delete;

    public:
        void makeSourceMips() override { m_arr.generateEnvMips(); }
        void bindSource() override { m_arr.bindEnvCube(GLBindings::BakeTextureSlots::SOURCE); }
        void attachIrradiance(int face) override {
            m_arr.attachIrradianceFace(m_gl, m_arr.stagingLayer(), face);
        }
        void attachPrefilter(int face, int mip) override {
            m_arr.attachPrefilterFace(m_gl, m_arr.stagingLayer(), face, mip);
        }

    private:
        Vkm::GL::Context& m_gl;
        GLProbeArray&     m_arr;
};

constexpr float CAPTURE_FAR = 1000.0f;

} // namespace

GLProbeBaker::GLProbeBaker(GLSceneCapture& capture, GLCubeConvolver& convolver)
    : m_capture(capture)
    , m_convolver(convolver) {}

GLProbeBaker::~GLProbeBaker() = default;

void GLProbeBaker::bake(
    Vkm::GL::Context& gl,
    GLProbeArray& arr,
    int layer,
    const ProbeData& probe,
    const RenderView& view,
    GLView& glView,
    const GLIBL& globalIBL,
    const GLIrradianceVolume* volume
) {
    endRefresh();
    captureFaces(gl, arr, probe, view, glView, globalIBL, volume);
    convolve(gl, arr, layer);
    LOG_INFO(
        "Reflection probe baked: layer %d at (%.1f, %.1f, %.1f)",
        layer,
        probe.position.x,
        probe.position.y,
        probe.position.z
    );
}

void GLProbeBaker::beginCapture(
    Vkm::GL::Context& gl,
    const GLProbeArray& arr,
    const ProbeData& probe,
    const RenderView& view,
    GLView& glView,
    const GLIBL& globalIBL,
    const GLIrradianceVolume* volume
) {
    // The reflection is parallax-corrected to the influence box, so the capture must get it right.
    const Math::AABB box{probe.position - probe.halfExtents, probe.position + probe.halfExtents};
    m_capture.begin(gl, view, glView, globalIBL, static_cast<float>(arr.resolution()), box);
    m_capture.setAmbientVolume(volume, view.irradianceVolume);
}

void GLProbeBaker::captureFaces(
    Vkm::GL::Context& gl,
    GLProbeArray& arr,
    const ProbeData& probe,
    const RenderView& view,
    GLView& glView,
    const GLIBL& globalIBL,
    const GLIrradianceVolume* volume
) {
    beginCapture(gl, arr, probe, view, glView, globalIBL, volume);

    arr.bindCaptureFbo();
    m_capture.captureCube(gl, probe.position, CAPTURE_FAR, [&](int face) { arr.attachEnvFace(gl, face); });

    arr.generateEnvMips();
    arr.unbindCaptureFbo();
}

void GLProbeBaker::beginRefresh(const GLProbeArray& arr) {
    // Cut once a resolution: a sun that never stops starts a refresh after every one.
    if (m_steps.empty() || m_steps.front().size != arr.prefilterSize()) {
        m_steps = GLCubeConvolver::slices(arr.prefilterSize(), GLProbeArray::PREFILTER_MIPS);
    }
    m_next = 0;
}

int GLProbeBaker::refreshSteps(int resolution) {
    const int prefilter = GLProbeArray::prefilterOf(GLProbeArray::clampResolution(resolution));
    return CAPTURE_STEPS + GLCubeConvolver::sliceCount(prefilter, GLProbeArray::PREFILTER_MIPS);
}

bool GLProbeBaker::advance(
    Vkm::GL::Context& gl,
    GLProbeArray& arr,
    int layer,
    const ProbeData& probe,
    const RenderView& view,
    GLView& glView,
    const GLIBL& globalIBL,
    const GLIrradianceVolume* volume
) {
    if (!refreshing()) return false;

    // A face of the capture a step, then the convolution's, into the staging layer.
    const int step = m_next++;
    if (step < CAPTURE_STEPS) {
        beginCapture(gl, arr, probe, view, glView, globalIBL, volume);
        arr.bindCaptureFbo();
        const GLSceneCapture::AttachFace attach = [&](int face) { arr.attachEnvFace(gl, face); };
        m_capture.captureFace(gl, step, probe.position, CAPTURE_FAR, attach);
        arr.unbindCaptureFbo();
    } else {
        StagingTarget target(gl, arr);
        gl.setDepthTest(false);
        gl.setFaceCulling(false);
        gl.setBlending(false);
        arr.bindCaptureFbo();
        m_convolver.run(gl, m_steps[static_cast<size_t>(step - CAPTURE_STEPS)], target);
        arr.unbindCaptureFbo();
    }
    if (refreshing()) return false;

    arr.copyLayer(arr.stagingLayer(), layer);
    return true;
}

void GLProbeBaker::convolve(Vkm::GL::Context& gl, GLProbeArray& arr, int layer) {
    gl.setDepthTest(false);
    gl.setFaceCulling(false);
    gl.setBlending(false);

    arr.bindCaptureFbo();

    // Convolve the env cube into this probe's array layers; the loops are GLCubeConvolver's.
    arr.bindEnvCube(GLBindings::BakeTextureSlots::SOURCE);
    m_convolver.irradiance([&](int face) { arr.attachIrradianceFace(gl, layer, face); });
    m_convolver.prefilter(
        GLProbeArray::PREFILTER_MIPS,
        [&](int face, int mip) { arr.attachPrefilterFace(gl, layer, face, mip); }
    );

    arr.unbindCaptureFbo();
}

} // namespace Vkm::Engine
