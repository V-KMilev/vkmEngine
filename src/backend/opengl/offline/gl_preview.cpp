#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "offline/gl_preview.h"

#include <algorithm>
#include <cmath>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_context.h"
#include "gl_shader.h"
#include "gl_texture.h"

#include "gl_view.h"
#include "storage/gl_ibl.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "offline/gl_scene_capture.h"
#include "storage/gl_shadow_atlas.h"
#include "convention/gl_bindings.h"
#include "resource/resource_manager.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_view.h"
#include "system/render/data/camera_data.h"

namespace Vkm::Engine {

namespace {

// The shared HDR scratch target's fixed edge. Every preview scene renders at
// this size and the tonemap downsamples into the per-key LDR texture, so the
// scratch never reallocates when live previews and thumbnails alternate.
constexpr uint32_t SCENE_SIZE = 512;

// Studio rig: key / fill / rim directionals around the default orbit
// (yaw 35, pitch 20), shadowless. Directions are the light's travel direction.
// The base rig is constant; requests rotate a copy around Y (lightYawDeg).
const std::vector<LightData>& studioLights() {
    static const std::vector<LightData> s_lights = [] {
        auto directional = [](const glm::vec3& dir, const glm::vec3& color, float intensity) {
            LightData l{};
            l.type        = LightType::Directional;
            l.color       = color;
            l.intensity   = intensity;
            l.position    = glm::vec3(0.0f);
            l.direction   = glm::normalize(dir);
            l.radius      = 0.0f;
            l.castShadows = false;
            return l;
        };
        return std::vector<LightData>{
            directional({-0.35f, -0.80f, -0.50f}, {1.00f, 0.98f, 0.95f}, 2.6f),  // key: high front-right
            directional({ 0.70f, -0.20f,  0.40f}, {0.75f, 0.80f, 1.00f}, 0.8f),  // fill: cool, low left
            directional({ 0.50f, -0.25f,  0.70f}, {1.00f, 0.95f, 0.85f}, 1.2f),  // rim: from behind
        };
    }();
    return s_lights;
}

} // namespace

GLPreview::GLPreview(GLSceneCapture& capture) : m_capture(capture) {}

GLPreview::~GLPreview() = default;

void GLPreview::init() {
    m_composite = std::make_unique<Vkm::GL::Shader>("shaders/composite");
    m_tri       = std::make_unique<ScreenTriangle>();
    m_scratch.resize(SCENE_SIZE, SCENE_SIZE);
}

GLPreview::Entry& GLPreview::ensureEntry(uint64_t key, uint32_t size) {
    std::unique_ptr<Entry>& slot = m_entries[key];
    if (!slot) slot = std::make_unique<Entry>();
    Entry& e = *slot;
    if (e.size != size) {
        Vkm::GL::Texture2DParams p;
        p.width           = size;
        p.height          = size;
        p.internalFormat  = GL_RGBA8;
        p.format          = GL_RGBA;
        p.type            = GL_UNSIGNED_BYTE;
        p.minFilter       = Vkm::GL::TextureMinFilter::Linear;
        p.magFilter       = Vkm::GL::TextureMagFilter::Linear;
        p.wrapS           = Vkm::GL::TextureWrap::ClampToEdge;
        p.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
        p.generateMipmaps = false;
        e.ldr = std::make_unique<Vkm::GL::Texture2D>("preview_ldr", p);

        e.fbo.bind();
        e.fbo.attachTexture2D(GL_COLOR_ATTACHMENT0, e.ldr->getID());
        if (!e.fbo.isComplete()) {
            LOG_ERROR("Preview framebuffer incomplete (%ux%u)", size, size);
        }
        e.fbo.unbind();
        e.size = size;
    }
    return e;
}

uint32_t GLPreview::render(
    Vkm::GL::Context& gl,
    GLView& glView,
    const GLIBL& ibl,
    const GLShadowAtlas& shadows,
    const PreviewRequest& req,
    const ResourceManager& resources
) {
    if (!req.mesh || !req.material || req.size == 0) return 0;
    if (!m_composite) init();

    // Mirror the request's assets onto the GPU. The tables are shared with the
    // main frame and version-gated, so this is cheap when nothing changed.
    m_object.models    = { glm::mat4(1.0f) };
    m_object.bounds    = { Math::AABB{} };
    m_object.draws     = { ObjectDraw{ req.mesh, req.material, 0 } };
    m_object.skinFirst = { 0u };
    m_object.visible   = { 0u };

    RenderView view;
    view.viewportWidth  = SCENE_SIZE;
    view.viewportHeight = SCENE_SIZE;
    view.surfaceWidth   = SCENE_SIZE;
    view.surfaceHeight  = SCENE_SIZE;
    view.objects        = &m_object;
    view.lights         = studioLights();
    if (req.lightYawDeg != 0.0f) {
        // Rotate the rig around Y so the user can swing the key light across
        // the material without moving the camera.
        const float     a = glm::radians(req.lightYawDeg);
        const glm::mat3 rot(glm::rotate(glm::mat4(1.0f), a, glm::vec3(0.0f, 1.0f, 0.0f)));
        for (LightData& l : view.lights) l.direction = glm::normalize(rot * l.direction);
    }
    glView.sync(view, resources);

    const GLMesh*     mesh     = glView.getMesh(req.mesh);
    const GLMaterial* material = glView.getMaterial(req.material);
    if (!mesh || !material) return 0;

    // Orbit camera framed on the mesh bounds; distance is in bounding radii so
    // the same zoom value frames a pebble and a building alike.
    const MeshAsset& asset = resources.get(req.mesh);
    glm::vec3 center(0.0f);
    float radius = 1.0f;
    if (asset.boundsMin.x <= asset.boundsMax.x
        && asset.boundsMin.y <= asset.boundsMax.y
        && asset.boundsMin.z <= asset.boundsMax.z) {
        center = (asset.boundsMin + asset.boundsMax) * 0.5f;
        radius = std::max(0.001f, glm::length(asset.boundsMax - asset.boundsMin) * 0.5f);
    }

    const float yaw   = glm::radians(req.yawDeg);
    const float pitch = glm::radians(req.pitchDeg);
    const glm::vec3 orbitDir(
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::cos(yaw)
    );
    const float dist = std::max(0.05f, req.distance) * radius;
    const glm::vec3 eye = center + orbitDir * dist;

    const glm::mat4 projection = glm::perspective(
        glm::radians(40.0f),
        1.0f,
        std::max(0.001f * radius, dist - radius * 2.0f),
        dist + radius * 4.0f
    );
    const glm::mat4 lookAt = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
    m_camera.update(CameraData::from(lookAt, projection, eye), glm::vec2(static_cast<float>(SCENE_SIZE)));

    // An empty slot view answers -1 for every light, so the PBR shader reads
    // no shadow value: a preview's lights cast none.
    m_lights.update(view.lights);

    m_scratch.bind(gl);
    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GL_LESS);
    gl.setBlending(false);
    // Backdrop clear colors are linear HDR (pre-tonemap): Grey lands near
    // mid-grey after the composite pass.
    const glm::vec4 backdrop = req.background == PreviewBackground::Grey
        ? glm::vec4(0.18f, 0.18f, 0.19f, 1.0f)
        : glm::vec4(0.028f, 0.028f, 0.033f, 1.0f);
    gl.setClearColor(backdrop);
    gl.clear(true, true, false);

