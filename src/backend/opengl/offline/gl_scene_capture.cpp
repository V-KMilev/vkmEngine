#include "offline/gl_scene_capture.h"

#include <algorithm>
#include <limits>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gl_buffer_upload.h"
#include "gl_context.h"
#include "gl_error_handle.h"
#include "gl_sampler.h"
#include "gl_texture.h"
#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
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

// The sun's map over a capture region: a room's worth of it at a few
// centimetres a texel, which the 3x3 kernel softens.
constexpr uint32_t SHADOW_RES = 2048;

/// Kept clear of the region's edge on every side, in the map's own world units.
constexpr float SHADOW_MARGIN = 0.5f;

/// A sun's direction is told apart in steps of 1/this a component (shadowKey).
constexpr float SHADOW_DIRECTION_STEPS = 256.0f;

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
    , m_shadowDepth("shaders/shadow/depth")
    , m_shadowMasked("shaders/shadow/depth_masked")
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

    prepareShadow(gl, view, region);

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
    const ShadowKey key = shadowKey(view, region);
    for (int slot = 0; slot < SHADOW_SLOTS; ++slot) {
        if (m_shadows[slot].key && *m_shadows[slot].key == key) {
            useShadow(slot, view);
            return;
        }
    }
    const bool castersOnly = true;
    gatherScene(view, glView, resources, castersOnly);
    prepareShadow(gl, view, region);
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

GLSceneCapture::ShadowKey GLSceneCapture::shadowKey(const RenderView& view, const Math::AABB& region) {
    ShadowKey key;
    key.region = region;
    if (const LightData* light = lowestSlotDirectionalCaster(view.lights)) {
        key.casts      = true;
        key.light      = light->entitySlot;
        // Steps of about a fifth of a degree: a sun crossing the sky draws its map when it has moved
        // that far, not on every frame it moves.
        const glm::vec3 steps = glm::round(glm::normalize(light->direction) * SHADOW_DIRECTION_STEPS);
        key.direction  = steps / SHADOW_DIRECTION_STEPS;
        key.bias       = light->shadowBias;
        key.normalBias = light->shadowNormalBias;
    }
    return key;
}

void GLSceneCapture::useShadow(int slot, const RenderView& view) {
    ShadowSlot& shadow = m_shadows[slot];
    shadow.used = ++m_shadowClock;

    // The lights, the shadowed sun on the map's slot while it casts.
    const size_t lightCount = std::min<size_t>(view.lights.size(), Config::MAX_LIGHTS);
    m_lightSlots.assign(lightCount, -1);
    const LightData* sun = lowestSlotDirectionalCaster(view.lights);
    if (sun && shadow.casts) {
        const auto index = static_cast<size_t>(sun - view.lights.data());
        if (index < lightCount) m_lightSlots[index] = 0;
    }
    m_lights.update(view.lights, {m_lightSlots.data(), static_cast<uint32_t>(lightCount)});
    if (!shadow.casts) return;

    shadow.ubo->bindBase(GLBindings::UBOBindingPoints::SHADOW);
    shadow.map->bindSlot(GLBindings::ShadowTextureSlots::ATLAS_2D);
    m_shadowSampler->bindSlot(GLBindings::ShadowTextureSlots::ATLAS_2D);
}

void GLSceneCapture::setAmbientVolume(const GLIrradianceVolume* volume, const IrradianceVolumeData& box) {
    m_volume    = volume;
    m_volumeBox = box;
    // A fade would let the sky onto a wall on the box's face.
    m_volumeBox.blendDistance = 0.0f;
}

