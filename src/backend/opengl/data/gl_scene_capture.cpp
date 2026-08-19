#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_scene_capture.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include "gl_context.h"

#include "convention/gl_bindings.h"
#include "data/gl_cubemap.h"
#include "data/gl_ibl.h"
#include "data/gl_material.h"
#include "data/gl_mesh.h"
#include "generator/mesh_generators.h"
#include "gl_view.h"
#include "system/render/data/camera_data.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {
constexpr float CAPTURE_NEAR = 0.05f;
} // namespace

GLSceneCapture::GLSceneCapture()
    : m_pbr("shaders/forward/pbr")
    , m_skybox("shaders/skybox")
    , m_cube(std::make_unique<GLMesh>(generateCube())) {}

GLSceneCapture::~GLSceneCapture() = default;

void GLSceneCapture::begin(const RenderView& view, const GLView& glView, const GLIBL& ibl,
                           float faceSize) {
    m_glView = &glView;
    m_ibl    = &ibl;

    // No-shadow lights: m_noShadow is default-built, so slotForLight() == -1 for
    // every light and the PBR shader skips shadow sampling (the camera's shadow
    // atlas does not cover these viewpoints).
    m_lights.update(view.lights, m_noShadow);

    // Group the opaque drawables once - the same geometry feeds every face of
    // every cube captured after this.
    m_opaque.clear();
    for (const DrawableData& d : view.drawables) {
        const GLMaterial* material = glView.getMaterial(d.material);
        if (material && material->getType() == MaterialType::Transparent) continue;
        m_opaque.push_back(&d);
    }
    m_batcher.buildGrouped(m_opaque, glView);

    bindOfflinePbrUniforms(m_pbr, ibl, view.environment.sky.intensity, faceSize);
}

void GLSceneCapture::captureCube(Vkm::GL::Context& gl, const glm::vec3& position, float farPlane,
                                 const AttachFace& attach) {
    const glm::mat4 proj   = glm::perspective(glm::radians(90.0f), 1.0f, CAPTURE_NEAR, farPlane);
    const bool      hasIBL = m_ibl->isReady();

    const std::vector<InstanceRun>& runs = m_batcher.runs();

    for (int face = 0; face < 6; ++face) {
        const glm::mat4 viewM = GLCubemap::faceView(face, position);

        attach(face);
        gl.setDepthTest(true);
        gl.setDepthWrite(true);
        gl.setDepthFunc(GL_LESS);
        gl.setBlending(false);
        gl.setClearColor({0.0f, 0.0f, 0.0f, 1.0f});
        gl.clear(true, true, false);

        CameraData cam;
        cam.view       = viewM;
        cam.projection = proj;
        cam.position   = position;
        cam.derive();
        m_camera.update(cam);

        // Background: the global skybox (env cube) so directions that miss
        // geometry carry sky radiance instead of black. Far plane, depth writes
        // off, and no analytic sun disc - it would blow out the capture.
        if (hasIBL) {
            gl.setDepthFunc(GL_LEQUAL);
            gl.setDepthWrite(false);
            gl.setFaceCulling(false);
            m_skybox.bind();
            m_skybox.setUniformMatrix4fv("u_view", viewM);
            m_skybox.setUniformMatrix4fv("u_projection", proj);
            m_skybox.setUniform1f("u_iblIntensity", 1.0f);
            m_skybox.setUniform1i("u_hasSun", 0);
            m_ibl->bindEnvCube(GLBindings::IBLTextureSlots::EnvCube);
            m_cube->draw();
            gl.setDepthFunc(GL_LESS);
            gl.setDepthWrite(true);
        }

        // Opaque geometry, full PBR. The skybox draw rebound the program, so
        // re-bind m_pbr; the uniforms + IBL binds begin() set persist on it.
        gl.setFaceCulling(true);
        gl.setCullFace(GL_BACK);
        m_pbr.bind();

        m_batcher.bindInstanceData();

        const GLMaterial* boundMaterial = nullptr;
        for (uint32_t i = 0; i < runs.size(); ++i) {
            const InstanceRun& run = runs[i];
            const GLMaterial* material = m_glView->getMaterial(run.material);
            if (material && material != boundMaterial) {
                material->bind(GLBindings::UBOBindingPoints::Material);
                material->bindTextures(*m_glView);
                boundMaterial = material;
            }
            m_batcher.drawRun(run, i);
        }
    }
}

void bindOfflinePbrUniforms(Vkm::GL::Shader& pbr, const GLIBL& ibl, float iblIntensity, float faceSize) {
    const bool hasIBL = ibl.isReady();

    pbr.bind();
    pbr.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    pbr.setUniform1f("u_iblIntensity", iblIntensity);
    if (hasIBL) {
        ibl.bindIrradiance(GLBindings::IBLTextureSlots::Irradiance);
        ibl.bindPrefilter(GLBindings::IBLTextureSlots::Prefilter);
        ibl.bindBrdf(GLBindings::IBLTextureSlots::BrdfLUT);
    }
    pbr.setUniform1i("u_hasSSAO", 0);
    pbr.setUniform1i("u_hasSceneColor", 0);
    pbr.setUniform1i("u_probeCount", 0);
    pbr.setUniform1i("u_useClusters", 0);
    pbr.setUniform1i("u_hasIrradianceVolume", 0);
    pbr.setUniform2f("u_screenSize", faceSize, faceSize);
}

} // namespace Vkm::Engine
