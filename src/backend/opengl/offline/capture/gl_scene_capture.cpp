#include "offline/capture/gl_scene_capture.h"

#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/texture/cube_faces.h"

#include "gl_forward_programs.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "storage/light/gl_ibl.h"
#include "storage/light/gl_irradiance_volume.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "frame/gl_object_buffer.h"
#include "asset/gl_asset_mirror.h"
#include "base/math/frustum.h"
#include "core/frame/camera_data.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

constexpr float CAPTURE_NEAR = 0.05f;

} // namespace

GLSceneCapture::GLSceneCapture(const GLMesh& cube, const GLObjectBuffer& objects, GLForwardPrograms& programs)
    : m_programs(programs)
    , m_skybox("shaders/opengl/scene/skybox")
    , m_surfels("shaders/opengl/offline/irradiance/surfels")
    , m_surfelsLayered("shaders/opengl/offline/irradiance/surfels", {"LAYERED"})
    , m_cube(cube)
    , m_objects(objects) {}

GLSceneCapture::~GLSceneCapture() = default;

void GLSceneCapture::begin(
    Vkm::OpenGL::Context& gl,
    const RenderView& view,
    GLAssetMirror& mirror,
    const GLIBL& ibl,
    float faceSize,
    const Math::AABB& region
) {
    m_mirror = &mirror;
    m_ibl    = &ibl;
    m_drawn  = view.objects;

    gatherScene(view, mirror);

    // A probe's faces and a volume's slices are captured over frames under one sun and region: the
    // first draws its map, the rest bind it.
    if (!m_shadow.useKept(view, region, m_lights)) {
        m_shadow.draw(gl, view, mirror, m_objects, m_casters, region, m_lights);
    }

    m_faceSize = faceSize;
    m_volume   = nullptr;
    m_programs.zeroUniforms();
    m_programs.forEach(false, false, false, [&](Vkm::OpenGL::Shader& program) {
        GLForwardPrograms::bindOffline(program, ibl);
    });
}

void GLSceneCapture::beginLighting(
    Vkm::OpenGL::Context& gl,
    const RenderView& view,
    GLAssetMirror& mirror,
    const Math::AABB& region
) {
    m_mirror = &mirror;
    if (m_shadow.useKept(view, region, m_lights)) return;
    const bool castersOnly = true;
    gatherScene(view, mirror, castersOnly);
    m_shadow.draw(gl, view, mirror, m_objects, m_casters, region, m_lights);
}

void GLSceneCapture::gatherScene(const RenderView& view, const GLAssetMirror& mirror, bool castersOnly) {
    // The whole scene, not the camera's: a capture looks every way.
    const RenderObjects& objects = *view.objects;
    const size_t casterCount     = std::min<size_t>(objects.casterCount, objects.scene.size());
    m_opaque.clear();
    m_casters.clear();
    for (size_t i = 0; i < (castersOnly ? casterCount : objects.scene.size()); ++i) {
        const uint32_t object = objects.scene[i];
        const GLMaterial* material = mirror.getMaterial(objects.draws[object].material);
        if (!material) continue;
        // Casters lead the scene list, and the sun's map draws each as the live map does,
        // a transparent one by its alpha; the faces draw only what is opaque.
        if (i < casterCount) m_casters.push_back(object);
        if (material->getType() != MaterialType::Transparent) m_opaque.push_back(object);
    }
}

void GLSceneCapture::setAmbientVolume(const GLIrradianceVolume* volume, const IrradianceVolumeData& box) {
    m_volume    = volume;
    m_volumeBox = volume ? volume->placed(box) : box;
    // A fade would let the sky onto a wall on the box's face.
    m_volumeBox.blendDistance = 0.0f;
}

void GLSceneCapture::beginFace(
    Vkm::OpenGL::Context& gl,
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
    gl.clear(true, true, GLDepth::EYE_CLEAR);

    m_camera.update(
        CameraData::from(Vkm::OpenGL::cubeFaceView(face, position), proj, position),
        glm::vec2(m_faceSize)
    );
}

