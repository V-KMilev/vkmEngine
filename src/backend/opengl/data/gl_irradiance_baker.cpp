#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_irradiance_baker.h"

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"

#include "data/gl_irradiance_volume.h"
#include "data/gl_scene_capture.h"
#include "system/render/data/irradiance_volume_data.h"

namespace Engine {

namespace {
constexpr float CAPTURE_FAR = 500.0f;
} // namespace

GLIrradianceBaker::GLIrradianceBaker(GLSceneCapture& capture)
    : m_project("shaders/irradiance/project")
    , m_capture(capture) {}

GLIrradianceBaker::~GLIrradianceBaker() = default;

void GLIrradianceBaker::ensureTargets() {
    if (m_cube.valid()) return;

    m_cube.create(CAPTURE_SIZE, 1, GL_RGB16F, GL_RGB, GL_FLOAT, false);

    m_depth = std::make_unique<Core::RenderBuffer>();
    m_depth->storage(GL_DEPTH_COMPONENT24, CAPTURE_SIZE, CAPTURE_SIZE);

    m_fbo.bind();
    m_fbo.attachRenderBuffer(GL_DEPTH_ATTACHMENT, m_depth->getID());
    m_fbo.unbind();
}

void GLIrradianceBaker::bake(Core::Context& gl, GLIrradianceVolume& volume,
                             const IrradianceVolumeData& data,
                             const RenderView& view, const GLView& glView, const GLIBL& globalIBL) {
    const uint32_t rx = data.resolutionX;
    const uint32_t ry = data.resolutionY;
    const uint32_t rz = data.resolutionZ;
    if (rx == 0 || ry == 0 || rz == 0) return;

    ensureTargets();

    m_capture.begin(view, glView, globalIBL, static_cast<float>(CAPTURE_SIZE));

    const GLSceneCapture::AttachFace attach = [&](int face) {
        m_cube.attachFace(GL_COLOR_ATTACHMENT0, face, 0);
        gl.setViewport(0, 0, CAPTURE_SIZE, CAPTURE_SIZE);
    };

    const glm::vec3 boxMin = data.center - data.halfExtents;
    const glm::vec3 boxSize = data.halfExtents * 2.0f;
    const glm::vec3 res(static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz));

    for (uint32_t z = 0; z < rz; ++z) {
        for (uint32_t y = 0; y < ry; ++y) {
            for (uint32_t x = 0; x < rx; ++x) {
                // Probes sit at texel centres, so a lookup of
                // (worldPos - boxMin) / boxSize trilinearly interpolates them.
                const glm::vec3 t = (glm::vec3(x, y, z) + 0.5f) / res;
                const glm::vec3 position = boxMin + boxSize * t;

                m_fbo.bind();
                m_capture.captureCube(gl, position, CAPTURE_FAR, attach);
                m_fbo.unbind();

                // Project the capture to SH-L1 straight into this probe's cell.
                m_cube.bindSlot(0);
                for (int i = 0; i < GLIrradianceVolume::SH_COEFFS; ++i) {
                    volume.bindImage(i, static_cast<uint32_t>(i), GL_WRITE_ONLY);
                }
                m_project.bind();
                m_project.setUniform1i("u_cellX", static_cast<int>(x));
                m_project.setUniform1i("u_cellY", static_cast<int>(y));
                m_project.setUniform1i("u_cellZ", static_cast<int>(z));
                m_project.dispatch(1, 1, 1);

                // The next capture reuses the cube, so the projection must have
                // consumed it before we overwrite it.
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            }
        }
    }

    volume.markReady();
    LOG_INFO("Irradiance volume baked: %ux%ux%u probes at (%.1f, %.1f, %.1f)",
             rx, ry, rz, data.center.x, data.center.y, data.center.z);
}

} // namespace Engine
