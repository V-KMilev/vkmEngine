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

#include "gl_forward_programs.h"
#include "gl_view.h"
#include "storage/gl_ibl.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "offline/gl_scene_capture.h"
#include "storage/gl_shadow_atlas.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "resource/resource_manager.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/data/camera_data.h"
#include "system/ocean/ocean_data.h"
#include "core/math/axes.h"
#include "core/math/bounds.h"

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

GLPreview::Entry& GLPreview::ensureEntry(uint64_t key, uint32_t width, uint32_t height) {
    std::unique_ptr<Entry>& slot = m_entries[key];
    if (!slot) slot = std::make_unique<Entry>();
    Entry& e = *slot;
    if (e.width != width || e.height != height) {
        Vkm::GL::Texture2DParams p;
        p.width           = width;
        p.height          = height;
        p.internalFormat  = GL_RGBA8;
        p.format          = GL_RGBA;
        p.type            = GL_UNSIGNED_BYTE;
        p.minFilter       = Vkm::GL::TextureMinFilter::Linear;
        p.magFilter       = Vkm::GL::TextureMagFilter::Linear;
        p.wrapS           = Vkm::GL::TextureWrap::ClampToEdge;
        p.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
        p.generateMipmaps = false;
        e.ldr = std::make_unique<Vkm::GL::Texture2D>("preview_ldr", p);

        e.fbo.attachTexture(GL_COLOR_ATTACHMENT0, e.ldr->getID());
        if (!e.fbo.isComplete()) {
            LOG_ERROR("Preview framebuffer incomplete (%ux%u)", width, height);
        }
        e.width  = width;
        e.height = height;
    }
    return e;
}

