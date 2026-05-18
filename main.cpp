#include <string>

#include "logger.h"
#include "debug/build_info.h"

#include "gl_debug.h"
#include "gl_context.h"

// Engine
#include "core/engine.h"
#include "system/animation/animation_system.h"
#include "system/event/event_system.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/visibility/visibility_system.h"
#include "system/render/render_system.h"
#include "system/camera/camera_controller.h"
#include "system/io/file_watcher.h"
#include "editor_system.h"

// Backend
#include "core/gl_backend.h"
#include "config/gl_config.h"
#include "pass/gl_forward_pass.h"
#include "pass/gl_aabb_debug_pass.h"
#include "pass/gl_grid_pass.h"
#include "pass/gl_shadow_pass.h"
#include "pass/gl_composite_pass.h"
#include "pass/gl_ibl_bake_pass.h"
#include "pass/gl_skybox_pass.h"
#include "pass/gl_bloom_pass.h"
#include "pass/gl_exposure_pass.h"
#include "pass/gl_prepass.h"
#include "pass/gl_gtao_pass.h"
#include "pass/gl_ssr_pass.h"
#include "pass/gl_taa_pass.h"
#include "pass/gl_dof_pass.h"
#include "pass/gl_motion_blur_pass.h"

// Tools
#include "asset_registration.h"
#include "loader/shader_loaders.h"

// Default scene (cube at origin, sun, camera)
#include "examples/default_scene.h"

