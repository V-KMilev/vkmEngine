#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "offline/gl_probe_baker.h"

#include <glm/glm.hpp>

#include "vkm_log/logger.h"
#include "vkm_opengl/core/context.h"

#include "convention/gl_bindings.h"
#include "offline/capture/gl_scene_capture.h"
#include "storage/light/gl_probe_array.h"
#include "system/render/data/probe_data.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief The convolution, from the arrays' env cube into one of their layers.
 */
class LayerTarget final : public GLCubeConvolver::Target {
    public:
        LayerTarget(Vkm::OpenGL::Context& gl, GLProbeArray& arr, int layer)
            : m_gl(gl)
            , m_arr(arr)
            , m_layer(layer) {}
        ~LayerTarget() override = default;

        LayerTarget(const LayerTarget& other) = delete;
        LayerTarget& operator=(const LayerTarget& other) = delete;

        LayerTarget(LayerTarget && other) = delete;
        LayerTarget& operator=(LayerTarget && other) = delete;

    public:
        void makeSourceMips() override { m_arr.generateEnvMips(); }
        void bindSource() override { m_arr.bindEnvCube(GLBindings::BakeTextureSlots::SOURCE); }
        void attachIrradiance(int face) override { m_arr.attachIrradianceFace(m_gl, m_layer, face); }
        void attachPrefilter(int face, int mip) override {
            m_arr.attachPrefilterFace(m_gl, m_layer, face, mip);
        }

    private:
        Vkm::OpenGL::Context& m_gl;
        GLProbeArray&         m_arr;
        int                   m_layer;
};

constexpr float CAPTURE_FAR = 1000.0f;

} // namespace

GLProbeBaker::GLProbeBaker(GLSceneCapture& capture, GLCubeConvolver& convolver)
    : m_capture(capture)
    , m_convolver(convolver) {}

GLProbeBaker::~GLProbeBaker() = default;

void GLProbeBaker::bake(
    Vkm::OpenGL::Context& gl,
    GLProbeArray& arr,
    int layer,
    const ProbeData& probe,
    const RenderView& view,
    GLAssetMirror& mirror,
    const GLIBL& globalIBL,
    const GLIrradianceVolume* volume
) {
    endRefresh();
    captureFaces(gl, arr, probe, view, mirror, globalIBL, volume);
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
    Vkm::OpenGL::Context& gl,
    const GLProbeArray& arr,
    const ProbeData& probe,
    const RenderView& view,
    GLAssetMirror& mirror,
    const GLIBL& globalIBL,
    const GLIrradianceVolume* volume
) {
    // The reflection is parallax-corrected to the influence box, so the capture must get it right.
    const Math::AABB box{probe.position - probe.halfExtents, probe.position + probe.halfExtents};
    m_capture.begin(gl, view, mirror, globalIBL, static_cast<float>(arr.resolution()), box);
    m_capture.setAmbientVolume(volume, view.lists.irradianceVolume);
}

void GLProbeBaker::captureFaces(
    Vkm::OpenGL::Context& gl,
    GLProbeArray& arr,
    const ProbeData& probe,
    const RenderView& view,
    GLAssetMirror& mirror,
    const GLIBL& globalIBL,
    const GLIrradianceVolume* volume
) {
    beginCapture(gl, arr, probe, view, mirror, globalIBL, volume);

    arr.bindCaptureFbo();
    m_capture.captureCube(gl, probe.position, CAPTURE_FAR, [&](int face) { arr.attachEnvFace(gl, face); });
    arr.unbindCaptureFbo();
}

void GLProbeBaker::beginRefresh(const GLProbeArray& arr) {
    // Cut once a resolution: a sun that never stops starts a refresh after every one.
    if (m_steps.empty() || m_steps.front().size != arr.prefilterSize()) {
        m_steps = GLCubeConvolver::slices(arr.prefilterSize(), GLProbeArray::PREFILTER_MIPS);
    }
    m_next = 0;
}

bool GLProbeBaker::advance(
    Vkm::OpenGL::Context& gl,
    GLProbeArray& arr,
    int layer,
    const ProbeData& probe,
    const RenderView& view,
    GLAssetMirror& mirror,
    const GLIBL& globalIBL,
    const GLIrradianceVolume* volume
) {
    if (!refreshing()) return false;

    // A face of the capture a step, then the convolution's, into the staging layer.
    const int step = m_next++;
    if (step < CAPTURE_STEPS) {
        beginCapture(gl, arr, probe, view, mirror, globalIBL, volume);
        arr.bindCaptureFbo();
        const GLSceneCapture::AttachFace attach = [&](int face) { arr.attachEnvFace(gl, face); };
        m_capture.captureFace(gl, step, probe.position, CAPTURE_FAR, attach);
        arr.unbindCaptureFbo();
    } else {
        LayerTarget target(gl, arr, arr.stagingLayer());
        arr.bindCaptureFbo();
        m_convolver.run(gl, m_steps[static_cast<size_t>(step - CAPTURE_STEPS)], target);
        arr.unbindCaptureFbo();
    }
    if (refreshing()) return false;

    arr.copyLayer(arr.stagingLayer(), layer);
    return true;
}

void GLProbeBaker::convolve(Vkm::OpenGL::Context& gl, GLProbeArray& arr, int layer) {
    arr.bindCaptureFbo();

    LayerTarget target(gl, arr, layer);
    m_convolver.convolve(gl, arr.prefilterSize(), GLProbeArray::PREFILTER_MIPS, target);

    arr.unbindCaptureFbo();
}

} // namespace Vkm::Engine
