#include "system/render/render_system.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>   // glm::rotation (GLM_ENABLE_EXPERIMENTAL is project-wide)

#include "logger.h"

#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "system/visibility/visibility.h"
#include "system/render/environment.h"

#include "system/render/render_backend.h"
#include "system/render/render_pass.h"
#include "system/render/render_target.h"

namespace Engine {

RenderSystem::RenderSystem() : m_width(0), m_height(0) {}

RenderSystem::~RenderSystem() {
    m_backend.reset();
}

void RenderSystem::setBackend(std::unique_ptr<RenderBackend> backend) { m_backend = std::move(backend); }

void RenderSystem::resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    if (!m_backend) return;

    m_backend->resize(width, height);
    m_graph.onResize(*m_backend, width, height);
}

void RenderSystem::update(FrameContext& ctx) {
    if (!m_backend) {
        LOG_WARNING("No backend set, skipping render frame");
        return;
    }

    // Keep internal size in sync
    if (ctx.viewportWidth != m_width || ctx.viewportHeight != m_height) {
        resize(ctx.viewportWidth, ctx.viewportHeight);
    }

    // Refill the editor thumbnail bake budget for this frame (consumed during
    // the later UI stage by the Asset Browser).
    m_thumbBudget = THUMB_BUDGET_PER_FRAME;

    // Environment lives as a singleton component on a scene entity (editable
    // in the Inspector). Pull it each frame; mirror into m_environment so
    // getEnvironment() returns a stable reference between frames.
    m_environment = sceneEnvironment(ctx.scene);
    m_renderView.environment = m_environment;
    m_renderView.deltaTime   = ctx.deltaTime;

    // Build snapshot for this frame (reuses vector capacity from previous frame)
    m_renderView.build(ctx.scene, ctx.resources, *ctx.visibility, ctx.viewportWidth, ctx.viewportHeight);

    // Wireframe is a diagnostic view: the forward pass routes every batch
    // through the unlit shader, AABB/Grid are unlit at the shader level,
    // and SSAO would sample filled-triangle gbuffer AO at line pixels
    // (lying about occlusion). Force env.ao.enabled off so the unlit path skips
    // the AO sample cleanly. Pass-level decisions (which post passes run,
    // composite bypass) live on each pass via enabledForView() and on
    // composite via env.wireframe directly.
    if (m_environment.wireframe) {
        m_renderView.environment.ao.enabled = false;
    }

    // Backend-owned GPU sync (no-op for backends that don't need it).
    m_backend->syncResources(m_renderView, ctx.resources);

    // Execute passes
    m_graph.execute(*m_backend, m_renderView, ctx.resources);
}

void RenderSystem::addPass(std::unique_ptr<RenderPass> pass) {
    m_graph.addPass(std::move(pass));
}

void RenderSystem::clearPasses() {
    m_graph.clear();
}

void RenderSystem::invalidateTemporalHistory() {
    m_graph.invalidateTemporalHistory();
}

size_t RenderSystem::passCount() const {
    return m_graph.passCount();
}

std::string_view RenderSystem::passName(size_t index) const {
    return m_graph.getPass(index).getName();
}

bool RenderSystem::isPassEnabled(size_t index) const {
    return m_graph.getPass(index).isEnabled();
}

void RenderSystem::setPassEnabled(size_t index, bool enabled) {
    m_graph.getPass(index).setEnabled(enabled);
}

