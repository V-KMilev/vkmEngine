#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_preview.h"

#include <algorithm>
#include <cmath>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_context.h"
#include "gl_shader.h"
#include "texture/gl_texture.h"

#include "gl_view.h"
#include "data/gl_ibl.h"
#include "data/gl_material.h"
#include "data/gl_mesh.h"
#include "data/gl_scene_capture.h"
#include "convention/gl_bindings.h"
#include "generator/mesh_generators.h"
#include "resource/resource_manager.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_view.h"
#include "system/render/data/camera_data.h"

namespace Engine {

namespace {

// The shared HDR scratch target's fixed edge. Every preview scene renders at
// this size and the tonemap downsamples into the per-key LDR texture, so the
// scratch never reallocates when live previews and thumbnails alternate.
constexpr uint32_t SCENE_SIZE = 512;

// Studio rig: key / fill / rim directionals around the default orbit
// (yaw 35, pitch 20), shadowless. Directions are the light's travel direction.
// The base rig is constant; requests rotate a copy around Y (lightYawDeg).
const std::vector<LightData>& studioLights() {
    static const std::vector<LightData> lights = [] {
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
    return lights;
}

} // namespace

GLPreview::GLPreview()  = default;
GLPreview::~GLPreview() = default;

void GLPreview::init() {
    m_pbr       = std::make_unique<Core::Shader>("shaders/forward/pbr");
    m_composite = std::make_unique<Core::Shader>("shaders/composite");
    m_skybox    = std::make_unique<Core::Shader>("shaders/skybox");
    m_skyCube   = std::make_unique<GLMesh>(generateCube());
    m_tri       = std::make_unique<Core::ScreenTriangle>();
    m_scratch.resize(SCENE_SIZE, SCENE_SIZE);
}

GLPreview::Entry& GLPreview::ensureEntry(uint64_t key, uint32_t size) {
    std::unique_ptr<Entry>& slot = m_entries[key];
    if (!slot) slot = std::make_unique<Entry>();
    Entry& e = *slot;
    if (e.size != size) {
        Core::Texture2DParams p;
        p.width           = size;
        p.height          = size;
        p.internalFormat  = GL_RGBA8;
        p.format          = GL_RGBA;
        p.type            = GL_UNSIGNED_BYTE;
        p.minFilter       = Core::TextureMinFilter::Linear;
        p.magFilter       = Core::TextureMagFilter::Linear;
        p.wrapS           = Core::TextureWrap::ClampToEdge;
        p.wrapT           = Core::TextureWrap::ClampToEdge;
        p.generateMipmaps = false;
        e.ldr = std::make_unique<Core::Texture2D>("preview_ldr", p);

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

uint32_t GLPreview::render(Core::Context& gl, GLView& glView, const GLIBL& ibl,
                           const PreviewRequest& req, const ResourceManager& resources) {
    if (!m_pbr) return 0;  // init() not run
    if (!req.mesh || !req.material || req.size == 0) return 0;

    // Mirror the request's assets onto the GPU. The tables are shared with the
    // main frame and version-gated, so this is cheap when nothing changed.
    RenderView view;
    view.viewportWidth  = SCENE_SIZE;
    view.viewportHeight = SCENE_SIZE;
    view.surfaceHeight  = SCENE_SIZE;
    DrawableData drawable{};
    drawable.mesh         = req.mesh;
    drawable.material     = req.material;
    drawable.model        = glm::mat4(1.0f);
    drawable.normalMatrix = glm::mat3(1.0f);
    drawable.castShadows  = false;
    view.drawables.push_back(drawable);
    view.lights = studioLights();
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
        std::cos(pitch) * std::cos(yaw));
    const float dist = std::max(0.05f, req.distance) * radius;
    const glm::vec3 eye = center + orbitDir * dist;

    CameraData cam;
    cam.view       = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
    cam.projection = glm::perspective(glm::radians(40.0f), 1.0f,
                                      std::max(0.001f * radius, dist - radius * 2.0f),
                                      dist + radius * 4.0f);
    cam.position   = eye;
    cam.derive();
    m_camera.update(cam);

    // m_noShadow is never built, so slotForLight() == -1 and the PBR shader
    // skips shadow sampling entirely (same trick as the probe baker).
    m_lights.update(view.lights, m_noShadow);

    m_scratch.bind(gl);
    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GL_LESS);
    gl.setBlending(false);
    // Backdrop clear colors are linear HDR (pre-tonemap): Grey lands near
    // mid-grey after the composite pass.
    gl.setClearColor(req.background == PreviewBackground::Grey
        ? glm::vec4(0.18f, 0.18f, 0.19f, 1.0f)
        : glm::vec4(0.028f, 0.028f, 0.033f, 1.0f));
    gl.clear(true, true, false);

    const bool hasIBL = ibl.isReady();

    // Sky backdrop: the baked environment cube, drawn first (depth writes off,
    // LEQUAL so it fills the cleared far plane) so a transparent material
    // blends over it. Before the bake finishes this falls back to the clear.
    if (req.background == PreviewBackground::Sky && hasIBL && m_skybox) {
        gl.setDepthWrite(false);
        gl.setDepthFunc(GL_LEQUAL);
        gl.setFaceCulling(false);  // viewed from inside the cube
        m_skybox->bind();
        m_skybox->setUniformMatrix4fv("u_view", cam.view);
        m_skybox->setUniformMatrix4fv("u_projection", cam.projection);
        m_skybox->setUniform1f("u_iblIntensity", 1.0f);
        m_skybox->setUniform1i("u_hasSun", 0);
        ibl.bindEnvCube(GLBindings::IBLTextureSlots::EnvCube);
        m_skyCube->draw();
        gl.setDepthWrite(true);
        gl.setDepthFunc(GL_LESS);
    }

    gl.setFaceCulling(true);
    gl.setCullFace(GL_BACK);
    // The same offline uniform set the bakers use, at full indirect strength:
    // the forward pass drives that from Environment::intensity, but a preview
    // should light and reflect the environment whatever the scene asked for.
    bindOfflinePbrUniforms(*m_pbr, ibl, 1.0f, static_cast<float>(SCENE_SIZE));

    // Transparent materials blend over the backdrop. One mesh, so no sorting or
    // partitioning is needed.
    const bool transparent = material->getType() == MaterialType::Transparent;
    if (transparent) {
        gl.setBlending(true);
        gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    m_drawables.clear();
    m_drawables.push_back(&view.drawables[0]);
    const std::vector<InstanceRun>& runs = m_batcher.buildGrouped(m_drawables, glView);
    material->bind(GLBindings::UBOBindingPoints::Material);
    material->bindTextures(glView);
    m_batcher.bindInstanceData();
    for (uint32_t i = 0; i < runs.size(); ++i) m_batcher.drawRun(runs[i], i);

    if (transparent) gl.setBlending(false);

    // Tonemap with the scene's own composite shader (bloom off, default view)
    // so previews match the viewport's color response.
    Entry& entry = ensureEntry(req.key, req.size);
    entry.fbo.bind();
    entry.fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.setViewport(0, 0, static_cast<int32_t>(entry.size), static_cast<int32_t>(entry.size));
    gl.setDepthTest(false);

    m_composite->bind();
    m_scratch.bindColor(0);
    m_composite->setUniform1f("u_bloomStrength", 0.0f);
    m_composite->setUniform1i("u_renderMode", 0);
    m_tri->draw();

    // Leave the engine-default state: default framebuffer bound, depth on,
    // culling off (matches GLBackend::init's baseline).
    Core::FrameBuffer::bindDefault();
    gl.setDepthTest(true);
    gl.setDepthFunc(GL_LEQUAL);
    gl.setFaceCulling(false);

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

} // namespace Engine
