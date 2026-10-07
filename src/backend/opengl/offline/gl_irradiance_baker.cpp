#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "offline/gl_irradiance_baker.h"

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "gl_error_handle.h"

#include "convention/gl_bindings.h"
#include "storage/gl_irradiance_volume.h"
#include "offline/gl_scene_capture.h"
#include "system/render/data/irradiance_volume_data.h"
#include "system/render/irradiance_dilation.h"

namespace Vkm::Engine {

namespace {
constexpr float CAPTURE_FAR = 500.0f;
} // namespace

GLIrradianceBaker::GLIrradianceBaker(GLSceneCapture& capture)
    : m_project("shaders/irradiance/project")
    , m_capture(capture) {}

GLIrradianceBaker::~GLIrradianceBaker() = default;

void GLIrradianceBaker::ensureTargets() {
    if (m_cube.isValid()) return;

    // Rendered into: RGBA16F, the half-float format GL requires to be renderable.
    m_cube.create(CAPTURE_SIZE, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, false);

    // One channel: the mask is yes/no per direction. Linear filtering is fine, since the
    // projection averages it over the sphere anyway.
    m_backface.create(CAPTURE_SIZE, 1, GL_R8, GL_RED, GL_UNSIGNED_BYTE, false);

    m_depth = std::make_unique<Vkm::GL::RenderBuffer>();
    m_depth->storage(GL_DEPTH_COMPONENT24, CAPTURE_SIZE, CAPTURE_SIZE);

    m_fbo.bind();
    m_fbo.attachRenderBuffer(GL_DEPTH_ATTACHMENT, m_depth->getID());
    m_fbo.unbind();
}

void GLIrradianceBaker::bake(
    Vkm::GL::Context& gl,
    GLIrradianceVolume& volume,
    const IrradianceVolumeData& data,
    const RenderView& view,
    GLView& glView,
    const ResourceManager& resources,
    const GLIBL& globalIBL
) {
    // Down until this bake succeeds: every early way out leaves the grid not holding this volume.
    volume.setBaked(false);

    const uint32_t rx = data.resolutionX;
    const uint32_t ry = data.resolutionY;
    const uint32_t rz = data.resolutionZ;
    if (rx == 0 || ry == 0 || rz == 0) return;

    ensureTargets();
    m_previous.resize(rx, ry, rz);
    const uint32_t cells = volume.cellCount();

    // The key light's shadow covers the volume's box: every probe sits inside it.
    const Math::AABB box{data.center - data.halfExtents, data.center + data.halfExtents};
    m_capture.begin(gl, view, glView, resources, globalIBL, static_cast<float>(CAPTURE_SIZE), box);

    // At unit intensity: the frame scales the finished grid by it, which a gather lit at it would
    // compound once per bounce.
    IrradianceVolumeData lit = data;
    lit.intensity = 1.0f;
    m_capture.setAmbientVolume(&m_previous, lit);

    // The first gather's surfaces are lit by nothing inside the box.
    ProbeGridSH grid;
    for (std::vector<glm::vec4>& coefficient : grid) coefficient.assign(cells, glm::vec4(0.0f));
    m_previous.upload(grid);

    // The first gather's verdicts, per cell: they depend on where a probe stands, not on the light.
    std::vector<bool> trusted;
    uint32_t          refused = 0;
    for (int bounce = 0; bounce < BOUNCES; ++bounce) {
        gather(gl, volume, data, bounce == 0 ? nullptr : &trusted);

        // Repair refused probes here: hardware trilinear filtering cannot skip a probe inside a
        // wall, and the next gather reads the grid as the frame will.
        volume.download(grid);
        if (bounce == 0) {
            trusted.resize(cells);
            for (uint32_t i = 0; i < cells; ++i) trusted[i] = probeTrusted(grid[0][i]);
        } else {
            for (uint32_t i = 0; i < cells; ++i) grid[0][i].w = trusted[i] ? 1.0f : 0.0f;
        }
        refused = dilateProbeGrid(grid, rx, ry, rz);
        if (refused == cells) {
            LOG_WARNING(
                "Irradiance volume at (%.1f, %.1f, %.1f): every one of its %u probes is inside "
                "geometry. Not baked - move or shrink the volume.",
                data.center.x,
                data.center.y,
                data.center.z,
                cells
            );
            return;
        }
        if (refused > 0) volume.upload(grid);
        if (bounce + 1 < BOUNCES) m_previous.upload(grid);
    }

    volume.setBaked(true);
    LOG_INFO(
        "Irradiance volume baked: %ux%ux%u probes at (%.1f, %.1f, %.1f), %d bounces; %u were inside "
        "geometry and were filled from their neighbours",
        rx,
        ry,
        rz,
        data.center.x,
        data.center.y,
        data.center.z,
        BOUNCES,
        refused
    );
}

void GLIrradianceBaker::gather(
    Vkm::GL::Context& gl,
    GLIrradianceVolume& volume,
    const IrradianceVolumeData& data,
    const std::vector<bool>* trusted
) {
    const GLSceneCapture::AttachFace attachRadiance = [&](int face) {
        m_cube.attachFace(GL_COLOR_ATTACHMENT0, face, 0);
        gl.setViewport(0, 0, CAPTURE_SIZE, CAPTURE_SIZE);
    };
    const GLSceneCapture::AttachFace attachBackface = [&](int face) {
        m_backface.attachFace(GL_COLOR_ATTACHMENT0, face, 0);
        gl.setViewport(0, 0, CAPTURE_SIZE, CAPTURE_SIZE);
    };

    const uint32_t  rx      = data.resolutionX;
    const uint32_t  ry      = data.resolutionY;
    const uint32_t  rz      = data.resolutionZ;
    const glm::vec3 boxMin  = data.center - data.halfExtents;
    const glm::vec3 boxSize = data.halfExtents * 2.0f;
    const glm::vec3 res(static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz));