uint32_t RenderSystem::renderMaterialPreview(
    ResourceManager& resources,
    const MaterialHandle& material,
    const MeshHandle& mesh,
    float yawDeg, float pitchDeg, float distance,
    uint32_t size
) {
    if (!m_backend || size == 0 || !material || !mesh) return 0;

    // Studio camera orbiting the origin.
    const float yaw   = glm::radians(yawDeg);
    const float pitch = glm::radians(pitchDeg);
    const glm::vec3 eye = distance * glm::vec3(
        glm::cos(pitch) * glm::sin(yaw),
        glm::sin(pitch),
        glm::cos(pitch) * glm::cos(yaw));

    RenderView& v = m_previewView;
    v.drawables.clear();
    v.lights.clear();

    v.camera.view           = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    v.camera.projection     = glm::perspective(glm::radians(40.0f), 1.0f, 0.05f, 100.0f);
    v.camera.viewProjection = v.camera.projection * v.camera.view;
    v.camera.position       = eye;
    v.camera.exposure       = 1.0f;

    // The scene's environment (IBL bake / SSR / bloom all match the viewport),
    // but with view-dependent / temporal effects forced off so a static
    // material ball reads as a clean, stable studio shot.
    v.environment                       = m_environment;
    v.environment.exposure.autoExposure = false;   // deterministic studio exposure
    v.environment.taa.enabled           = false;
    v.environment.dof.enabled           = false;
    v.environment.motionBlur.enabled    = false;

    v.viewportWidth  = size;
    v.viewportHeight = size;
    v.deltaTime      = 0.0f;

    DrawableData d;
    d.mesh         = mesh;
    d.material     = material;
    d.materialType = resources.get(material).type;
    d.castShadows  = false;
    d.model        = glm::mat4(1.0f);
    v.drawables.emplace_back(d);

    // Single directional key light; IBL fills the rest.
    LightData key{};
    key.type           = LightType::Directional;
    key.color          = glm::vec3(1.0f);
    key.intensity      = 2.5f;
    key.radius         = 0.0f;
    key.innerConeAngle = 0.0f;
    key.outerConeAngle = 0.0f;
    key.castShadows    = false;
    key.shadowBias     = 0.0f;
    key.shadowExtent   = 0.0f;
    key.shadowSlot     = -1;
    key.position       = glm::vec3(0.0f);
    key.rotation       = glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f),
                             glm::normalize(glm::vec3(-0.5f, -0.7f, -0.6f)));
    v.lights.emplace_back(key);

    // World-grid and AABB-debug make no sense around a single preview shape;
    // suppress just those two, leave the rest of the pipeline intact.
    const size_t passes = m_graph.passCount();
    for (size_t i = 0; i < passes; ++i) {
        RenderPass& p = m_graph.getPass(i);
        const std::string& n = p.getName();
        if (n == "GLGridPass" || n == "GLAABBDebugPass") {
            m_previewPassWasEnabled.push_back({ i, p.isEnabled() });
            p.setEnabled(false);
        }
    }

    m_graph.beginPreview(*m_backend, size);
    // Preview's hand-built RenderView can slip past sync's version-gating
    // heuristics; force the mesh/material/texture resident first.
    m_backend->ensurePreviewResourceTables(v, resources);
    m_backend->syncResources(v, resources);
    m_graph.execute(*m_backend, v, resources);
    m_graph.endPreview();
    // The composite pass left the preview FBO bound. Rebind the window
    // backbuffer so ImGui (which draws into the active FBO) doesn't end
    // up rendering the editor into the preview texture.
    m_backend->getDefaultTarget().bind();

    for (const auto& s : m_previewPassWasEnabled) {
        m_graph.getPass(s.first).setEnabled(s.second);
    }
    m_previewPassWasEnabled.clear();

    return m_graph.previewColorTexture();
}

uint32_t RenderSystem::materialPreviewTexture(
    ResourceManager& resources,
    const MaterialHandle& material,
    const MeshHandle& mesh,
    float yawDeg, float pitchDeg, float distance,
    uint64_t key, uint64_t version, bool live
) {
    if (!m_backend) return 0;

    if (!live) {
        const uint32_t cached = m_graph.cachedPreview(*m_backend, key);
        auto it = m_thumbVersion.find(key);
        const bool fresh = cached && it != m_thumbVersion.end() && it->second == version;
        if (fresh) return cached;            // up to date - no work
        if (m_thumbBudget == 0) return cached;  // wait our turn (0 = placeholder)
        --m_thumbBudget;
    }

    // One fixed offscreen resolution for every preview so switching between
    // the 512 Material Editor and small grid thumbnails never reallocates
    // the preview targets mid-frame.
    const uint32_t liveTex =
        renderMaterialPreview(resources, material, mesh,
                              yawDeg, pitchDeg, distance, PREVIEW_RES);
    if (!liveTex) return live ? 0u : m_graph.cachedPreview(*m_backend, key);

    const uint32_t snap = m_graph.snapshotPreviewToCache(*m_backend, key, PREVIEW_RES);
    if (!live) m_thumbVersion[key] = version;
    return snap ? snap : liveTex;
}


} // namespace Engine
