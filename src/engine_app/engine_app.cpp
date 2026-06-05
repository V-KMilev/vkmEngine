#define VKM_LOG_CATEGORY "ENGINE_APP"

#include "engine_app.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "logger.h"

#include "core/engine.h"
#include "resource/asset_database.h"
#include "system/animation/animation_system.h"
#include "system/async/async_loader_system.h"
#include "system/event/event_system.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/physics/physics_system.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/render/render_pass.h"
#include "system/render/render_pass_factory.h"
#include "system/render/environment.h"
#include "system/camera/camera_controller.h"
#include "system/io/file_watcher.h"
#include "io/project_paths.h"

#include "core/gl_backend.h"
#include "config/gl_config.h"
#include "pass/gl_pass_registration.h"

#include "loader/shader_loaders.h"

#include "default_scene.h"

namespace Engine {

EngineAppSystems setupEngineApp(Engine& engine) {
    // AssetDatabase must be bound before any loader runs - loaders stamp
    // each asset's GUID into it on import. First-run creates an empty
    // file; subsequent runs reuse the recorded GUIDs.
    AssetDatabase::get().initFromDisk(
        (ProjectPaths::assets() / "_database.json").string());

    auto& cameraController = engine.addSystem<CameraController>(SystemStage::Input);
    auto& eventSystem      = engine.addSystem<EventSystem>     (SystemStage::Simulation);
    // AsyncLoaderSystem drains the ThreadPool's completed-load queue
    // before the per-frame visibility + render sweep, so freshly decoded
    // textures reach the GPU upload step in the same frame they finished.
    engine.addSystem<AsyncLoaderSystem>(SystemStage::Simulation);
    engine.addSystem<AnimationSystem> (SystemStage::Simulation);
    // Physics runs after animation and before the Transform stage, so the
    // hierarchy resolves physics-updated Transforms into WorldTransform the
    // same frame. fixedUpdate() does the work; update() is a no-op.
    engine.addSystem<PhysicsSystem>   (SystemStage::Simulation);
    engine.addSystem<HierarchySystem> (SystemStage::Transform);
    auto& visibilitySystem = engine.addSystem<VisibilitySystem>(SystemStage::Visibility);
    auto& renderSystem     = engine.addSystem<RenderSystem>    (SystemStage::Render);

    // Shaders are first-class assets. Sampler->slot bindings live on the
    // asset and get re-applied automatically every time the backend
    // (re)compiles them - which includes hot reload.
    const std::string shaderDir = ProjectPaths::shaders().string();
    auto& resources = engine.getResources();

    // Load + hot-reload-watch every shader from one list. load() records each
    // handle so the watch loop at the end cannot drift out of sync with the
    // loads - adding a shader is one load() call, watched automatically. (The
    // previous code kept a parallel hand-maintained watchShader() list; a
    // forgotten line there silently dropped a shader's hot reload.)
    std::vector<ShaderHandle> shaderHandles;
    auto load = [&](const std::string& subPath, const std::string& name,
                    const std::unordered_map<std::string, int>& samplers = {},
                    bool variantAware = false) {
        shaderHandles.push_back(
            loadShader(resources, shaderDir + subPath, name, samplers, variantAware));
    };

    const std::unordered_map<std::string, int> pbrSamplers = {
        {GLConfig::UniformNames::AlbedoTexture,              GLConfig::TextureSlots::Albedo},
        {GLConfig::UniformNames::NormalTexture,              GLConfig::TextureSlots::Normal},
        {GLConfig::UniformNames::MetallicRoughnessTexture,   GLConfig::TextureSlots::MetallicRoughness},
        {GLConfig::UniformNames::AOMetallicRoughnessTexture, GLConfig::TextureSlots::MetallicRoughness},
        {GLConfig::UniformNames::AOTexture,                  GLConfig::TextureSlots::AO},
        {GLConfig::UniformNames::EmissionTexture,            GLConfig::TextureSlots::Emission},
        {GLConfig::UniformNames::HeightTexture,              GLConfig::TextureSlots::Height},
        // Clearcoat/transmission samplers return with their material phase.
        {GLConfig::UniformNames::MetallicTexture,            GLConfig::TextureSlots::Metallic},
        {GLConfig::UniformNames::RoughnessTexture,           GLConfig::TextureSlots::Roughness},
        {GLConfig::UniformNames::ShadowMap2D,                GLConfig::TextureSlots::ShadowMap2D},
        {GLConfig::UniformNames::ShadowMapCube,              GLConfig::TextureSlots::ShadowMapCube},
        // Raw-depth shadow bindings - same textures as above, bound with
        // a compare-off sampler object for PCSS's blocker-search pass.
        {GLConfig::UniformNames::ShadowMap2DDepth,           GLConfig::TextureSlots::ShadowMap2DDepth},
        {GLConfig::UniformNames::ShadowMapCubeDepth,         GLConfig::TextureSlots::ShadowMapCubeDepth},
        {GLConfig::UniformNames::IrradianceMap,              GLConfig::TextureSlots::IrradianceMap},
        {GLConfig::UniformNames::PrefilterMap,               GLConfig::TextureSlots::PrefilterMap},
        {GLConfig::UniformNames::BrdfLUT,                    GLConfig::TextureSlots::BrdfLUT},
        {GLConfig::UniformNames::EnvCube,                    GLConfig::TextureSlots::EnvCube},
        // Fallback IBL set - the global skybox bake the PBR shader blends
        // against the active reflection probe at probe-influence edges.
        {GLConfig::UniformNames::IrradianceMap2,             GLConfig::TextureSlots::IrradianceMap2},
        {GLConfig::UniformNames::PrefilterMap2,              GLConfig::TextureSlots::PrefilterMap2},
        {GLConfig::UniformNames::EnvCube2,                   GLConfig::TextureSlots::EnvCube2},
        {GLConfig::UniformNames::SssLUT,                     GLConfig::TextureSlots::SssLUT},
        {GLConfig::UniformNames::SSAO,                       GLConfig::TextureSlots::SSAO},
        {GLConfig::UniformNames::SceneColor,                 GLConfig::TextureSlots::SceneColor},
    };
    const std::unordered_map<std::string, int> unlitSamplers = {
        {GLConfig::UniformNames::AlbedoTexture,   GLConfig::TextureSlots::Albedo},
        {GLConfig::UniformNames::EmissionTexture, GLConfig::TextureSlots::Emission},
    };
    // pbr is variantAware: per-material feature flags drive an
    // #ifdef HAS_X variant cache. unlit and every other shader share
    // a single compiled program.
    load("/pbr",        "shader:pbr",        pbrSamplers, /*variantAware=*/true);
    load("/unlit",      "shader:unlit",      unlitSamplers);
    load("/aabb_debug", "shader:aabb_debug");
    load("/outline",    "shader:outline");
    load("/grid",       "shader:grid");
    load("/shadow",     "shader:shadow");

    // Composite/AgX post pass: u_hdr samples the resolved HDR scene target.
    const std::unordered_map<std::string, int> compositeSamplers = {
        {"u_hdr", 0},
        {"u_bloom", 1},
        {"u_adaptedLum", 2},
        {"u_colorLut", 3},
        {"u_dirt", 4},
        {"u_overlay", 5},
    };
    load("/post/composite", "shader:composite", compositeSamplers);

    // IBL bake + skybox. Bake sampler 0 = source (equirect / env cube).
    const std::unordered_map<std::string, int> equirectSamplers = { {"u_equirect", 0} };
    const std::unordered_map<std::string, int> envCubeSamplers   = { {"u_envCube", 0} };
    load("/ibl/equirect",   "shader:ibl_equirect",   equirectSamplers);
    load("/ibl/irradiance", "shader:ibl_irradiance", envCubeSamplers);
    load("/ibl/prefilter",  "shader:ibl_prefilter",  envCubeSamplers);
    load("/ibl/brdf",       "shader:ibl_brdf");
    load("/skybox",         "shader:skybox",         envCubeSamplers);

    // Bloom (COD/Jimenez): both passes sample one source mip at slot 0.
    const std::unordered_map<std::string, int> bloomSamplers = { {"u_src", 0} };
    load("/post/bloom_down", "shader:bloom_down", bloomSamplers);
    load("/post/bloom_up",   "shader:bloom_up",   bloomSamplers);

    // Auto-exposure: lum shader reads the scene; adapt reads lum + history.
    const std::unordered_map<std::string, int> lumSamplers   = { {"u_hdr", 0} };
    const std::unordered_map<std::string, int> adaptSamplers = { {"u_lumTex", 0}, {"u_prevAdapt", 1} };
    load("/post/lum",      "shader:lum",      lumSamplers);
    load("/post/exposure", "shader:exposure", adaptSamplers);

    // Depth/normal prepass + GTAO. GTAO reads the view-space MRT.
    const std::unordered_map<std::string, int> gtaoSamplers = { {"u_normalTex", 0}, {"u_posTex", 1} };
    load("/prepass",   "shader:prepass");
    load("/post/gtao", "shader:gtao", gtaoSamplers);

    // SSR reuses the prepass G-buffer + resolved HDR (slots 0/1/2).
    const std::unordered_map<std::string, int> ssrSamplers = {
        {"u_sceneColor", 0}, {"u_viewNormal", 1}, {"u_viewPos", 2}
    };
    load("/post/ssr", "shader:ssr", ssrSamplers);

    // TAA reprojects history (slots: current/history/viewPos = 0/1/2).
    const std::unordered_map<std::string, int> taaSamplers = {
        {"u_current", 0}, {"u_history", 1}, {"u_viewPos", 2}
    };
    load("/post/taa", "shader:taa", taaSamplers);

    // DoF + motion blur read scene/viewPos (slots 0/1) into the scratch.
    const std::unordered_map<std::string, int> postGeomSamplers = {
        {"u_scene", 0}, {"u_viewPos", 1}
    };
    load("/post/dof",         "shader:dof",         postGeomSamplers);
    load("/post/motion_blur", "shader:motion_blur", postGeomSamplers);

    // Lens flare reads the resolved HDR scene at slot 0; the procedural
    // starburst mask is bound to slot 1 by the pass.
    const std::unordered_map<std::string, int> lensFlareSamplers = {
        {"u_hdr", 0}, {"u_starburst", 1}
    };
    load("/post/lens_flare", "shader:lens_flare", lensFlareSamplers);

    // OIT resolve: composites Weighted-Blended OIT (accum, revealage)
    // into the HDR scene. Only active when env.transparency.useOIT is on.
    const std::unordered_map<std::string, int> oitResolveSamplers = {
        {"u_oitAccum", 0}, {"u_oitRevealage", 1}
    };
    load("/post/oit_resolve", "shader:oit_resolve", oitResolveSamplers);

    // Hi-Z pyramid: init samples the view-space position MRT; reduce
    // samples the pyramid itself one mip below. No consumer yet (#20).
    const std::unordered_map<std::string, int> hizInitSamplers   = { {"u_viewPos", 0} };
    const std::unordered_map<std::string, int> hizReduceSamplers = { {"u_src",     0} };
    load("/post/hiz_init",   "shader:hiz_init",   hizInitSamplers);
    load("/post/hiz_reduce", "shader:hiz_reduce", hizReduceSamplers);

    // Render passes - shadow runs first so the forward pass can sample its result.
    renderSystem.setBackend(std::make_unique<GLBackend>());

    // Register every builtin GL pass with the engine-side factory once;
    // the pipeline below is then a simple name list. This decouples pass
    // instantiation from main.cpp so adding a pass means editing the
    // backend's registration file, not this list - and unblocks future
    // data-driven pipeline configs without further refactor.
    registerBuiltinGLPasses();

    // Pipeline order: IBL bake first (no-op unless env map changed),
    // shadow before forward, prepass+GTAO before forward (AO sampling),
    // opaque -> sky -> transparent so transmissive glass refracts the
    // composited opaque+sky image, then debug overlays + post chain.
    const std::vector<std::string> defaultPipeline = {
        "GLIBLBakePass",
        "GLShadowPass",
        "GLPrepass",
        "GLHiZPass",
        "GLGTAOPass",
        "GLForwardPass.Opaque",
        "GLSkyboxPass",
        "GLForwardPass.Transparent",
        "GLOITResolvePass",
        "GLAABBDebugPass",
        "GLOutlinePass",
        "GLGridPass",
        "GLSSRPass",
        "GLLensFlarePass",
        "GLTAAPass",
        "GLDofPass",
        "GLMotionBlurPass",
        "GLBloomPass",
        "GLExposurePass",
        "GLCompositePass",
    };
    auto& factory = RenderPassFactory::get();
    for (const auto& name : defaultPipeline) {
        if (auto pass = factory.create(name, resources)) {
            renderSystem.addPass(std::move(pass));
        } else {
            LOG_ERROR("Pipeline: failed to instantiate pass '%s'", name.c_str());
        }
    }

    // Hot reload: file change -> asset version bump -> backend resyncs. Every
    // shader load() above recorded its handle, so this watches all of them
    // with no hand-maintained parallel list to fall out of sync.
    auto& fileWatcher = engine.addSystem<FileWatcher>(SystemStage::Input);
    for (const ShaderHandle handle : shaderHandles) {
        watchShader(fileWatcher, resources, handle);
    }

    // Default scene: a single cube at the origin under a directional
    // light. Scene/asset round-trip happy: every asset has a source
    // descriptor so save -> cold-start load reproduces this exactly.
    auto cameraEntity = generateDefaultScene(engine);
    cameraController.setCameraEntity(cameraEntity);

    // Environment is a singleton scene entity, selectable in the editor
    // (Hierarchy "Environment" row -> Inspector). Create it now and seed
    // the default IBL map.
    sceneEnvironment(engine.getScene()).ibl.path =
        (ProjectPaths::envs() / "environment.hdr").string();

    return EngineAppSystems{ cameraController, eventSystem, visibilitySystem, renderSystem };
}

} // namespace Engine
