#include "offline/gl_scene_capture.h"

#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gl_context.h"
#include "gl_error_handle.h"

#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "offline/gl_cubemap.h"
#include "storage/gl_ibl.h"
#include "storage/gl_irradiance_volume.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "frame/gl_object_buffer.h"
#include "gl_view.h"
#include "system/render/data/camera_data.h"
#include "system/ocean/ocean_data.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

constexpr float CAPTURE_NEAR = 0.05f;

/// The values in a uniform of GL type @p type; 0 for one GLSceneCapture::resetPbr() leaves.
uint32_t floatsIn(GLenum type) {
    switch (type) {
        case GL_FLOAT:
        case GL_INT:
        case GL_BOOL:
        case GL_UNSIGNED_INT:      return 1;
        case GL_FLOAT_VEC2:
        case GL_INT_VEC2:
        case GL_BOOL_VEC2:
        case GL_UNSIGNED_INT_VEC2: return 2;
        case GL_FLOAT_VEC3:
        case GL_INT_VEC3:
        case GL_BOOL_VEC3:
        case GL_UNSIGNED_INT_VEC3: return 3;
        case GL_FLOAT_VEC4:
        case GL_INT_VEC4:
        case GL_BOOL_VEC4:
        case GL_UNSIGNED_INT_VEC4: return 4;
        case GL_FLOAT_MAT3:        return 9;
        case GL_FLOAT_MAT4:        return 16;
        default:                   return 0;  // a sampler or an image keeps the unit it is bound to
    }
}

} // namespace

GLSceneCapture::GLSceneCapture(const GLMesh& cube, const GLObjectBuffer& objects)
    : m_pbr("shaders/forward/pbr")
    , m_skybox("shaders/skybox")
    , m_surfels("shaders/irradiance/surfels")
    , m_cube(cube)
    , m_objects(objects) {}

GLSceneCapture::~GLSceneCapture() = default;

void GLSceneCapture::begin(
    Vkm::GL::Context& gl,
    const RenderView& view,
    GLView& glView,
    const ResourceManager& resources,
    const GLIBL& ibl,
    float faceSize,
    const Math::AABB& region
) {
    m_glView = &glView;
    m_ibl    = &ibl;

    gatherScene(view, glView, resources);

    // Grouped once for every face of every cube after this. No palette: the capture draws
    // shaders/forward/pbr only, so a character bakes into GI in bind pose.
    m_batcher.buildGrouped(m_opaque, *view.objects, glView, 0);

    m_shadow.draw(gl, view, glView, m_objects, m_casters, region, m_lights);

    m_faceSize = faceSize;
    m_volume   = nullptr;
    resetPbr();
    bindOfflinePbrUniforms(m_pbr, ibl);
}

void GLSceneCapture::resetPbr() {
    m_pbr.bind();
    GLint program = 0;
    VKM_GL_CHECK(glGetIntegerv(GL_CURRENT_PROGRAM, &program));
    if (static_cast<uint32_t>(program) != m_pbrListed) {
        // Listed again after a hot reload links another program.
        m_pbrListed = static_cast<uint32_t>(program);
        m_pbrUniforms.clear();
        GLint active = 0;
        VKM_GL_CHECK(glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active));
        for (GLint i = 0; i < active; ++i) {
            char   name[256];
            GLint  count = 0;
            GLenum type  = 0;
            VKM_GL_CHECK(glGetActiveUniform(program, i, sizeof(name), nullptr, &count, &type, name));
            const GLint location = glGetUniformLocation(program, name);
            if (location >= 0 && floatsIn(type) > 0) m_pbrUniforms.push_back({location, count, type});
        }
        // Zeros enough for the largest, kept, so a reset allocates nothing.
        size_t most = 0;
        for (const PlainUniform& u : m_pbrUniforms) most = std::max<size_t>(most, u.count * floatsIn(u.type));
        m_pbrZeros.assign(most, 0.0f);
        m_pbrIntZeros.assign(most, 0);
        m_pbrUintZeros.assign(most, 0u);
    }

    const GLfloat* zeros = m_pbrZeros.data();
    const GLint*   ints  = m_pbrIntZeros.data();
    const GLuint*  uints = m_pbrUintZeros.data();
    for (const PlainUniform& u : m_pbrUniforms) {
        const GLsizei n = u.count;
        switch (u.type) {
            case GL_FLOAT:             glProgramUniform1fv(program, u.location, n, zeros); break;
            case GL_FLOAT_VEC2:        glProgramUniform2fv(program, u.location, n, zeros); break;
            case GL_FLOAT_VEC3:        glProgramUniform3fv(program, u.location, n, zeros); break;
            case GL_FLOAT_VEC4:        glProgramUniform4fv(program, u.location, n, zeros); break;
            case GL_FLOAT_MAT3:
                glProgramUniformMatrix3fv(program, u.location, n, GL_FALSE, zeros);
                break;
            case GL_FLOAT_MAT4:
                glProgramUniformMatrix4fv(program, u.location, n, GL_FALSE, zeros);
                break;
            case GL_INT:
            case GL_BOOL:              glProgramUniform1iv(program, u.location, n, ints); break;
            case GL_INT_VEC2:
            case GL_BOOL_VEC2:         glProgramUniform2iv(program, u.location, n, ints); break;
            case GL_INT_VEC3:
            case GL_BOOL_VEC3:         glProgramUniform3iv(program, u.location, n, ints); break;
            case GL_INT_VEC4:
            case GL_BOOL_VEC4:         glProgramUniform4iv(program, u.location, n, ints); break;
            case GL_UNSIGNED_INT:      glProgramUniform1uiv(program, u.location, n, uints); break;
            case GL_UNSIGNED_INT_VEC2: glProgramUniform2uiv(program, u.location, n, uints); break;
            case GL_UNSIGNED_INT_VEC3: glProgramUniform3uiv(program, u.location, n, uints); break;
            case GL_UNSIGNED_INT_VEC4: glProgramUniform4uiv(program, u.location, n, uints); break;
        }
    }
}

