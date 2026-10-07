#include "offline/gl_scene_capture.h"

#include <algorithm>
#include <limits>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gl_buffer_upload.h"
#include "gl_context.h"
#include "gl_sampler.h"
#include "gl_texture.h"
#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "offline/gl_cubemap.h"
#include "storage/gl_ibl.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "frame/gl_object_buffer.h"
#include "gl_view.h"
#include "system/render/data/camera_data.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

constexpr float CAPTURE_NEAR = 0.05f;

// The key light's map over a capture region: a room's worth of it at a few
// centimetres a texel, which the 3x3 kernel softens.
constexpr uint32_t SHADOW_RES = 2048;

/// Kept clear of the region's edge on every side, in the map's own world units.
constexpr float SHADOW_MARGIN = 0.5f;

} // namespace

GLSceneCapture::GLSceneCapture(const GLMesh& cube, const GLObjectBuffer& objects)
    : m_pbr("shaders/forward/pbr")
    , m_skybox("shaders/skybox")
    , m_backface("shaders/irradiance/backface")
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

    // The whole scene, not the camera's: a capture looks every way. GLView::sync leaves most
    // scene-list materials out, so they are synced here on the frames a capture runs.
    const RenderObjects& objects = *view.objects;
    const size_t casterCount = std::min<size_t>(objects.casterCount, objects.scene.size());
    m_opaque.clear();
    m_casters.clear();
    MaterialHandle lastMaterial;
    for (size_t i = 0; i < objects.scene.size(); ++i) {
        const uint32_t        object = objects.scene[i];
        const MaterialHandle& handle = objects.draws[object].material;
        if (handle != lastMaterial) {
            lastMaterial = handle;
            glView.ensureMaterial(handle, resources);
        }
        const GLMaterial* material = glView.getMaterial(handle);
        if (!material || material->getType() == MaterialType::Transparent) continue;
        m_opaque.push_back(object);
        // Casters lead the scene list; their opaque ones are what the key light's map draws.
        if (i < casterCount) m_casters.push_back(object);
    }

    // Grouped once for every face of every cube after this. No palette: the capture draws
    // shaders/forward/pbr only, so a character bakes into GI in bind pose.
    m_batcher.buildGrouped(m_opaque, objects, glView, 0);

    prepareShadow(gl, view, region);

    m_faceSize = faceSize;
    bindOfflinePbrUniforms(m_pbr, ibl);
}

void GLSceneCapture::prepareShadow(Vkm::GL::Context& gl, const RenderView& view, const Math::AABB& region) {
    const size_t lightCount = std::min<size_t>(view.lights.size(), Config::MAX_LIGHTS);
    m_lightSlots.assign(lightCount, -1);

    // The key light (see lowestSlotDirectional).
    const LightData* key = lowestSlotDirectional(view.lights);
    const bool casts = key && key->castShadows && !m_casters.empty();
    if (casts) {
        const auto index = static_cast<size_t>(key - view.lights.data());
        if (index < lightCount) m_lightSlots[index] = 0;
    }
    m_lights.update(view.lights, {m_lightSlots.data(), static_cast<uint32_t>(lightCount)});
    if (!casts) return;

    // Across the light, the region; along it, every caster over the region's footprint, since a
    // roof well above a volume still shades it.
    const RenderObjects& objects   = *view.objects;
    const glm::vec3      dir       = glm::normalize(key->direction);
    const glm::mat4      lightView = glm::lookAt(glm::vec3(0.0f), dir, stableUp(dir));

    const Math::AABB across = Math::transform(lightView, region);
    glm::vec3 lo = across.min - glm::vec3(SHADOW_MARGIN);
    glm::vec3 hi = across.max + glm::vec3(SHADOW_MARGIN);

    for (const uint32_t object : m_casters) {
        const Math::AABB c = Math::transform(lightView, objects.bounds[object]);
        if (c.max.x < lo.x || c.min.x > hi.x || c.max.y < lo.y || c.min.y > hi.y) continue;
        lo.z = std::min(lo.z, c.min.z);
        hi.z = std::max(hi.z, c.max.z);
    }

    // The view looks down -z, so what lies nearest the light has the largest z.
    const float     zNear   = -hi.z - SHADOW_MARGIN;
    const float     zFar    = -lo.z + SHADOW_MARGIN;
    const glm::mat4 lightVP = glm::ortho(lo.x, hi.x, lo.y, hi.y, zNear, zFar) * lightView;

    if (!m_shadowMap) {
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
        m_shadowMap = std::make_unique<Vkm::GL::Texture2D>("capture_shadow", params);

        Vkm::GL::Sampler::Params compare;
        compare.minFilter = Vkm::GL::TextureMinFilter::Linear;
        compare.magFilter = Vkm::GL::TextureMagFilter::Linear;
        compare.wrapS     = Vkm::GL::TextureWrap::ClampToEdge;
        compare.wrapT     = Vkm::GL::TextureWrap::ClampToEdge;
        compare.compare   = Vkm::GL::TextureCompare::LessEqual;
        m_shadowSampler = std::make_unique<Vkm::GL::Sampler>(compare);

        m_shadowFbo.bind();
        m_shadowFbo.attachTexture2D(GL_DEPTH_ATTACHMENT, m_shadowMap->getID());
        m_shadowFbo.setDrawBuffer(GL_NONE);
        m_shadowFbo.setReadBuffer(GL_NONE);
    }

    // Casters through the static depth programs (bind pose, as the capture draws a character),
    // a cutout through the one its material cuts.
    m_shadowFbo.bind();
    gl.setViewport(0, 0, static_cast<int32_t>(SHADOW_RES), static_cast<int32_t>(SHADOW_RES));
    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GL_LESS);
    gl.setBlending(false);
    gl.setFaceCulling(false);
    gl.clear(false, true, false);

    m_casterBatcher.buildGrouped(m_casters, objects, *m_glView, 0);
    m_shadowMasked.bind();
    m_shadowMasked.setUniformMatrix4fv("u_lightVP", lightVP);
    m_shadowDepth.bind();
    m_shadowDepth.setUniformMatrix4fv("u_lightVP", lightVP);
    m_objects.bind();
    const Vkm::GL::Shader* bound = &m_shadowDepth;
    for (const InstanceDraw& draw : m_casterBatcher.draws()) {
        const GLMaterial* material = m_glView->getMaterial(draw.material);
        const bool        masked   = material && material->getType() == MaterialType::AlphaMask;
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
    Vkm::GL::uploadIfChanged(m_shadowUbo, m_shadowLast, data);
    m_shadowUbo->bindBase(GLBindings::UBOBindingPoints::SHADOW);

    m_shadowMap->bindSlot(GLBindings::ShadowTextureSlots::ATLAS_2D);
    m_shadowSampler->bindSlot(GLBindings::ShadowTextureSlots::ATLAS_2D);
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
}

