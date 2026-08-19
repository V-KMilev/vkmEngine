#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_probe_baker.h"

#include <glm/glm.hpp>

#include "logger.h"

#include "gl_context.h"

#include "data/gl_cube_convolver.h"
#include "data/gl_probe.h"
#include "data/gl_scene_capture.h"

namespace Engine {

namespace {
constexpr float CAPTURE_FAR = 1000.0f;
} // namespace

GLProbeBaker::GLProbeBaker(GLSceneCapture& capture, GLCubeConvolver& convolver)
    : m_capture(capture)
    , m_convolver(convolver) {}

GLProbeBaker::~GLProbeBaker() = default;

void GLProbeBaker::bake(Core::Context& gl, GLProbeArray& arr, int layer, const glm::vec3& position,
                        const RenderView& view, const GLView& glView, const GLIBL& globalIBL) {
    captureFaces(gl, arr, position, view, glView, globalIBL);
    convolve(gl, arr, layer);
    LOG_INFO("Reflection probe baked: layer %d at (%.1f, %.1f, %.1f)",
        layer, position.x, position.y, position.z);
}

void GLProbeBaker::captureFaces(Core::Context& gl, GLProbeArray& arr, const glm::vec3& position,
                                const RenderView& view, const GLView& glView, const GLIBL& globalIBL) {
    m_capture.begin(view, glView, globalIBL, static_cast<float>(arr.envSize()));

    arr.bindCaptureFbo();
    m_capture.captureCube(gl, position, CAPTURE_FAR,
        [&](int face) { arr.attachEnvFace(gl, face); });

    arr.generateEnvMips();
    arr.unbindCaptureFbo();
    gl.setFaceCulling(false);
}

void GLProbeBaker::convolve(Core::Context& gl, GLProbeArray& arr, int layer) {
    gl.setDepthTest(false);
    gl.setFaceCulling(false);
    gl.setBlending(false);

    arr.bindCaptureFbo();

    // Convolve the env cube into this probe's irradiance + prefilter array layers
    // (the loops are shared with the global IBL baker; only the attach differs).
    m_convolver.irradiance(
        [&] { arr.bindEnvCube(0); },
        [&](int face) { arr.attachIrradianceFace(gl, layer, face); });
    m_convolver.prefilter(GLProbeArray::PREFILTER_MIPS,
        [&] { arr.bindEnvCube(0); },
        [&](int face, int mip) { arr.attachPrefilterFace(gl, layer, face, mip); });

    arr.unbindCaptureFbo();
    gl.setDepthTest(true);
}

} // namespace Engine