void GLSceneCapture::beginLighting(
    Vkm::GL::Context& gl,
    const RenderView& view,
    GLView& glView,
    const ResourceManager& resources,
    const Math::AABB& region
) {
    m_glView = &glView;
    if (m_shadow.useKept(view, region, m_lights)) return;
    const bool castersOnly = true;
    gatherScene(view, glView, resources, castersOnly);
    m_shadow.draw(gl, view, glView, m_objects, m_casters, region, m_lights);
}

void GLSceneCapture::gatherScene(
    const RenderView& view,
    GLView& glView,
    const ResourceManager& resources,
    bool castersOnly
) {
    // The whole scene, not the camera's: a capture looks every way. GLView::sync leaves most
    // scene-list materials out, so they are synced here on the frames a capture runs.
    const RenderObjects& objects = *view.objects;
    const size_t casterCount = std::min<size_t>(objects.casterCount, objects.scene.size());
    m_opaque.clear();
    m_casters.clear();
    MaterialHandle lastMaterial;
    for (size_t i = 0; i < (castersOnly ? casterCount : objects.scene.size()); ++i) {
        const uint32_t        object = objects.scene[i];
        const MaterialHandle& handle = objects.draws[object].material;
        if (handle != lastMaterial) {
            lastMaterial = handle;
            glView.ensureMaterial(handle, resources);
        }
        const GLMaterial* material = glView.getMaterial(handle);
        if (!material) continue;
        // Casters lead the scene list, and the sun's map draws each as the live map does,
        // a transparent one by its alpha; the faces draw only what is opaque.
        if (i < casterCount) m_casters.push_back(object);
        if (material->getType() != MaterialType::Transparent) m_opaque.push_back(object);
    }
}

void GLSceneCapture::setAmbientVolume(const GLIrradianceVolume* volume, const IrradianceVolumeData& box) {
    m_volume    = volume;
    m_volumeBox = box;
    // A fade would let the sky onto a wall on the box's face.
    m_volumeBox.blendDistance = 0.0f;
}

void GLSceneCapture::beginFace(
    Vkm::GL::Context& gl,
    int face,
    const glm::vec3& position,
    const glm::mat4& proj,
    const AttachFace& attach
) {
    attach(face);
    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GLDepth::EYE_TEST);
    gl.setBlending(false);
    gl.setClearColor({0.0f, 0.0f, 0.0f, 1.0f});
    gl.clear(true, true, false, GLDepth::EYE_CLEAR);

    m_camera.update(
        CameraData::from(GLCubemap::faceView(face, position), proj, position),
        glm::vec2(m_faceSize)
    );
    m_ocean.update(OceanData{});
}