void GLSceneCapture::drawSky(Vkm::GL::Context& gl, const GLIBL& ibl) {
    gl.setDepthFunc(GL_LEQUAL);
    gl.setDepthWrite(false);
    gl.setFaceCulling(false);
    m_skybox.bind();
    m_skybox.setUniform1i("u_hasSky", 1);
    m_skybox.setUniform1f("u_iblIntensity", 1.0f);
    m_skybox.setUniform1i("u_hasSun", 0);
    m_skybox.setUniform1i("u_hasFog", 0);
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
    const glm::mat4 proj   = glm::perspective(glm::radians(90.0f), 1.0f, CAPTURE_NEAR, farPlane);
    const bool      hasIBL = m_ibl->isReady();

    const std::vector<InstanceDraw>& draws = m_batcher.draws();

    for (int face = 0; face < 6; ++face) {
        beginFace(gl, face, position, proj, attach);

        // The global sky, so directions that miss geometry carry sky radiance, not black.
        if (hasIBL) drawSky(gl, *m_ibl);

        // The skybox draw rebound the program; begin()'s uniforms and IBL binds persist on m_pbr.
        gl.setFaceCulling(true);
        gl.setCullFace(GL_BACK);
        m_pbr.bind();
        m_objects.bind();

        const GLMaterial* boundMaterial = nullptr;
        for (const InstanceDraw& draw : draws) {
            const GLMaterial* material = m_glView->getMaterial(draw.material);
            if (material && material != boundMaterial) {
                material->bind(GLBindings::UBOBindingPoints::MATERIAL);
                material->bindTextures(*m_glView);
                boundMaterial = material;
                gl.setFaceCulling(!material->doubleSided());
            }
            m_batcher.draw(gl, draw);
        }
    }
    gl.setFaceCulling(true);
}

void GLSceneCapture::captureBackfaceCube(
    Vkm::GL::Context& gl,
    const glm::vec3& position,
    float farPlane,
    const AttachFace& attach
) {
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, CAPTURE_NEAR, farPlane);

    const std::vector<InstanceDraw>& draws = m_batcher.draws();

    for (int face = 0; face < 6; ++face) {
        beginFace(gl, face, position, proj, attach);

        // Culling off is the whole point: a surface enclosing the probe reaches
        // this pass only as the back face captureCube() throws away.
        gl.setFaceCulling(false);
        m_backface.bind();
        m_objects.bind();

        // The answer is a property of the winding, except on a double-sided material, whose
        // back is a surface too. An alpha-masked material counts as solid here - a probe
        // behind a leaf card reads as enclosed.
        const GLMaterial* boundMaterial = nullptr;
        for (const InstanceDraw& draw : draws) {
            const GLMaterial* material = m_glView->getMaterial(draw.material);
            if (material && material != boundMaterial) {
                m_backface.setUniform1i("u_doubleSided", material->doubleSided() ? 1 : 0);
                boundMaterial = material;
            }
            m_batcher.draw(gl, draw);
        }
    }
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
    pbr.setUniform1i("u_hasSceneColor", 0);
    pbr.setUniform1i("u_probeCount", 0);
    pbr.setUniform1i("u_useClusters", 0);
    pbr.setUniform1i("u_hasIrradianceVolume", 0);
    pbr.setUniform1i("u_hasFog", 0);
}

} // namespace Vkm::Engine
