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
#include "data/gl_cubemap.h"
#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"
#include "system/render/data/camera_data.h"

namespace Engine {

namespace {
constexpr float CAPTURE_NEAR = 0.05f;
constexpr float CAPTURE_FAR  = 1000.0f;
}

GLProbeBaker::GLProbeBaker()
    : m_pbr("shaders/forward/pbr")
    , m_skybox("shaders/skybox")
{
}

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
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, CAPTURE_NEAR, CAPTURE_FAR);

    // No-shadow lights: m_noShadow is default-built, so slotForLight() == -1 for
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

    arr.bindCaptureFbo();

    // The lit PBR uniforms + global-IBL binds are identical for all six faces, so
    // set them once. Only the camera UBO + skybox view change per face.
    // u_probeCount = 0 is the recursion guard: the bake never samples probes.
    m_pbr.bind();
    m_pbr.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    // Same trap as the irradiance bake: unset, this is 0 and the capture loses
    // its whole indirect term.
    m_pbr.setUniform1f("u_iblIntensity", view.environment.intensity);
    if (hasIBL) {
        globalIBL.bindIrradiance(GLBindings::IBLTextureSlots::Irradiance);
        globalIBL.bindPrefilter(GLBindings::IBLTextureSlots::Prefilter);
        globalIBL.bindBrdf(GLBindings::IBLTextureSlots::BrdfLUT);
    }
    m_pbr.setUniform1i("u_hasSSAO", 0);
    m_pbr.setUniform1i("u_hasContactShadow", 0);
    m_pbr.setUniform1i("u_hasSceneColor", 0);
    m_pbr.setUniform1i("u_probeCount", 0);
    m_pbr.setUniform1i("u_useClusters", 0);
    m_pbr.setUniform1i("u_hasIrradianceVolume", 0);  // never sample GI while baking
    m_pbr.setUniform2f("u_screenSize",
        static_cast<float>(arr.envSize()), static_cast<float>(arr.envSize()));

    for (int face = 0; face < 6; ++face) {
        const glm::mat4 viewM = GLCubemap::faceView(face, position);

        arr.attachEnvFace(gl, face);
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
        cam.derive();
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
            m_convolver.cube().draw();
            gl.setDepthFunc(GL_LESS);
            gl.setDepthWrite(true);
        }

        // Opaque geometry, full PBR. The skybox draw rebound the program, so
        // re-bind m_pbr; the uniforms + IBL binds hoisted above persist.
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