void GLSceneCapture::prepareShadow(Vkm::GL::Context& gl, const RenderView& view, const Math::AABB& region) {
    // Into the slot drawn for this before, or else the one bound longest ago.
    const ShadowKey shadowFor = shadowKey(view, region);
    int slot = 0;
    for (int i = 0; i < SHADOW_SLOTS; ++i) {
        if (m_shadows[i].key && *m_shadows[i].key == shadowFor) {
            slot = i;
            break;
        }
        if (m_shadows[i].used < m_shadows[slot].used) slot = i;
    }
    ShadowSlot& shadow = m_shadows[slot];

    // The sun the frame shadows (see lowestSlotDirectionalCaster).
    const LightData* key = lowestSlotDirectionalCaster(view.lights);
    shadow.key   = shadowFor;
    shadow.casts = key && !m_casters.empty();
    if (!shadow.casts) {
        useShadow(slot, view);
        return;
    }

    // Across the light, the region; along it, every caster over the region's footprint, since a
    // roof well above a volume still shades it.
    const RenderObjects& objects   = *view.objects;
    const glm::vec3      dir       = glm::normalize(key->direction);
    const glm::mat4      lightView = glm::lookAt(glm::vec3(0.0f), dir, stableUp(dir));

    const Math::AABB across = Math::transform(lightView, region);
    glm::vec3 lo = across.min - glm::vec3(SHADOW_MARGIN);
    glm::vec3 hi = across.max + glm::vec3(SHADOW_MARGIN);

    // Only those over the region's footprint draw: the map shows nothing else.
    m_overRegion.clear();
    for (const uint32_t object : m_casters) {
        const Math::AABB c = Math::transform(lightView, objects.bounds[object]);
        if (c.max.x < lo.x || c.min.x > hi.x || c.max.y < lo.y || c.min.y > hi.y) continue;
        lo.z = std::min(lo.z, c.min.z);
        hi.z = std::max(hi.z, c.max.z);
        m_overRegion.push_back(object);
    }

    // The view looks down -z, so what lies nearest the light has the largest z.
    const float     zNear   = -hi.z - SHADOW_MARGIN;
    const float     zFar    = -lo.z + SHADOW_MARGIN;
    const glm::mat4 lightVP = glm::ortho(lo.x, hi.x, lo.y, hi.y, zNear, zFar) * lightView;

    if (!shadow.map) {
        Vkm::GL::Texture2DParams params;
        params.width           = SHADOW_RES;
        params.height          = SHADOW_RES;
        params.internalFormat  = GL_DEPTH_COMPONENT24;
        params.format          = GL_DEPTH_COMPONENT;
        params.type            = GL_FLOAT;
        params.minFilter       = Vkm::GL::TextureMinFilter::Linear;
        params.magFilter       = Vkm::GL::TextureMagFilter::Linear;
        params.wrapS           = Vkm::GL::TextureWrap::ClampToEdge;
        params.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
        params.generateMipmaps = false;
        shadow.map = std::make_unique<Vkm::GL::Texture2D>("capture_shadow", params);
    }
    if (!m_shadowSampler) {
        Vkm::GL::Sampler::Params compare;
        compare.minFilter = Vkm::GL::TextureMinFilter::Linear;
        compare.magFilter = Vkm::GL::TextureMagFilter::Linear;
        compare.wrapS     = Vkm::GL::TextureWrap::ClampToEdge;
        compare.wrapT     = Vkm::GL::TextureWrap::ClampToEdge;
        compare.compare   = Vkm::GL::TextureCompare::LessEqual;
        m_shadowSampler = std::make_unique<Vkm::GL::Sampler>(compare);
    }

    // Casters through the static depth programs (bind pose, as the capture draws a character),
    // a cutout through the one its material cuts.
    m_shadowFbo.bind();
    m_shadowFbo.attachTexture2D(GL_DEPTH_ATTACHMENT, shadow.map->getID());
    m_shadowFbo.setDrawBuffer(GL_NONE);
    m_shadowFbo.setReadBuffer(GL_NONE);
    gl.setViewport(0, 0, static_cast<int32_t>(SHADOW_RES), static_cast<int32_t>(SHADOW_RES));
    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GL_LESS);
    gl.setBlending(false);
    gl.setFaceCulling(false);
    gl.clear(false, true, false);

    m_casterBatcher.buildGrouped(m_overRegion, objects, *m_glView, 0);
    m_shadowMasked.bind();
    m_shadowMasked.setUniformMatrix4fv("u_lightVP", lightVP);
    m_shadowMasked.setUniform2f("u_ditherShift", 0.0f, 0.0f);
    m_shadowDepth.bind();
    m_shadowDepth.setUniformMatrix4fv("u_lightVP", lightVP);
    m_objects.bind();
    const Vkm::GL::Shader* bound = &m_shadowDepth;
    for (const InstanceDraw& draw : m_casterBatcher.draws()) {
        const GLMaterial* material = m_glView->getMaterial(draw.material);
        const bool        masked   = material && material->cutsItsShadow();
        Vkm::GL::Shader&  program  = masked ? m_shadowMasked : m_shadowDepth;
        if (&program != bound) {
            program.bind();
            bound = &program;
        }
        if (masked) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(*m_glView);
        }
        m_casterBatcher.draw(gl, draw);
    }

    // One "cascade" covering everything: the whole map, and a split no view
    // depth reaches.
    ShadowUBOData data{};
    data.csmBase       = 0;
    data.csmCount      = 1;
    data.cascadeSplits = glm::vec4(std::numeric_limits<float>::max());
    Shadow2DGPU& map = data.s2d[0];
    map.lightVP = lightVP;
    map.atlas   = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    const float worldTexel = std::max(hi.x - lo.x, hi.y - lo.y) / static_cast<float>(SHADOW_RES);
    map.params  = glm::vec4(key->shadowBias, worldTexel, zFar - zNear, 0.0f);  // w = 0: the hard kernel
    map.shape   = glm::vec4(key->shadowNormalBias, 0.0f, 0.0f, 0.0f);
    Vkm::GL::uploadIfChanged(shadow.ubo, shadow.last, data);
    useShadow(slot, view);
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
    gl.setDepthFunc(GL_LESS);
    gl.setBlending(false);
    gl.setClearColor({0.0f, 0.0f, 0.0f, 1.0f});
    gl.clear(true, true, false);

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
    gl.setDepthFunc(GL_LEQUAL);
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
    gl.setDepthFunc(GL_LESS);
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
    gl.setDepthFunc(GL_LESS);
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
