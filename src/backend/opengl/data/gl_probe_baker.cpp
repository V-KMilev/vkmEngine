#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_probe_baker.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_context.h"

#include "data/gl_probe.h"
#include "data/gl_ibl.h"
#include "data/gl_mesh.h"
#include "data/gl_material.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "generator/mesh_generators.h"
#include "system/render/render_view.h"
#include "system/render/data/camera_data.h"

namespace Engine {

namespace {
constexpr float CAPTURE_NEAR = 0.05f;
constexpr float CAPTURE_FAR  = 1000.0f;

// Standard cubemap capture basis (matches the env cube convention the forward +
// skybox already sample). dir = the +face direction, up = its up vector.
struct FaceBasis { glm::vec3 dir; glm::vec3 up; };
const FaceBasis FACES[6] = {
    {{ 1.0f,  0.0f,  0.0f}, {0.0f, -1.0f,  0.0f}},
    {{-1.0f,  0.0f,  0.0f}, {0.0f, -1.0f,  0.0f}},
    {{ 0.0f,  1.0f,  0.0f}, {0.0f,  0.0f,  1.0f}},
    {{ 0.0f, -1.0f,  0.0f}, {0.0f,  0.0f, -1.0f}},
    {{ 0.0f,  0.0f,  1.0f}, {0.0f, -1.0f,  0.0f}},
    {{ 0.0f,  0.0f, -1.0f}, {0.0f, -1.0f,  0.0f}},
};
}

GLProbeBaker::GLProbeBaker()
    : m_pbr("shaders/forward/pbr")
    , m_skybox("shaders/skybox")
    , m_irradiance("shaders/ibl/irradiance")
    , m_prefilter("shaders/ibl/prefilter")
    , m_cube(std::make_unique<GLMesh>(generateCube()))
{
}

GLProbeBaker::~GLProbeBaker() = default;

void GLProbeBaker::bake(Core::Context& gl, GLProbe& probe, const glm::vec3& position,
                        const RenderView& view, const GLView& glView, const GLIBL& globalIBL) {
    probe.createTargets();

    captureFaces(gl, probe, position, view, glView, globalIBL);
    convolve(gl, probe);

    probe.markBaked();
    LOG_INFO("Reflection probe baked at (%.1f, %.1f, %.1f)", position.x, position.y, position.z);
}

void GLProbeBaker::captureFaces(Core::Context& gl, GLProbe& probe, const glm::vec3& position,
                                const RenderView& view, const GLView& glView, const GLIBL& globalIBL) {
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, CAPTURE_NEAR, CAPTURE_FAR);

    // No-shadow lights: m_noShadow is never built, so slotForLight() == -1 for
    // every light and the PBR shader skips shadow sampling (the camera's shadow
    // atlas does not cover the probe's viewpoint).
    m_lights.update(view.lights, m_noShadow);

    // Group the opaque drawables once - the same geometry feeds all six faces.
    m_opaque.clear();
    for (const DrawableData& d : view.drawables) {
        const GLMaterial* material = glView.getMaterial(d.material);
        if (material && material->getType() == MaterialType::Transparent) continue;
        m_opaque.push_back(&d);
    }
    const std::vector<InstanceRun>& runs = m_batcher.buildGrouped(m_opaque, glView);

    const bool hasIBL = globalIBL.isReady();

    probe.bindCaptureFbo();

