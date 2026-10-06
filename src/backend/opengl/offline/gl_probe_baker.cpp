#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "offline/gl_probe_baker.h"

#include <glm/glm.hpp>

#include "logger.h"

#include "gl_context.h"

#include "convention/gl_bindings.h"
#include "offline/gl_cube_convolver.h"
#include "storage/gl_probe_array.h"
#include "offline/gl_scene_capture.h"
#include "system/render/data/probe_data.h"

namespace Vkm::Engine {

namespace {
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
    const ResourceManager& resources,
    const GLIBL& globalIBL
) {
    captureFaces(gl, arr, probe, view, glView, resources, globalIBL);
    convolve(gl, arr, layer);
    LOG_INFO(
        "Reflection probe baked: layer %d at (%.1f, %.1f, %.1f)",
        layer,
        probe.position.x,
        probe.position.y,
        probe.position.z
    );
}

void GLProbeBaker::captureFaces(
    Vkm::GL::Context& gl,
    GLProbeArray& arr,
    const ProbeData& probe,
    const RenderView& view,
    GLView& glView,
    const ResourceManager& resources,
    const GLIBL& globalIBL
) {
    // The reflection is parallax-corrected to the influence box, so the capture must get it right.
    const Math::AABB box{probe.position - probe.halfExtents, probe.position + probe.halfExtents};
    m_capture.begin(gl, view, glView, resources, globalIBL, static_cast<float>(arr.resolution()), box);

    arr.bindCaptureFbo();
    m_capture.captureCube(gl, probe.position, CAPTURE_FAR, [&](int face) { arr.attachEnvFace(gl, face); });

    arr.generateEnvMips();
    arr.unbindCaptureFbo();
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
