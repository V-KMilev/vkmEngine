#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_irradiance_baker.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_context.h"

#include "convention/gl_bindings.h"
#include "data/gl_cubemap.h"
#include "data/gl_ibl.h"
#include "data/gl_irradiance_volume.h"
#include "data/gl_material.h"
#include "data/gl_mesh.h"
#include "generator/mesh_generators.h"
#include "gl_view.h"
#include "system/render/data/irradiance_volume_data.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {
constexpr float CAPTURE_NEAR = 0.05f;
constexpr float CAPTURE_FAR  = 500.0f;
} // namespace

GLIrradianceBaker::GLIrradianceBaker()
    : m_pbr("shaders/forward/pbr")
    , m_skybox("shaders/skybox")
    , m_project("shaders/irradiance/project")
    , m_unitCube(std::make_unique<GLMesh>(generateCube())) {}

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

void GLIrradianceBaker::captureProbe(Core::Context& gl, const glm::vec3& position,
                                     const std::vector<InstanceRun>& runs,
                                     const GLView& glView, const GLIBL& globalIBL) {
    const glm::mat4 proj   = glm::perspective(glm::radians(90.0f), 1.0f, CAPTURE_NEAR, CAPTURE_FAR);
    const bool      hasIBL = globalIBL.isReady();

    m_fbo.bind();

    for (int face = 0; face < 6; ++face) {
        const glm::mat4 viewM = GLCubemap::faceView(face, position);

        m_cube.attachFace(GL_COLOR_ATTACHMENT0, face, 0);
        gl.setViewport(0, 0, CAPTURE_SIZE, CAPTURE_SIZE);
        gl.setDepthTest(true);
        gl.setDepthWrite(true);
        gl.setDepthFunc(GL_LESS);
        gl.setBlending(false);
        gl.setClearColor({0.0f, 0.0f, 0.0f, 1.0f});
        gl.clear(true, true, false);

        // Camera UBO for this face: viewProjection + probe position.
        CameraData cam{};
        cam.view       = viewM;
        cam.projection = proj;
        cam.position   = position;
        cam.derive();
        m_camera.update(cam);

        // Background: the global skybox, so directions that miss geometry carry
        // sky radiance into the SH instead of black.
        if (hasIBL) {
            gl.setDepthFunc(GL_LEQUAL);
            gl.setDepthWrite(false);
            gl.setFaceCulling(false);
            m_skybox.bind();
            m_skybox.setUniformMatrix4fv("u_view", viewM);
            m_skybox.setUniformMatrix4fv("u_projection", proj);
            m_skybox.setUniform1f("u_iblIntensity", 1.0f);
            m_skybox.setUniform1i("u_hasSun", 0);  // the analytic disc would blow out the SH
            globalIBL.bindEnvCube(GLBindings::IBLTextureSlots::EnvCube);
            m_unitCube->draw();
            gl.setDepthFunc(GL_LESS);
            gl.setDepthWrite(true);
        }

        // Opaque geometry, full PBR. The skybox draw rebound the program, so
        // re-bind; the uniforms hoisted in bake() persist on the program.
        gl.setFaceCulling(true);
        gl.setCullFace(GL_BACK);
        m_pbr.bind();

        m_batcher.bindInstanceData();

        const GLMaterial* boundMaterial = nullptr;
        for (uint32_t i = 0; i < runs.size(); ++i) {
            const InstanceRun& run = runs[i];
            const GLMaterial* material = glView.getMaterial(run.material);
            if (material && material != boundMaterial) {
                material->bind(GLBindings::UBOBindingPoints::Material);
                material->bindTextures(glView);
                boundMaterial = material;
            }
            m_batcher.drawRun(run, i);
        }
    }

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

    // No-shadow lights: m_noShadow is default-built, so slotForLight() == -1 for
    // every light and the PBR shader skips shadow sampling (the camera's atlas
    // does not cover these viewpoints).
    m_lights.update(view.lights, m_noShadow);

    // Group the opaque drawables once - the same geometry feeds every probe.
    m_opaque.clear();
    for (const DrawableData& d : view.drawables) {
        const GLMaterial* material = glView.getMaterial(d.material);
        if (material && material->getType() == MaterialType::Transparent) continue;
        m_opaque.push_back(&d);
    }
    const std::vector<InstanceRun>& runs = m_batcher.buildGrouped(m_opaque, glView);

    const bool hasIBL = globalIBL.isReady();

    // Uniforms identical for every probe + face: set once on the program.
    // u_probeCount = 0 is the recursion guard - the bake never samples probes,
    // and u_useClusters = 0 because no cull pass ran for these viewpoints.
    m_pbr.bind();
    m_pbr.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    // Must be set: the shader does `ambient *= u_iblIntensity`, and an unset
    // uniform is 0 - which would bake every shadowed surface black and leave the
    // SH a flat sky average.
    m_pbr.setUniform1f("u_iblIntensity", view.environment.intensity);
    if (hasIBL) {
        globalIBL.bindIrradiance(GLBindings::IBLTextureSlots::Irradiance);
        globalIBL.bindPrefilter(GLBindings::IBLTextureSlots::Prefilter);
        globalIBL.bindBrdf(GLBindings::IBLTextureSlots::BrdfLUT);
    }
    m_pbr.setUniform1i("u_hasSSAO", 0);
    m_pbr.setUniform1i("u_hasSceneColor", 0);
    m_pbr.setUniform1i("u_probeCount", 0);
    m_pbr.setUniform1i("u_useClusters", 0);
    m_pbr.setUniform1i("u_hasIrradianceVolume", 0);  // and never samples itself
    m_pbr.setUniform2f("u_screenSize",
                       static_cast<float>(CAPTURE_SIZE), static_cast<float>(CAPTURE_SIZE));

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

                captureProbe(gl, position, runs, glView, globalIBL);

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

                // The capture rebinds the PBR program's uniforms only once, so
                // restore it for the next probe's faces.
                m_pbr.bind();
            }
        }
    }

    volume.markReady();
    LOG_INFO("Irradiance volume baked: %ux%ux%u probes at (%.1f, %.1f, %.1f)",
             rx, ry, rz, data.center.x, data.center.y, data.center.z);
}

} // namespace Engine