    const bool hasIBL = ibl.isReady();

    // Sky backdrop, drawn first so a transparent material blends over it.
    // Before the bake finishes this falls back to the clear.
    if (req.background == PreviewBackground::Sky && hasIBL) m_capture.drawSky(gl, ibl);

    gl.setFaceCulling(true);
    gl.setCullFace(GL_BACK);
    // The offline uniform set, at full indirect strength.
    bindOfflinePbrUniforms(m_capture.pbrProgram(), ibl);

    // Transparent materials blend over the backdrop. One mesh, so no sorting or
    // partitioning is needed.
    const bool transparent = material->getType() == MaterialType::Transparent;
    if (transparent) {
        gl.setBlending(true);
        gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Never read - every preview light is shadowless - but the shader declares the slot
    // as sampler2DShadow, and the driver validates a declared sampler at draw time.
    shadows.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);

    // No palette: the preview draws shaders/forward/pbr only, so a character
    // thumbnails in bind pose.
    m_objectBuffer.upload(m_object, false);
    m_objectBuffer.bind();
    const std::vector<InstanceDraw>& draws = m_batcher.buildGrouped(m_object.visible, m_object, glView, 0);
    material->bind(GLBindings::UBOBindingPoints::MATERIAL);
    material->bindTextures(glView);
    for (const InstanceDraw& draw : draws) m_batcher.draw(draw);

    if (transparent) gl.setBlending(false);

    // Tonemapped with the scene's composite shader, but pinned to Khronos
    // Neutral whatever the scene uses: it holds an authored albedo as it
    // brightens instead of washing it white, which is what a thumbnail is read for.
    Entry& entry = ensureEntry(req.key, req.size);
    entry.fbo.bind();
    entry.fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.setViewport(0, 0, static_cast<int32_t>(entry.size), static_cast<int32_t>(entry.size));
    gl.setDepthTest(false);

    m_composite->bind();
    // Never read - the preview composites in default mode - but this shader
    // declares the atlas slot as a plain sampler2D, and the driver validates a
    // declared sampler at draw time.
    shadows.bind2DRaw(GLBindings::ShadowTextureSlots::ATLAS_2D_RAW);
    m_scratch.bindTexture(GLTarget::Attachment::Color, GLBindings::CompositeTextureSlots::SCENE);
    m_composite->setUniform1f("u_bloomStrength", 0.0f);
    m_composite->setUniform1i("u_renderMode", static_cast<int>(RenderMode::Default));
    m_composite->setUniform1i("u_tonemap", static_cast<int>(Tonemap::KhronosNeutral));
    m_composite->setUniform1f("u_exposure", 1.0f);
    m_composite->setUniform1i("u_hasFog", 0);  // a preview's studio has no air in it
    m_tri->draw();

    // Whatever draws next draws into the default framebuffer. The GL state needs
    // no putting back: GLBackend::render resets it before every pass.
    Vkm::GL::FrameBuffer::bindDefault();

    return entry.ldr->getID();
}

uint32_t GLPreview::texture(uint64_t key) const {
    auto it = m_entries.find(key);
    return (it != m_entries.end() && it->second->ldr) ? it->second->ldr->getID() : 0;
}

void GLPreview::release(uint64_t key) {
    m_entries.erase(key);
}

void GLPreview::releaseAll() {
    m_entries.clear();
}

} // namespace Vkm::Engine
