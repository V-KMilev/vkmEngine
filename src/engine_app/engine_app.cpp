#define VKM_LOG_CATEGORY "ENGINE_APP"

#include "engine_app.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "logger.h"

#include "core/engine.h"
#include "resource/asset_database.h"
#include "system/animation/animation_system.h"
#include "system/event/event_system.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/render/render_pass.h"
#include "system/render/render_pass_factory.h"
#include "system/render/environment.h"
#include "system/camera/camera_controller.h"
#include "system/io/file_watcher.h"

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
        std::string(APP_ROOT_DIR) + "/assets/_database.json");

    auto& cameraController = engine.addSystem<CameraController>(SystemStage::Input);
    auto& eventSystem      = engine.addSystem<EventSystem>     (SystemStage::Simulation);
    engine.addSystem<AnimationSystem> (SystemStage::Simulation);
    engine.addSystem<HierarchySystem> (SystemStage::Transform);
    auto& visibilitySystem = engine.addSystem<VisibilitySystem>(SystemStage::Visibility);
    auto& renderSystem     = engine.addSystem<RenderSystem>    (SystemStage::Render);

    // Shaders are first-class assets. Sampler->slot bindings live on the
    // asset and get re-applied automatically every time the backend
    // (re)compiles them - which includes hot reload.
    const std::string shaderDir = std::string(APP_ROOT_DIR) + "/shaders";
    auto& resources = engine.getResources();

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
    const auto pbrShader     = loadShader(resources, shaderDir + "/pbr",        "shader:pbr",        pbrSamplers, /*variantAware=*/true);
    const auto unlitShader   = loadShader(resources, shaderDir + "/unlit",      "shader:unlit",      unlitSamplers);
    const auto aabbShader    = loadShader(resources, shaderDir + "/aabb_debug", "shader:aabb_debug");
    const auto outlineShader = loadShader(resources, shaderDir + "/outline",    "shader:outline");
    const auto gridShader    = loadShader(resources, shaderDir + "/grid",       "shader:grid");
    const auto shadowShader  = loadShader(resources, shaderDir + "/shadow",     "shader:shadow");

    // Composite/AgX post pass: u_hdr samples the resolved HDR scene target.
    const std::unordered_map<std::string, int> compositeSamplers = {
        {"u_hdr", 0},
        {"u_bloom", 1},
        {"u_adaptedLum", 2},
        {"u_colorLut", 3},
        {"u_dirt", 4},
        {"u_overlay", 5},
    };
    const auto compositeShader = loadShader(resources, shaderDir + "/post/composite", "shader:composite", compositeSamplers);

    // IBL bake + skybox. Bake sampler 0 = source (equirect / env cube).
    const std::unordered_map<std::string, int> equirectSamplers = { {"u_equirect", 0} };
    const std::unordered_map<std::string, int> envCubeSamplers   = { {"u_envCube", 0} };
    const auto equirectShader   = loadShader(resources, shaderDir + "/ibl/equirect",   "shader:ibl_equirect",   equirectSamplers);
    const auto irradianceShader = loadShader(resources, shaderDir + "/ibl/irradiance", "shader:ibl_irradiance", envCubeSamplers);
    const auto prefilterShader  = loadShader(resources, shaderDir + "/ibl/prefilter",  "shader:ibl_prefilter",  envCubeSamplers);
    const auto brdfShader       = loadShader(resources, shaderDir + "/ibl/brdf",       "shader:ibl_brdf");
    const auto skyboxShader     = loadShader(resources, shaderDir + "/skybox",         "shader:skybox",         envCubeSamplers);

    // Bloom (COD/Jimenez): both passes sample one source mip at slot 0.
    const std::unordered_map<std::string, int> bloomSamplers = { {"u_src", 0} };
    const auto bloomDownShader = loadShader(resources, shaderDir + "/post/bloom_down", "shader:bloom_down", bloomSamplers);
    const auto bloomUpShader   = loadShader(resources, shaderDir + "/post/bloom_up",   "shader:bloom_up",   bloomSamplers);

    // Auto-exposure: lum shader reads the scene; adapt reads lum + history.
    const std::unordered_map<std::string, int> lumSamplers   = { {"u_hdr", 0} };
    const std::unordered_map<std::string, int> adaptSamplers = { {"u_lumTex", 0}, {"u_prevAdapt", 1} };
    const auto lumShader      = loadShader(resources, shaderDir + "/post/lum",      "shader:lum",      lumSamplers);
    const auto exposureShader = loadShader(resources, shaderDir + "/post/exposure", "shader:exposure", adaptSamplers);

    // Depth/normal prepass + GTAO. GTAO reads the view-space MRT.
    const std::unordered_map<std::string, int> gtaoSamplers = { {"u_normalTex", 0}, {"u_posTex", 1} };
    const auto prepassShader = loadShader(resources, shaderDir + "/prepass",   "shader:prepass");
    const auto gtaoShader    = loadShader(resources, shaderDir + "/post/gtao", "shader:gtao", gtaoSamplers);

    // SSR reuses the prepass G-buffer + resolved HDR (slots 0/1/2).
    const std::unordered_map<std::string, int> ssrSamplers = {
        {"u_sceneColor", 0}, {"u_viewNormal", 1}, {"u_viewPos", 2}
    };
    const auto ssrShader = loadShader(resources, shaderDir + "/post/ssr", "shader:ssr", ssrSamplers);

    // TAA reprojects history (slots: current/history/viewPos = 0/1/2).
    const std::unordered_map<std::string, int> taaSamplers = {
        {"u_current", 0}, {"u_history", 1}, {"u_viewPos", 2}
    };
    const auto taaShader = loadShader(resources, shaderDir + "/post/taa", "shader:taa", taaSamplers);

    // DoF + motion blur read scene/viewPos (slots 0/1) into the scratch.
    const std::unordered_map<std::string, int> postGeomSamplers = {
        {"u_scene", 0}, {"u_viewPos", 1}
    };
    const auto dofShader = loadShader(resources, shaderDir + "/post/dof",         "shader:dof",         postGeomSamplers);
    const auto mbShader  = loadShader(resources, shaderDir + "/post/motion_blur", "shader:motion_blur", postGeomSamplers);

    // Lens flare reads the resolved HDR scene at slot 0; the procedural
    // starburst mask is bound to slot 1 by the pass.
    const std::unordered_map<std::string, int> lensFlareSamplers = {
        {"u_hdr", 0}, {"u_starburst", 1}
    };
    const auto lensFlareShader = loadShader(resources, shaderDir + "/post/lens_flare", "shader:lens_flare", lensFlareSamplers);

    // OIT resolve: composites Weighted-Blended OIT (accum, revealage)
    // into the HDR scene. Only active when env.transparency.useOIT is on.
    const std::unordered_map<std::string, int> oitResolveSamplers = {
        {"u_oitAccum", 0}, {"u_oitRevealage", 1}
    };
    const auto oitResolveShader = loadShader(
        resources, shaderDir + "/post/oit_resolve",
        "shader:oit_resolve", oitResolveSamplers);

    // Hi-Z pyramid: init samples the view-space position MRT; reduce
    // samples the pyramid itself one mip below. No consumer yet (#20).
    const std::unordered_map<std::string, int> hizInitSamplers   = { {"u_viewPos", 0} };
    const std::unordered_map<std::string, int> hizReduceSamplers = { {"u_src",     0} };
    const auto hizInitShader   = loadShader(
        resources, shaderDir + "/post/hiz_init",
        "shader:hiz_init", hizInitSamplers);
    const auto hizReduceShader = loadShader(
        resources, shaderDir + "/post/hiz_reduce",
        "shader:hiz_reduce", hizReduceSamplers);

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

    // Hot reload: file change -> asset version bump -> backend resyncs.
    auto& fileWatcher = engine.addSystem<FileWatcher>(SystemStage::Input);
    watchShader(fileWatcher, resources, pbrShader);
    watchShader(fileWatcher, resources, unlitShader);
    watchShader(fileWatcher, resources, aabbShader);
    watchShader(fileWatcher, resources, outlineShader);
    watchShader(fileWatcher, resources, gridShader);
    watchShader(fileWatcher, resources, shadowShader);
    watchShader(fileWatcher, resources, compositeShader);
    watchShader(fileWatcher, resources, equirectShader);
    watchShader(fileWatcher, resources, irradianceShader);
    watchShader(fileWatcher, resources, prefilterShader);
    watchShader(fileWatcher, resources, brdfShader);
    watchShader(fileWatcher, resources, skyboxShader);
    watchShader(fileWatcher, resources, bloomDownShader);
    watchShader(fileWatcher, resources, bloomUpShader);
    watchShader(fileWatcher, resources, lumShader);
    watchShader(fileWatcher, resources, exposureShader);
    watchShader(fileWatcher, resources, prepassShader);
    watchShader(fileWatcher, resources, gtaoShader);
    watchShader(fileWatcher, resources, ssrShader);
    watchShader(fileWatcher, resources, taaShader);
    watchShader(fileWatcher, resources, dofShader);
    watchShader(fileWatcher, resources, mbShader);
    watchShader(fileWatcher, resources, lensFlareShader);
    watchShader(fileWatcher, resources, oitResolveShader);
    watchShader(fileWatcher, resources, hizInitShader);
    watchShader(fileWatcher, resources, hizReduceShader);

    // Default scene: a single cube at the origin under a directional
    // light. Scene/asset round-trip happy: every asset has a source
    // descriptor so save -> cold-start load reproduces this exactly.
    auto cameraEntity = generateDefaultScene(engine);
    cameraController.setCameraEntity(cameraEntity);

    // Environment is a singleton scene entity, selectable in the editor
    // (Hierarchy "Environment" row -> Inspector). Create it now and seed
    // the default IBL map.
    sceneEnvironment(engine.getScene()).ibl.path =
        std::string(APP_ROOT_DIR) + "/assets/envs/environment.hdr";

    return EngineAppSystems{ cameraController, eventSystem, visibilitySystem, renderSystem };
}

} // namespace Engine