void GLSceneCapture::drawSky(
    Vkm::OpenGL::Context& gl,
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
    Vkm::OpenGL::Context& gl,
    const glm::vec3& position,
    float farPlane,
    const AttachFace& attach
) {
    for (int face = 0; face < 6; ++face) captureFace(gl, face, position, farPlane, attach);
}

void GLSceneCapture::captureFace(
    Vkm::OpenGL::Context& gl,
    int face,
    const glm::vec3& position,
    float farPlane,
    const AttachFace& attach
) {
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, CAPTURE_NEAR, farPlane);

    // Uniforms persist on the programs through the skybox draw below.
    m_programs.forEach(false, false, false, [&](Vkm::OpenGL::Shader& program) {
        program.bind();
        if (m_volume) {
            m_volume->bindForShading(program, m_volumeBox);
        } else {
            program.setUniform1i("u_hasIrradianceVolume", 0);
        }
    });

    beginFace(gl, face, position, proj, attach);

    // The global sky, so directions that miss geometry carry sky radiance, not black.
    if (m_ibl->isReady()) drawSky(gl, *m_ibl, proj);

    // The skybox draw rebound the program; begin()'s uniforms and IBL binds persist on each.
    gl.setFaceCulling(true);
    gl.setCullFace(GL_BACK);
    m_objects.bind();

    // The capture draws the static programs only, so a character bakes in bind pose.
    const GLMaterial* boundMaterial = nullptr;
    const Vkm::OpenGL::Shader* boundProgram = nullptr;
    for (const InstanceDraw& draw : faceDraws(proj * Vkm::OpenGL::cubeFaceView(face, position))) {
        const GLMaterial*    material = m_mirror->getMaterial(draw.material);
        Vkm::OpenGL::Shader& program  = m_programs.programFor(material, false, false);
        if (&program != boundProgram) {
            program.bind();
            boundProgram = &program;
        }
        if (material && material != boundMaterial) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(*m_mirror);
            boundMaterial = material;
            gl.setFaceCulling(!material->doubleSided());
        }
        m_faceBatcher.draw(gl, draw);
    }
    gl.setFaceCulling(true);
}

void GLSceneCapture::captureSurfels(
    Vkm::OpenGL::Context& gl,
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
    m_objects.bind();

    for (int face = 0; face < 6; ++face) {
        attach(face);
        const glm::mat4 view = Vkm::OpenGL::cubeFaceView(face, position);
        m_camera.update(CameraData::from(view, proj, position), glm::vec2(m_faceSize));

        const GLMaterial* boundMaterial = nullptr;
        const Vkm::OpenGL::Shader* boundProgram = &m_surfels;
        m_surfels.bind();
        for (const InstanceDraw& draw : faceDraws(proj * view)) {
            const GLMaterial*    material = m_mirror->getMaterial(draw.material);
            Vkm::OpenGL::Shader& program  = material && material->layered() ? m_surfelsLayered : m_surfels;
            if (&program != boundProgram) {
                program.bind();
                boundProgram  = &program;
                boundMaterial = nullptr;
            }
            if (material && material != boundMaterial) {
                material->bind(GLBindings::UBOBindingPoints::MATERIAL);
                material->bindTextures(*m_mirror);
                program.setUniform1i("u_doubleSided", material->doubleSided() ? 1 : 0);
                boundMaterial = material;
            }
            m_faceBatcher.draw(gl, draw);
        }
    }
    gl.setFaceCulling(true);
}

const std::vector<InstanceDraw>& GLSceneCapture::faceDraws(const glm::mat4& viewProjection) {
    const Math::Frustum seen = Math::extractFrustum(viewProjection);
    m_faceOpaque.clear();
    for (const uint32_t object : m_opaque) {
        if (Math::frustumIntersectsAABB(seen, m_drawn->bounds[object])) m_faceOpaque.push_back(object);
    }
    return m_faceBatcher.buildGrouped(m_faceOpaque, *m_drawn, *m_mirror, 0);
}

} // namespace Vkm::Engine