uint32_t GLPreview::render(
    Vkm::GL::Context& gl,
    GLView& glView,
    const GLIBL& ibl,
    const PreviewScene& scene,
    const GLShadowAtlas& shadows,
    const PreviewRequest& req,
    const ResourceManager& resources
) {
    if (req.draws.empty() || req.width == 0 || req.height == 0) return 0;
    if (!m_composite) init();

    // Mirror the request's assets onto the GPU. The tables are shared with the main frame and
    // version-gated, so this is cheap when nothing changed. Opaque ones draw first, the
    // transparent blended over them and the backdrop.
    const size_t count = req.draws.size();
    m_object.models.resize(count);
    m_object.bounds.resize(count);
    m_object.draws.resize(count);
    m_object.skinFirst.assign(count, 0u);
    m_object.visible.clear();
    m_bones.clear();
    m_opaque.clear();
    m_transparent.clear();
    Math::AABB drawn = Math::AABB::empty();
    for (size_t i = 0; i < count; ++i) {
        const PreviewDraw& draw = req.draws[i];
        if (!draw.mesh || !draw.material) return 0;
        glView.ensureDrawn(draw.mesh, draw.material, resources);
        const GLMaterial* material = glView.getMaterial(draw.material);
        if (!glView.getMesh(draw.mesh) || !material) return 0;

        const MeshAsset& asset = resources.get(draw.mesh);
        Math::AABB box = Math::AABB::empty();
        box.expand(asset.boundsMin);
        box.expand(asset.boundsMax);
        const auto object = static_cast<uint32_t>(i);
        m_object.models[i] = draw.model;
        m_object.bounds[i] = box.valid() ? Math::transform(draw.model, box) : box;
        // Posed when the request brings a palette, which holds a bone for each its mesh is skinned to
        // (PreviewDraw::bones).
        const bool posed = !asset.skin.empty() && !draw.bones.empty();
        const auto bones      = posed ? static_cast<uint32_t>(draw.bones.size()) : 0u;
        m_object.draws[i]     = ObjectDraw{draw.mesh, draw.material, bones};
        m_object.skinFirst[i] = posed ? static_cast<uint32_t>(m_bones.size()) : 0u;
        if (posed) m_bones.insert(m_bones.end(), draw.bones.begin(), draw.bones.end());
        m_object.visible.push_back(object);
        if (m_object.bounds[i].valid()) drawn.expand(m_object.bounds[i]);
        (material->getType() == MaterialType::Transparent ? m_transparent : m_opaque).push_back(object);
    }

    // The scene's sun, or the studio rig where the scene has none: a preview cannot place lamps.
    std::vector<LightData> lights;
    if (scene.sun) {
        lights = {*scene.sun};
        lights.front().castShadows = false;
    } else {
        lights = studioLights();
    }
    if (req.lightYawDeg != 0.0f) {
        // Rotate the light around Y so the user can swing it across the material without
        // moving the camera.
        const float     a = glm::radians(req.lightYawDeg);
        const glm::mat3 rot(glm::rotate(glm::mat4(1.0f), a, glm::vec3(0.0f, 1.0f, 0.0f)));
        for (LightData& l : lights) l.direction = glm::normalize(rot * l.direction);
    }

    // Orbit camera framed on what is drawn; distance and pan are in bounding radii so the same
    // view frames a pebble and a building alike.
    glm::vec3 center(0.0f);
    float     radius = 1.0f;
    if (drawn.valid()) {
        center = drawn.center();
        radius = std::max(0.001f, glm::length(drawn.max - drawn.min) * 0.5f);
    }
    const float yaw   = glm::radians(req.yawDeg);
    const float pitch = glm::radians(req.pitchDeg);
    const glm::vec3 orbitDir(
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::cos(yaw)
    );
    const glm::vec3 right = glm::normalize(glm::cross(Math::WORLD_UP, orbitDir));
    const glm::vec3 up    = glm::cross(orbitDir, right);
    center += (right * req.pan.x + up * req.pan.y) * radius;
    const float dist = std::max(0.05f, req.distance) * radius;
    const glm::vec3 eye = center + orbitDir * dist;

    // A square thumbnail shares the fixed target; a viewport's size keeps its own.
    Entry&       entry  = ensureEntry(req.key, req.width, req.height);
    const bool   thumb  = req.width == req.height && req.width <= SCENE_SIZE;
    if (!thumb && !entry.scene) entry.scene = std::make_unique<GLTarget>(GLTarget::Layout::ColorDepth);
    if (thumb) entry.scene.reset();
    GLTarget&    target = thumb ? m_scratch : *entry.scene;
    const glm::uvec2 sceneSize = thumb ? glm::uvec2(SCENE_SIZE) : glm::uvec2(req.width, req.height);
    if (!thumb) target.resize(sceneSize.x, sceneSize.y);

    const float aspect = static_cast<float>(sceneSize.x) / static_cast<float>(sceneSize.y);
    const glm::mat4 projection = glm::perspective(
        glm::radians(PreviewRequest::FIELD_DEGREES),
        aspect,
        std::max(0.001f * radius, dist - radius * 2.0f),
        dist + radius * 4.0f
    );
    const glm::mat4 lookAt = glm::lookAt(eye, center, up);
    m_camera.update(CameraData::from(lookAt, projection, eye), glm::vec2(sceneSize));
    m_ocean.update(OceanData{});

    // An empty slot view answers -1 for every light, so the PBR shader reads
    // no shadow value: a preview's lights cast none.
    m_lights.update(lights);

    target.bind(gl);
    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GLDepth::EYE_TEST);
    gl.setBlending(false);
    // Backdrop clear colors are linear HDR (pre-tonemap): Grey lands near
    // mid-grey after the composite pass.
    const glm::vec4 backdrop = req.background == PreviewBackground::Grey
        ? glm::vec4(0.18f, 0.18f, 0.19f, 1.0f)
        : glm::vec4(0.028f, 0.028f, 0.033f, 1.0f);
    gl.setClearColor(backdrop);
    gl.clear(true, true, false, GLDepth::EYE_CLEAR);

    // Sky backdrop, drawn first so a transparent material blends over it.
    // Before the bake finishes this falls back to the clear.
    if (req.background == PreviewBackground::Sky && ibl.isReady()) {
        m_capture.drawSky(gl, ibl, projection, scene.skyIntensity);
    }

    // Never read - every preview light is shadowless - but the shader declares the slot
    // as sampler2DShadow, and the driver validates a declared sampler at draw time.
    shadows.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);

    // A posed mesh reads its bones from the preview's own palette; the frame binds its own again.
    const bool posed = !m_bones.empty();
    m_skin.update(posed ? &m_bones : nullptr, glView.stream());
    if (posed) m_skin.bind();
    m_objectBuffer.upload(m_object, posed, false, glView.stream());
    m_objectBuffer.bind();

    // Drawn as the frame draws them: a layered material through its own program, a double-sided
    // one from behind too. The forward pass draws with these programs too, and leaves its render
    // mode, air and coverage in them.
    GLForwardPrograms& programs = m_capture.programs();
    programs.zeroUniforms();
    gl.setCullFace(GL_BACK);
    std::vector<const Vkm::GL::Shader*> lit;
    const Vkm::GL::Shader* boundProgram  = nullptr;
    const GLMaterial*      boundMaterial = nullptr;
    const auto drawEach = [&](const std::vector<uint32_t>& objects) {
        const auto bones = static_cast<uint32_t>(m_bones.size());
        for (const InstanceDraw& draw : m_batcher.buildGrouped(objects, m_object, glView, bones)) {
            const GLMaterial* material = glView.getMaterial(draw.material);
            Vkm::GL::Shader&  program  = programs.programFor(material, draw.skinned, false);
            if (&program != boundProgram) {
                if (std::find(lit.begin(), lit.end(), &program) == lit.end()) {
                    GLForwardPrograms::bindOffline(program, ibl);
                    program.setUniform1f("u_iblIntensity", scene.skyIntensity);
                    lit.push_back(&program);
                } else {
                    program.bind();
                }
                boundProgram = &program;
            }
            if (material != boundMaterial) {
                material->bind(GLBindings::UBOBindingPoints::MATERIAL);
                material->bindTextures(glView);
                gl.setFaceCulling(!material->doubleSided());
                boundMaterial = material;
            }
            m_batcher.draw(gl, draw);
        }
    };
    drawEach(m_opaque);
    if (!m_transparent.empty()) {
        gl.setBlending(true);
        gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        drawEach(m_transparent);
        gl.setBlending(false);
    }

    // Tonemapped and exposed as the scene is, by its composite shader.
    entry.fbo.bind();
    entry.fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.setViewport(0, 0, static_cast<int32_t>(entry.width), static_cast<int32_t>(entry.height));
    gl.setDepthTest(false);

    m_composite->bind();
    // Never read - the preview composites in default mode - but this shader
    // declares the atlas slot as a plain sampler2D, and the driver validates a
    // declared sampler at draw time.
    shadows.bind2DRaw(GLBindings::ShadowTextureSlots::ATLAS_2D_RAW);
    target.bindTexture(GLTarget::Attachment::Color, GLBindings::CompositeTextureSlots::SCENE);
    m_composite->setUniform1f("u_bloomStrength", 0.0f);
    m_composite->setUniform1i("u_renderMode", static_cast<int>(RenderMode::Default));
    m_composite->setUniform1i("u_tonemap", static_cast<int>(scene.tonemap));
    // The studio rig is set for no exposure; the scene's sun for the scene's.
    m_composite->setUniform1f("u_exposure", std::exp2(scene.sun ? scene.exposure : 0.0f));
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
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) return;
    m_released.push_back(std::move(it->second));
    m_entries.erase(it);
}

void GLPreview::releaseAll() {
    for (auto& [key, entry] : m_entries) m_released.push_back(std::move(entry));
    m_entries.clear();
}

} // namespace Vkm::Engine