int main() {
    try {
        const std::string rootDir = APP_ROOT_DIR;
        const std::string logFile = rootDir + "/logs/log.log";

        if (!Logger::init(logFile, "ENGINE", LogLevel::TRACE)) {
            return -1;
        }

        Engine::printBuildInfo();
        Core::enableGLDebugLogging(true);

        // Register generators/loaders with the engine's asset factory registry
        // so SceneSerializer can recreate procedural meshes + folder materials
        // on cold-start scene loads.
        Engine::registerBuiltinAssetFactories();

        auto& engine = Engine::Engine::get();
        auto& window = engine.getWindow();

        window.createWindow("VKM Engine");
        window.setFramerate(0);

        // Systems - registered at the stage that matches their role.
        auto& cameraController = engine.addSystem<Engine::CameraController>(Engine::SystemStage::Input);
        auto& eventSystem      = engine.addSystem<Engine::EventSystem>     (Engine::SystemStage::Simulation);
        auto& animationSystem  = engine.addSystem<Engine::AnimationSystem> (Engine::SystemStage::Simulation);
        auto& hierarchySystem  = engine.addSystem<Engine::HierarchySystem> (Engine::SystemStage::Transform);
        auto& visibilitySystem = engine.addSystem<Engine::VisibilitySystem>(Engine::SystemStage::Visibility);
        auto& renderSystem     = engine.addSystem<Engine::RenderSystem>    (Engine::SystemStage::Render);
        engine.addSystem<Engine::EditorSystem>(Engine::SystemStage::UI,
            window.getWindowContext(), &cameraController, &visibilitySystem, &renderSystem, &eventSystem);

        // Shaders are first-class assets. Sampler→slot bindings live on the
        // asset and get re-applied automatically every time the backend
        // (re)compiles them — which includes hot reload.
        const std::string shaderDir = std::string(APP_ROOT_DIR) + "/shaders";
        auto& resources = engine.getResources();

        const std::unordered_map<std::string, int> pbrSamplers = {
            {Engine::GLConfig::UniformNames::AlbedoTexture,              Engine::GLConfig::TextureSlots::Albedo},
            {Engine::GLConfig::UniformNames::NormalTexture,              Engine::GLConfig::TextureSlots::Normal},
            {Engine::GLConfig::UniformNames::MetallicRoughnessTexture,   Engine::GLConfig::TextureSlots::MetallicRoughness},
            {Engine::GLConfig::UniformNames::AOMetallicRoughnessTexture, Engine::GLConfig::TextureSlots::MetallicRoughness},
            {Engine::GLConfig::UniformNames::AOTexture,                  Engine::GLConfig::TextureSlots::AO},
            {Engine::GLConfig::UniformNames::EmissionTexture,            Engine::GLConfig::TextureSlots::Emission},
            {Engine::GLConfig::UniformNames::HeightTexture,              Engine::GLConfig::TextureSlots::Height},
            // Clearcoat/transmission samplers return with their material phase.
            {Engine::GLConfig::UniformNames::MetallicTexture,            Engine::GLConfig::TextureSlots::Metallic},
            {Engine::GLConfig::UniformNames::RoughnessTexture,           Engine::GLConfig::TextureSlots::Roughness},
            {Engine::GLConfig::UniformNames::ShadowMap2D,                Engine::GLConfig::TextureSlots::ShadowMap2D},
            {Engine::GLConfig::UniformNames::ShadowMapCube,              Engine::GLConfig::TextureSlots::ShadowMapCube},
            {Engine::GLConfig::UniformNames::IrradianceMap,              Engine::GLConfig::TextureSlots::IrradianceMap},
            {Engine::GLConfig::UniformNames::PrefilterMap,               Engine::GLConfig::TextureSlots::PrefilterMap},
            {Engine::GLConfig::UniformNames::BrdfLUT,                    Engine::GLConfig::TextureSlots::BrdfLUT},
            {Engine::GLConfig::UniformNames::SSAO,                       Engine::GLConfig::TextureSlots::SSAO},
            {Engine::GLConfig::UniformNames::EnvCube,                    Engine::GLConfig::TextureSlots::EnvCube},
        };
        const std::unordered_map<std::string, int> unlitSamplers = {
            {Engine::GLConfig::UniformNames::AlbedoTexture,   Engine::GLConfig::TextureSlots::Albedo},
            {Engine::GLConfig::UniformNames::EmissionTexture, Engine::GLConfig::TextureSlots::Emission},
        };
        const auto pbrShader    = Engine::loadShader(resources, shaderDir + "/pbr",        "shader:pbr",        pbrSamplers);
        const auto unlitShader  = Engine::loadShader(resources, shaderDir + "/unlit",      "shader:unlit",      unlitSamplers);
        const auto aabbShader   = Engine::loadShader(resources, shaderDir + "/aabb_debug", "shader:aabb_debug");
        const auto gridShader   = Engine::loadShader(resources, shaderDir + "/grid",       "shader:grid");
        const auto shadowShader = Engine::loadShader(resources, shaderDir + "/shadow",     "shader:shadow");

        // Composite/AgX post pass: u_hdr samples the resolved HDR scene target.
        const std::unordered_map<std::string, int> compositeSamplers = {
            {"u_hdr", 0},
            {"u_bloom", 1},
            {"u_adaptedLum", 2},
            {"u_colorLut", 3},
        };
        const auto compositeShader = Engine::loadShader(resources, shaderDir + "/post/composite", "shader:composite", compositeSamplers);

        // IBL bake + skybox. Bake sampler 0 = source (equirect / env cube).
        const std::unordered_map<std::string, int> equirectSamplers = { {"u_equirect", 0} };
        const std::unordered_map<std::string, int> envCubeSamplers   = { {"u_envCube", 0} };
        const auto equirectShader   = Engine::loadShader(resources, shaderDir + "/ibl/equirect",   "shader:ibl_equirect",   equirectSamplers);
        const auto irradianceShader = Engine::loadShader(resources, shaderDir + "/ibl/irradiance", "shader:ibl_irradiance", envCubeSamplers);
        const auto prefilterShader  = Engine::loadShader(resources, shaderDir + "/ibl/prefilter",  "shader:ibl_prefilter",  envCubeSamplers);
        const auto brdfShader       = Engine::loadShader(resources, shaderDir + "/ibl/brdf",       "shader:ibl_brdf");
        const auto skyboxShader     = Engine::loadShader(resources, shaderDir + "/skybox",         "shader:skybox",         envCubeSamplers);

        // Bloom (COD/Jimenez): both passes sample one source mip at slot 0.
        const std::unordered_map<std::string, int> bloomSamplers = { {"u_src", 0} };
        const auto bloomDownShader = Engine::loadShader(resources, shaderDir + "/post/bloom_down", "shader:bloom_down", bloomSamplers);
        const auto bloomUpShader   = Engine::loadShader(resources, shaderDir + "/post/bloom_up",   "shader:bloom_up",   bloomSamplers);

        // Auto-exposure: lum shader reads the scene; adapt reads lum + history.
        const std::unordered_map<std::string, int> lumSamplers   = { {"u_hdr", 0} };
        const std::unordered_map<std::string, int> adaptSamplers = { {"u_lumTex", 0}, {"u_prevAdapt", 1} };
        const auto lumShader      = Engine::loadShader(resources, shaderDir + "/post/lum",      "shader:lum",      lumSamplers);
        const auto exposureShader = Engine::loadShader(resources, shaderDir + "/post/exposure", "shader:exposure", adaptSamplers);

        // Depth/normal prepass + GTAO. GTAO reads the view-space MRT.
        const std::unordered_map<std::string, int> gtaoSamplers = { {"u_normalTex", 0}, {"u_posTex", 1} };
        const auto prepassShader = Engine::loadShader(resources, shaderDir + "/prepass",   "shader:prepass");
        const auto gtaoShader    = Engine::loadShader(resources, shaderDir + "/post/gtao", "shader:gtao", gtaoSamplers);

        // SSR reuses the prepass G-buffer + resolved HDR (slots 0/1/2).
        const std::unordered_map<std::string, int> ssrSamplers = {
            {"u_sceneColor", 0}, {"u_viewNormal", 1}, {"u_viewPos", 2}
        };
        const auto ssrShader = Engine::loadShader(resources, shaderDir + "/post/ssr", "shader:ssr", ssrSamplers);

        // TAA reprojects history (slots: current/history/viewPos = 0/1/2).
        const std::unordered_map<std::string, int> taaSamplers = {
            {"u_current", 0}, {"u_history", 1}, {"u_viewPos", 2}
        };
        const auto taaShader = Engine::loadShader(resources, shaderDir + "/post/taa", "shader:taa", taaSamplers);

        // DoF + motion blur read scene/viewPos (slots 0/1) into the scratch.
        const std::unordered_map<std::string, int> postGeomSamplers = {
            {"u_scene", 0}, {"u_viewPos", 1}
        };
        const auto dofShader = Engine::loadShader(resources, shaderDir + "/post/dof",         "shader:dof",         postGeomSamplers);
        const auto mbShader  = Engine::loadShader(resources, shaderDir + "/post/motion_blur", "shader:motion_blur", postGeomSamplers);

        // Render passes - shadow runs first so the forward pass can sample its result.
        renderSystem.setBackend(std::make_unique<Engine::GLBackend>());
        // Bake runs first (no-ops unless the environment map changed).
        renderSystem.addPass(std::make_unique<Engine::GLIBLBakePass>(
            equirectShader, irradianceShader, prefilterShader, brdfShader));
        renderSystem.addPass(std::make_unique<Engine::GLShadowPass>(shadowShader));
        // Depth/normal prepass then GTAO; the forward pass samples the AO.
        renderSystem.addPass(std::make_unique<Engine::GLPrepass>(prepassShader));
        renderSystem.addPass(std::make_unique<Engine::GLGTAOPass>(gtaoShader));
        auto forwardPass = std::make_unique<Engine::GLForwardPass>(pbrShader);
        forwardPass->setShader(Engine::MaterialType::Unlit, unlitShader);
        renderSystem.addPass(std::move(forwardPass));
        // Skybox fills the background in the HDR target, after opaque.
        renderSystem.addPass(std::make_unique<Engine::GLSkyboxPass>(skyboxShader));
        auto aabbPass = std::make_unique<Engine::GLAABBDebugPass>(aabbShader);
        aabbPass->setEnabled(false);
        renderSystem.addPass(std::move(aabbPass));
        renderSystem.addPass(std::make_unique<Engine::GLGridPass>(gridShader));
        // Screen-space reflections, additively blended into the HDR scene.
        renderSystem.addPass(std::make_unique<Engine::GLSSRPass>(ssrShader));
        // TAA (off by default) stabilises the resolved HDR before bloom.
        renderSystem.addPass(std::make_unique<Engine::GLTAAPass>(taaShader));
        // DoF then motion blur (both off by default) over the resolved HDR.
        renderSystem.addPass(std::make_unique<Engine::GLDofPass>(dofShader));
        renderSystem.addPass(std::make_unique<Engine::GLMotionBlurPass>(mbShader));
        // Bloom over the resolved HDR scene; blended in the composite.
        renderSystem.addPass(std::make_unique<Engine::GLBloomPass>(bloomDownShader, bloomUpShader));
        // Auto-exposure metering + eye adaptation (read by the composite).
        renderSystem.addPass(std::make_unique<Engine::GLExposurePass>(lumShader, exposureShader));
        // Final pass: resolve HDR + bloom + exposure/AgX/sRGB to the backbuffer.
        renderSystem.addPass(std::make_unique<Engine::GLCompositePass>(compositeShader));

        // Hot reload: file change → asset version bump → backend resyncs.
        auto& fileWatcher = engine.addSystem<Engine::FileWatcher>(Engine::SystemStage::Input);
        Engine::watchShader(fileWatcher, resources, pbrShader);
        Engine::watchShader(fileWatcher, resources, unlitShader);
        Engine::watchShader(fileWatcher, resources, aabbShader);
        Engine::watchShader(fileWatcher, resources, gridShader);
        Engine::watchShader(fileWatcher, resources, shadowShader);
        Engine::watchShader(fileWatcher, resources, compositeShader);
        Engine::watchShader(fileWatcher, resources, equirectShader);
        Engine::watchShader(fileWatcher, resources, irradianceShader);
        Engine::watchShader(fileWatcher, resources, prefilterShader);
        Engine::watchShader(fileWatcher, resources, brdfShader);
        Engine::watchShader(fileWatcher, resources, skyboxShader);
        Engine::watchShader(fileWatcher, resources, bloomDownShader);
        Engine::watchShader(fileWatcher, resources, bloomUpShader);
        Engine::watchShader(fileWatcher, resources, lumShader);
        Engine::watchShader(fileWatcher, resources, exposureShader);
        Engine::watchShader(fileWatcher, resources, prepassShader);
        Engine::watchShader(fileWatcher, resources, gtaoShader);
        Engine::watchShader(fileWatcher, resources, ssrShader);
        Engine::watchShader(fileWatcher, resources, taaShader);
        Engine::watchShader(fileWatcher, resources, dofShader);
        Engine::watchShader(fileWatcher, resources, mbShader);

        // Default scene: a single cube at the origin under a directional
        // light. Scene/asset round-trip happy: every asset has a source
        // descriptor so save → cold-start load reproduces this exactly.
        auto cameraEntity = generateDefaultScene(engine);
        cameraController.setCameraEntity(cameraEntity);

        // Default IBL environment (editable live from the Environment panel).
        renderSystem.getEnvironment().environmentMapPath = rootDir + "/assets/envs/environment.hdr";

        engine.run();

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
    } catch (...) {
        LOG_FATAL("Unknown exception");
    }

    LOG_INFO("Shutdown successfully!");
    return 0;
}