    for (uint32_t z = 0; z < rz; ++z) {
        for (uint32_t y = 0; y < ry; ++y) {
            for (uint32_t x = 0; x < rx; ++x) {
                const size_t cell = x + static_cast<size_t>(y) * rx + static_cast<size_t>(z) * rx * ry;
                if (trusted && !(*trusted)[cell]) continue;

                // Probes sit at texel centres, so a (worldPos - boxMin) / boxSize lookup
                // interpolates them.
                const glm::vec3 t = (glm::vec3(x, y, z) + 0.5f) / res;
                const glm::vec3 position = boxMin + boxSize * t;

                m_fbo.bind();
                m_capture.captureCube(gl, position, CAPTURE_FAR, attachRadiance);
                if (!trusted) m_capture.captureBackfaceCube(gl, position, CAPTURE_FAR, attachBackface);
                m_fbo.unbind();

                // SH-L1 straight into this probe's cell; the mask decides whether it is trusted, on
                // the first gather.
                m_cube.bindSlot(GLBindings::IrradianceProjectSlots::PROBE);
                m_backface.bindSlot(GLBindings::IrradianceProjectSlots::BACKFACE);
                for (int i = 0; i < GLIrradianceVolume::SH_COEFFS; ++i) {
                    volume.bindImage(i, static_cast<uint32_t>(i), GL_WRITE_ONLY);
                }
                m_project.bind();
                m_project.setUniform1i("u_cellX", static_cast<int>(x));
                m_project.setUniform1i("u_cellY", static_cast<int>(y));
                m_project.setUniform1i("u_cellZ", static_cast<int>(z));
                m_project.dispatch(1, 1, 1);
            }
        }
    }

    // Each cell was written as an image, read back next and sampled after: one barrier orders
    // both. The next capture re-rendering a sampled cube is ordered by GL without one.
    VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT));
}

} // namespace Vkm::Engine