    for (int face = 0; face < 6; ++face) {
        const glm::mat4 viewM = glm::lookAt(position, position + FACES[face].dir, FACES[face].up);

        probe.attachEnvFace(face);
        gl.setDepthTest(true);
        gl.setDepthWrite(true);
        gl.setDepthFunc(GL_LESS);
        gl.setBlending(false);
        gl.setClearColor({0.0f, 0.0f, 0.0f, 1.0f});
        gl.clear(true, true, false);

        // Camera UBO for this face (binding 2): viewProjection + probe position.
        CameraData cam;
        cam.view       = viewM;
        cam.projection = proj;
        cam.position   = position;
        m_camera.update(cam);

        // Background: the global skybox (env cube) so off-geometry directions
        // reflect the sky, not black. Far plane, depth writes off.
        if (hasIBL) {
            gl.setDepthFunc(GL_LEQUAL);
            gl.setDepthWrite(false);
            gl.setFaceCulling(false);
            m_skybox.bind();
            m_skybox.setUniformMatrix4fv("u_view", viewM);
            m_skybox.setUniformMatrix4fv("u_projection", proj);
            m_skybox.setUniform1f("u_iblIntensity", 1.0f);
            globalIBL.bindEnvCube(GLBindings::IBLTextureSlots::EnvCube);
            m_cube->draw();
            gl.setDepthFunc(GL_LESS);
            gl.setDepthWrite(true);
        }

        // Opaque geometry, full PBR, lit by direct lights + the GLOBAL IBL.
        // u_hasProbe = 0 is the recursion guard: the bake never samples probes.
        gl.setFaceCulling(true);
        gl.setCullFace(GL_BACK);
        m_pbr.bind();
        m_pbr.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
        if (hasIBL) {
            globalIBL.bindIrradiance(GLBindings::IBLTextureSlots::Irradiance);
            globalIBL.bindPrefilter(GLBindings::IBLTextureSlots::Prefilter);
            globalIBL.bindBrdf(GLBindings::IBLTextureSlots::BrdfLUT);
        }
        m_pbr.setUniform1i("u_hasSSAO", 0);
        m_pbr.setUniform1i("u_hasSceneColor", 0);
        m_pbr.setUniform1i("u_hasProbe", 0);
        m_pbr.setUniform2f("u_screenSize",
            static_cast<float>(GLProbe::ENV_SIZE), static_cast<float>(GLProbe::ENV_SIZE));

        const GLMaterial* boundMaterial = nullptr;
        for (const InstanceRun& run : runs) {
            const GLMaterial* material = glView.getMaterial(run.material);
            if (material && material != boundMaterial) {
                material->bind(GLBindings::UBOBindingPoints::Material);
                material->bindTextures(glView);
                boundMaterial = material;
            }
            m_batcher.drawRun(run);
        }
    }

    probe.generateEnvMips();
    probe.unbindCaptureFbo();
    gl.setFaceCulling(false);
}

void GLProbeBaker::convolve(Core::Context& gl, GLProbe& probe) {
    gl.setDepthTest(false);
    gl.setFaceCulling(false);
    gl.setBlending(false);

    // Direction-only views (the convolution integrates over directions, so the
    // cube is sampled about the origin, not the probe position).
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

    probe.bindCaptureFbo();

    // Diffuse irradiance convolution.
    m_irradiance.bind();
    m_irradiance.setUniformMatrix4fv("u_projection", proj);
    probe.bindEnvCube(0);
    for (int face = 0; face < 6; ++face) {
        m_irradiance.setUniformMatrix4fv("u_view",
            glm::lookAt(glm::vec3(0.0f), FACES[face].dir, FACES[face].up));
        probe.attachIrradianceFace(face);
        m_cube->draw();
    }

    // GGX prefiltered specular, one roughness per mip.
    m_prefilter.bind();
    m_prefilter.setUniformMatrix4fv("u_projection", proj);
    probe.bindEnvCube(0);
    for (int mip = 0; mip < GLProbe::PREFILTER_MIPS; ++mip) {
        const float roughness = static_cast<float>(mip) / static_cast<float>(GLProbe::PREFILTER_MIPS - 1);
        m_prefilter.setUniform1f("u_roughness", roughness);
        for (int face = 0; face < 6; ++face) {
            m_prefilter.setUniformMatrix4fv("u_view",
                glm::lookAt(glm::vec3(0.0f), FACES[face].dir, FACES[face].up));
            probe.attachPrefilterFace(face, mip);
            m_cube->draw();
        }
    }

    probe.unbindCaptureFbo();
    gl.setDepthTest(true);
}

} // namespace Engine