void GLSceneCapture::drawSky(
    Vkm::GL::Context& gl,
    const GLIBL& ibl,
    const glm::mat4& projection,
    float intensity
) {
    gl.setDepthFunc(GLDepth::EYE_TEST_EQUAL);
    gl.setDepthWrite(false);
    gl.setFaceCulling(false);
    m_skybox.bind();
    m_skybox.setUniform1i("u_hasSky", 1);
    m_skybox.setUniform1f("u_iblIntensity", intensity);
    m_skybox.setUniform1i("u_hasSun", 0);
    m_skybox.setUniform1i("u_hasFog", 0);
    // The skybox projects by its own matrix: an orthographic camera's sky takes a perspective one.
    m_skybox.setUniformMatrix4fv("u_skyProjection", projection);
    ibl.bindEnvCube(GLBindings::IBLTextureSlots::ENV_CUBE);
    m_cube.draw();
    gl.setDepthFunc(GLDepth::EYE_TEST);
    gl.setDepthWrite(true);
}

void GLSceneCapture::captureCube(
    Vkm::GL::Context& gl,
    const glm::vec3& position,
    float farPlane,
    const AttachFace& attach
) {
    for (int face = 0; face < 6; ++face) captureFace(gl, face, position, farPlane, attach);
}

void GLSceneCapture::captureFace(
    Vkm::GL::Context& gl,
    int face,
    const glm::vec3& position,
    float farPlane,
    const AttachFace& attach
) {
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, CAPTURE_NEAR, farPlane);

    // Uniforms persist on m_pbr through the skybox draw below.
    m_pbr.bind();
    if (m_volume) {
        m_volume->bindForShading(m_pbr, m_volumeBox);
    } else {
        m_pbr.setUniform1i("u_hasIrradianceVolume", 0);
    }

    beginFace(gl, face, position, proj, attach);

    // The global sky, so directions that miss geometry carry sky radiance, not black.
    if (m_ibl->isReady()) drawSky(gl, *m_ibl, proj);

    // The skybox draw rebound the program; begin()'s uniforms and IBL binds persist on m_pbr.
    gl.setFaceCulling(true);
    gl.setCullFace(GL_BACK);
    m_pbr.bind();
    m_objects.bind();

    const GLMaterial* boundMaterial = nullptr;
    for (const InstanceDraw& draw : m_batcher.draws()) {
        const GLMaterial* material = m_glView->getMaterial(draw.material);
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(*m_glView);
            boundMaterial = material;
            gl.setFaceCulling(!material->doubleSided());
        }
        m_batcher.draw(gl, draw);
    }
    gl.setFaceCulling(true);
}

void GLSceneCapture::captureSurfels(
    Vkm::GL::Context& gl,
    const glm::vec3& position,
    float farPlane,
    const AttachFace& attach
) {
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, CAPTURE_NEAR, farPlane);

    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GLDepth::EYE_TEST);
    gl.setBlending(false);
    // Off whatever the material: a back face is how a probe learns it is inside a solid.
    gl.setFaceCulling(false);
    m_surfels.bind();
    m_objects.bind();

    const std::vector<InstanceDraw>& draws = m_batcher.draws();
    for (int face = 0; face < 6; ++face) {
        attach(face);
        m_camera.update(
            CameraData::from(GLCubemap::faceView(face, position), proj, position),
            glm::vec2(m_faceSize)
        );

        const GLMaterial* boundMaterial = nullptr;
        for (const InstanceDraw& draw : draws) {
            const GLMaterial* material = m_glView->getMaterial(draw.material);
            if (material && material != boundMaterial) {
                material->bind(GLBindings::UBOBindingPoints::MATERIAL);
                material->bindTextures(*m_glView);
                m_surfels.setUniform1i("u_doubleSided", material->doubleSided() ? 1 : 0);
                boundMaterial = material;
            }
            m_batcher.draw(gl, draw);
        }
    }
    gl.setFaceCulling(true);
}

void bindOfflinePbrUniforms(Vkm::GL::Shader& pbr, const GLIBL& ibl) {
    const bool hasIBL = ibl.isReady();

    pbr.bind();
    pbr.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    pbr.setUniform1f("u_iblIntensity", 1.0f);
    if (hasIBL) {
        ibl.bindIrradiance(GLBindings::IBLTextureSlots::IRRADIANCE);
        ibl.bindPrefilter(GLBindings::IBLTextureSlots::PREFILTER);
    }
    ibl.bindBrdf(GLBindings::IBLTextureSlots::BRDF_LUT);
    pbr.setUniform1i("u_hasAO", 0);
    pbr.setUniform1i("u_hasScreenShadow", 0);
    pbr.setUniform1i("u_hasSceneColor", 0);
    pbr.setUniform1i("u_probeCount", 0);
    pbr.setUniform1i("u_useClusters", 0);
    pbr.setUniform1i("u_hasIrradianceVolume", 0);
    pbr.setUniform1i("u_hasFog", 0);
}

} // namespace Vkm::Engine
