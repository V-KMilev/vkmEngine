#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_backend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "vkm_log/logger.h"
#include "vkm_opengl/core/version.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/shader/shader_preprocess.h"
#include "vkm_opengl/shader/shader_reload.h"
#include "vkm_opengl/texture/texture.h"

#include "pass/gl_frame_context.h"
#include "pass/gl_pass.h"
#include "gl_profiler.h"
#include "gl_shader_constants.h"
#include "gl_window_surface.h"
#include "gl_image_upload.h"
#include "asset/gl_material.h"
#include "convention/gl_depth.h"
#include "offline/gl_capture_lighting.h"
#include "pass/scene/gl_shadow_pass.h"
#include "pass/scene/gl_depth_prepass.h"
#include "pass/scene/gl_resolve_pass.h"
#include "pass/light/gl_cluster_pass.h"
#include "pass/light/gl_fog_pass.h"
#include "pass/light/gl_reflection_pass.h"
#include "pass/overlay/gl_debug_draw_pass.h"
#include "pass/scene/gl_decal_pass.h"
#include "pass/scene/gl_particle_pass.h"
#include "pass/light/gl_gtao_pass.h"
#include "pass/scene/gl_forward_pass.h"
#include "pass/scene/gl_outline_pass.h"
#include "pass/scene/gl_water_pass.h"
#include "pass/light/gl_atmosphere_pass.h"
#include "pass/scene/gl_skybox_pass.h"
#include "pass/post/gl_bloom_pass.h"
#include "pass/overlay/gl_grid_pass.h"
#include "pass/post/gl_composite_pass.h"
#include "pass/post/gl_upscale_pass.h"
#include "pass/overlay/gl_loading_pass.h"
#include "pass/overlay/gl_splash_pass.h"
#include "pass/overlay/gl_ui_pass.h"
#include "base/hash/fnv1a.h"
#include "ecs/settings/environment.h"
#include "io/project/roots.h"
#include "vkm_platform/threading/thread_pool.h"
#include "vkm_platform/window/window_manager.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "io/asset/generate/mesh_generators.h"
#include "system/render/render_view.h"
#include "system/render/data/light_data.h"
#include "system/sky/atmosphere.h"

namespace Vkm::Engine {

std::unique_ptr<Platform::WindowSurface> GLBackend::windowSurface() {
    return std::make_unique<GLWindowSurface>();
}

GLBackend::ConstantsInstalled::ConstantsInstalled() {
    Vkm::OpenGL::setShaderPrelude(shaderConstants());
}

namespace {

constexpr uint32_t WARM_SLICES = 8;  ///< The frames behind the loading screen a world is drawn whole over, so
                                     ///< none of them holds long.

constexpr std::chrono::milliseconds STARTUP_PRESENT_INTERVAL{100};
    ///< The least time between two presents while the passes build: each waits on the display.

constexpr std::chrono::milliseconds STARTUP_CHECK_SHARE{4};  ///< A frame's share of checking startup programs
                                                             ///< (programsReady()), in which many quick
                                                             ///< checks fit. One check longer than it means
                                                             ///< the driver builds as it checks, holding
                                                             ///< every GL call meanwhile, so checking all at
                                                             ///< once would be one long frozen frame.

/**
 * @brief Present black, then answer the desktop: the window is not hung while the passes build,
 *        and shows nothing before the splash does.
 *
 * @param gl     The context, whose cached state the clear goes through.
 * @param window The window shown.
 */
void presentBlack(Vkm::OpenGL::Context& gl, Platform::WindowManager& window) {
    if (window.getWidth() == 0 || window.getHeight() == 0) return;
    Vkm::OpenGL::FrameBuffer::bindDefault();
    gl.setViewport(0, 0, static_cast<int32_t>(window.getWidth()), static_cast<int32_t>(window.getHeight()));
    gl.setColorMask(true, true, true, true);
    gl.setClearColor(glm::vec4(0.0f));
    gl.clear(true, false);
    window.swapBuffers();
    window.pumpEvents();
}

} // namespace

GLBackend::GLBackend()
    : m_unitCube(m_mirror.meshPool(), generateCube())
    , m_sceneCapture(m_unitCube, m_objects, m_forwardPrograms)
    , m_cubeConvolver(m_unitCube)
    , m_iblBaker(m_cubeConvolver)
    , m_irradianceBaker(m_sceneCapture)
    , m_preview(m_sceneCapture) {}

GLBackend::~GLBackend() = default;

bool GLBackend::init(Platform::WindowManager& window) {
    // GLEW and the context come with the window, already current. Presentation is the engine
    // loop's, but for the black presented below while the passes build, before its first frame.
    PROFILE_GPU_CONTEXT();

    // Every unit gl_bindings.h numbers must exist in each stage that samples
    // one, or a sampler past the last binds to nothing and reads black.
    // A vertex stage samples too (shaders/opengl/scene/particle/vertex.shader).
    const int32_t units = std::min({
        m_context.integer(GL_MAX_TEXTURE_IMAGE_UNITS),
        m_context.integer(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS),
        m_context.integer(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS),
    });
    if (units <= static_cast<int32_t>(GLBindings::highestTextureUnit())) {
        LOG_ERROR(
            "This GPU samples %d texture units a stage; the renderer binds up to %u",
            units,
            GLBindings::highestTextureUnit() + 1
        );
        return false;
    }

    // Likewise every storage-buffer binding point it numbers.
    const int32_t storageBindings = m_context.integer(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS);
    if (storageBindings <= static_cast<int32_t>(GLBindings::highestSSBOBinding())) {
        LOG_ERROR(
            "This GPU has %d storage-buffer binding points; the renderer binds up to %u",
            storageBindings,
            GLBindings::highestSSBOBinding() + 1
        );
        return false;
    }

    const int32_t textureEdge = m_context.maxTextureSize();
    if (textureEdge < static_cast<int32_t>(BACKEND_MIN_TEXTURE_EDGE)) {
        LOG_ERROR(
            "This GPU's textures are at most %d texels across; the engine bakes up to %u",
            textureEdge,
            BACKEND_MIN_TEXTURE_EDGE
        );
        return false;
    }

    m_context.setDefaultState();

    // Clip z in 0..w: the depth convention/gl_depth.h states, the eye's reversed.
    m_context.setClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

    // Pixels are tightly packed in a file's own channel count, so a row need not be a multiple
    // of four bytes; at GL's default alignment such a texture uploads sheared and overreads.
    m_context.setPixelAlignment(1);

    // A linear read near a face edge filters into the next face; without it a cube shows seams.
    m_context.setSeamlessCubes(true);

    // Passes build their shaders, so this runs after the context exists. The order is
    // load-bearing: see docs/reference/rendering.md, "The passes (fixed order)". The driver
    // compiles the programs on its own threads (ShaderBase::compileInParallel) while the frames
    // after init() draw the chrome alone (programsReady()); building them is quick.
    using Wall = std::chrono::steady_clock;
    Wall::time_point shown = Wall::now();
    presentBlack(m_context, window);
    const auto add = [&](PassEntry entry) {
        m_passes.push_back(std::move(entry));
        if (Wall::now() - shown < STARTUP_PRESENT_INTERVAL) return;
        shown = Wall::now();
        presentBlack(m_context, window);
    };
    // What a first frame draws - the splash, and the loading screen with the UI pass it draws a
    // project's screen through - is built before the other passes, so the driver compiles its
    // programs ahead of theirs, though behind those this backend's members built as it was made;
    // then the first sync's layered materials, then the forward programs, the slowest, as the world
    // waits longest on them.
    auto splash  = std::make_unique<GLSplashPass>();
    auto ui      = std::make_unique<GLUIPass>();
    auto loading = std::make_unique<GLLoadingPass>(*ui);
    // And what the first frame's sync runs: it builds the layered materials before any pass draws.
    m_mirror.buildPrograms();
    m_forwardPrograms.build();
    add({"DepthPrepass",   std::make_unique<GLDepthPrepass>()});
    add({"ResolveDepth",   std::make_unique<GLResolvePass>(GLResolvePass::Scope::Geometry)});
    add({"GTAO",           std::make_unique<GLGTAOPass>()});
    add({"ClusterCull",    std::make_unique<GLClusterPass>()});
    add({"Shadow",         std::make_unique<GLShadowPass>()});
    add({"FogCompute",     std::make_unique<GLFogPass>()});
    add({"Atmosphere",     std::make_unique<GLAtmospherePass>()});
    add({"Water",          std::make_unique<GLWaterPass>()});
    add({"Skybox",         std::make_unique<GLSkyboxPass>()});
    auto forward = std::make_unique<GLForwardPass>(m_forwardPrograms);
    add({"Forward",        std::move(forward)});
    add({"Outline",        std::make_unique<GLOutlinePass>()});
    add({"ResolveColor",   std::make_unique<GLResolvePass>(GLResolvePass::Scope::Color)});
    add({"Particles",      std::make_unique<GLParticlePass>(m_camera)});
    add({"Reflections",    std::make_unique<GLReflectionPass>()});
    add({"Decals",         std::make_unique<GLDecalPass>()});
    add({"Bloom",          std::make_unique<GLBloomPass>()});
    auto composite = std::make_unique<GLCompositePass>();
    m_composite = composite.get();
    add({"Composite",      std::move(composite)});
    add({"Upscale",        std::make_unique<GLUpscalePass>()});
    add({"Grid",           std::make_unique<GLGridPass>()});
    add({"DebugLines",     std::make_unique<GLDebugDrawPass>()});
    add({"UI",             std::move(ui)});
    m_splash = splash.get();
    m_chrome = {splash.get(), loading.get()};
    add({"Splash",         std::move(splash)});
    add({"Loading",        std::move(loading)});

    // Forward+ cluster light grid: allocate its SSBO now the context is live.
    m_clusterGrid.init();

    // Froxel fog volumes and the editor preview rig allocate lazily (see
    // GLFogPass, GLPreview), so a host that uses neither pays for neither.

    // Allocates cube-map arrays, so it needs the live context.
    m_probes.init(m_sceneCapture, m_cubeConvolver);

    // The split-sum LUT depends on no environment, and a scene without a sky
    // still reads it.
    m_iblBaker.integrateBrdf(m_context, m_ibl);

    const std::string version = m_context.versionString();
    BackendInfo info;
    info.api    = version.empty() ? "OpenGL" : "OpenGL " + version;
    info.device = m_context.rendererString();
    LOG_INFO("%s on %s", info.api.c_str(), info.device.c_str());
    setInfo(std::move(info));

    return true;
}

void GLBackend::render(const RenderView& view, const ResourceManager& resources) {
    // A window closed last frame drew its preview before it closed; that frame's UI has drawn now.
    m_preview.freeReleased();

    // A minimized window or collapsed viewport has no pixels; stopping here spares every pass. The
    // stream's frame still ends: an editor preview may have written into it.
    if (view.viewportWidth == 0 || view.viewportHeight == 0) {
        m_eyeDrawn = false;
        m_mirror.stream().endFrame();
        return;
    }

    PROFILE_SCOPE("GLBackend::render");
    PROFILE_GPU_SCOPE("GPU.Frame");

    // Before anything reads a cache: what was built from a replaced world or asset graph cannot
    // be trusted, and nothing downstream can tell.
    onWorldReplaced(view, resources);

    // A material preview is lit and tonemapped as this frame is, at the authored exposure: an adapting
    // one would re-render every thumbnail each frame.
    const LightData* key = view.lists.keyLight();
    m_previewScene.sun          = key ? std::optional<LightData>(*key) : std::nullopt;
    m_previewScene.skyIntensity = view.environment->sky.intensity;
    m_previewScene.tonemap      = view.settings->tonemap;
    m_previewScene.exposure     = view.settings->exposure;

    {
        PROFILE_SCOPE("Render/SyncAssets");
        m_mirror.sync(view, resources);
        m_mirror.setTextureAnisotropy(static_cast<float>(view.settings->textureAnisotropy));
    }
    // The splash must not stall.
    if (!programsReady(view.splashShows())) {
        drawChrome(view);
        m_mirror.stream().endFrame();
        return;
    }

    // From the Environment's authored angles, which the key light is aimed from too (see
    // SkySystem), so sky and shadows agree.
    const glm::vec3 sunDir = view.environment->sunDirection();

    // The atmosphere's air tables first: what follows reads them.
    std::optional<SkyParams> sky;
    const SkySettings& skySettings = view.environment->sky;
    // An image the sky no longer names is not baked, and is decoded again if it is named again.
    if (skySettings.procedural || skySettings.hdrPath.empty()) forgetHDRSky();
    if (skySettings.procedural) {
        sky = skyParams(*view.environment, sunDir);
        if (m_bakedAir.changed(sky->air)) m_airBaker.bake(m_context, m_atmosphere, sky->air);
        followProceduralSky(*sky);
    } else if (skySettings.hdrPath.empty()) {
        // No image-based lighting wanted, not a failed load: the last sky goes, without an error.
        if (m_ibl.isReady()) clearEnvironment();
    } else {
        followHDRSky(skySettings.hdrPath);
    }

    // Before the shadow plan, so both agree on the tile size.
    {
        PROFILE_SCOPE("Render/ShadowAtlasInit");
        m_shadowAtlas.init(m_context, view.settings->shadowResolution);
        // On its units for what draws before a pass binds them - a bake's capture, which binds only
        // its own sun: the forward programs declare them as shadow samplers, and the driver
        // validates a declared sampler at draw time.
        m_shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
        m_shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);
    }

    // A world arrived behind the loading screen is drawn there whole, a slice of its objects a frame, as
    // if seen, with a particle of each blend. The driver builds a program for the state of its first
    // draw, a few ms each, which a frame of play would pay where a cut first shows the sea or a spark.
    const bool covered = view.loadingCovers();
    const bool warming = covered && m_warmSlices < WARM_SLICES && view.hasCamera && !view.worldArriving;
    m_warmOwed = warming;
    if (covered && !warming) {
        // Nothing of the world shows: the screen alone is drawn, and the bakes after it have the time the
        // world would have taken. They read every object's transform and lightmap place and the probes'
        // layers; no shadow is planned, so the sun's held layers keep what they show.
        uploadObjects(view, false);
        if (view.hasCamera) m_probes.bind(view, m_lightmaps);
        drawChrome(view);
        m_eyeDrawn = false;
    } else {
        drawWorld(view, warming, sunDir, sky);
    }

    // A frame with no camera gathered nothing to bake from; one whose world still arrives waits for
    // it (RenderView::worldArriving).
    m_bakes     = view.hasCamera;
    m_bakesHeld = m_bakes && view.worldArriving;
    if (m_bakes && !m_bakesHeld) bakeCaptures(view);
    // The bakes draw into targets of their own and leave one bound; what draws after the frame - an
    // editor's UI - draws into the window.
    Vkm::OpenGL::FrameBuffer::bindDefault();

    // Every command reading what the frame streamed is issued: fence its region.
    m_mirror.stream().endFrame();

    PROFILE_GPU_COLLECT();
}

void GLBackend::drawWorld(
    const RenderView& view,
    bool warming,
    const glm::vec3& sunDir,
    const std::optional<SkyParams>& sky
) {
    // The reflection inputs exist while the reflection pass reads them.
    const bool reflect = view.settings->ssr;
    m_sceneHDR.resize(m_context, view.sceneWidth(), view.sceneHeight(), 1, reflect);
    m_postA.resize(view.sceneWidth(), view.sceneHeight());
    m_postB.resize(view.sceneWidth(), view.sceneHeight());
    m_bloom.resize(view.sceneWidth(), view.sceneHeight());

    // The multisample twin exists only while MSAA is on; at 4x it is the frame's largest
    // allocation. What draws where: docs/reference/rendering.md.
    const uint32_t samples = view.settings->msaaSamples;
    if (samples > 1) {
        m_sceneMS.resize(m_context, view.sceneWidth(), view.sceneHeight(), samples, reflect);
    } else {
        m_sceneMS.release();
    }
    GLTarget& sceneRender = (samples > 1) ? m_sceneMS : m_sceneHDR;

    // Shadows first: the plan gives each light the atlas slot the lights SSBO carries (spot.w).
    {
        PROFILE_SCOPE("Shadow/Plan");
        m_shadowPlan.build(view, m_mirror, m_shadowAtlas.tileResolution());
        // Latched, so a plan the atlas refuses says so once.
        const bool laidOut = m_shadowAtlas.layout(m_shadowPlan.tileSizes());
        if (!laidOut && !m_layoutRefusedLogged) {
            LOG_ERROR("Shadow tiles did not fit the atlas; shadows keep last frame's places");
        }
        m_layoutRefusedLogged = !laidOut;
        m_shadowPlan.placeTiles(m_shadowAtlas);
    }

    if (warming) {
        m_warm = warmView(view);
        const std::vector<uint32_t>& scene = view.objects->scene;
        m_warmList.assign(
            scene.begin() + static_cast<std::ptrdiff_t>(scene.size() * m_warmSlices / WARM_SLICES),
            scene.begin() + static_cast<std::ptrdiff_t>(scene.size() * (m_warmSlices + 1) / WARM_SLICES)
        );
    }
    const RenderView& drawn = warming ? m_warm : view;

    // Bucket the drawables once, for every pass that reads a bucket; and see whether water is drawn,
    // which is what the waves' block, swell, ripples and shore are for.
    const DrawnWater water = partitionDrawables(view, warming ? m_warmList : view.objects->visible);

    // Per-frame UBOs: uploaded and bound once here, visible to every pass.
    {
        PROFILE_SCOPE("Render/FrameUBOs");
        const float width  = static_cast<float>(view.sceneWidth());
        const float height = static_cast<float>(view.sceneHeight());
        m_camera.update(view.camera, glm::vec2(width, height));
        m_waves.update(view.lists.waves, water.shown);
        if (water.shown) m_waterBodies.update(view.objects->waters);
        const std::vector<LightData>& lights = view.lists.lights;
        m_lights.update(lights, m_shadowPlan.lightSlots(), false, m_lightmaps.channels(lights));
        m_shadowPlan.uploadAndBind();
    }

    // The GPU occlusion cull runs from a camera alone, reading every object's world box.
    const bool occlusionCulled = view.settings->occlusionCulling && view.hasCamera && !warming;
    uploadObjects(view, occlusionCulled);

    // ...and batch the opaque bucket once too (see GLFrameContext::opaqueBatch).
    {
        PROFILE_SCOPE("Render/OpaqueBatch");
        m_opaqueBatcher.buildGrouped(m_opaque, *view.objects, m_mirror, m_skinPalette.count(), MeshOf::Eye);
    }

    // Each pass binds and clears its own target.
    GLFrameContext ctx = frameContext(drawn, sceneRender);

    ctx.continues  = m_eyeDrawn && !warming;
    ctx.sunDir     = sunDir;
    ctx.waterShown = water.shown;
    ctx.shore      = water.shore;
    ctx.exposure   = m_exposure.stops(*view.settings, view.frameSeconds);
    ctx.sky        = sky ? &*sky : nullptr;
    ctx.lightmaps  = &m_lightmaps;
    if (occlusionCulled) {
        PROFILE_SCOPE("Render/OcclusionPrepare");
        if (m_occlusion.prepare(m_opaqueBatcher, *view.objects)) {
            ctx.occlusion = &m_occlusion;
        }
    }
    m_waveFields.beginFrame(view.lists.waves, !view.objects->waters.empty(), water.swell);

    // Post color chain: the scene starts on the geometry target; the first
    // post pass moves it into a scratch and the chain ping-pongs from there.
    ctx.scratchA = &m_postA;
    ctx.scratchB = &m_postB;
    ctx.colorSrc = &m_sceneHDR;
    ctx.colorDst = &m_postA;

    // No camera gathers no probes, which is not the scene losing them: their bakes stay.
    ctx.probeCount = view.hasCamera ? m_probes.bind(view, m_lightmaps) : 0;

    // Every pass starts from the same state, so none puts back what the last changed; and so does
    // what runs after them.
    for (const auto& entry : m_passes) {
        PROFILE_SCOPE_NAMED(entry.name);
        PROFILE_GPU_SCOPE_NAMED(entry.name);
        passState();
        entry.pass->execute(ctx);
    }
    passState();
    m_eyeDrawn = !warming;
    if (warming) ++m_warmSlices;

    PROFILE_PLOT("Render/Drawables",   static_cast<int64_t>(view.objects->visible.size()));
    PROFILE_PLOT("Render/Opaque",      static_cast<int64_t>(m_opaque.size()));
    PROFILE_PLOT("Render/AlphaMask",   static_cast<int64_t>(m_alphaMask.size()));
    PROFILE_PLOT("Render/Transparent", static_cast<int64_t>(m_transparent.size()));
    PROFILE_PLOT("Render/Lights",      static_cast<int64_t>(view.lists.lights.size()));
    PROFILE_PLOT("Render/SceneMeshes", static_cast<int64_t>(view.objects->scene.size()));
    PROFILE_PLOT(
        "Render/Particles",
        static_cast<int64_t>(
            view.lists.particlesAdditive.size() + view.lists.particlesAlpha.size()
                + view.lists.particlesDistortion.size()
        )
    );
    PROFILE_PLOT("Render/Probes",      static_cast<int64_t>(ctx.probeCount));
    PROFILE_PLOT("Render/SkinBones",   static_cast<int64_t>(m_skinPalette.count()));

    // The scene as lit, before the bloom and the tonemap, so what it measures does not depend on
    // the exposure it sets.
    if (view.settings->autoExposure && view.hasCamera && !warming) {
        PROFILE_GPU_SCOPE("Render/Exposure");
        m_exposure.measure(*ctx.colorSrc, view.sceneWidth(), view.sceneHeight(), view.frameSeconds);
    }
    PROFILE_PLOT("Render/Exposure", static_cast<double>(ctx.exposure));
}

void GLBackend::uploadObjects(const RenderView& view, bool boxed) {
    // Before the object upload and the batch, which both ask whether the frame posed anything.
    {
        PROFILE_SCOPE("Render/SkinPalette");
        m_skinPalette.update(view.lists.skinMatrices, m_mirror.stream());
    }
    // Every object's transform, once: an index list names objects in it.
    PROFILE_SCOPE("Render/Objects");
    m_objects.upload(*view.objects, m_skinPalette.count() > 0, boxed, m_mirror.stream());
    m_lightmaps.prepare(*view.objects, m_mirror);
    m_lightmaps.bindObjects();
}

bool GLBackend::programsReady(bool quickOnly) {
    if (m_programsReady) return true;
    // A frame's share at most; for an asker that must not stall, only while checks stay quick. A
    // program that failed throws as it is checked, at startup, not at its first use.
    const auto begun = std::chrono::steady_clock::now();
    while (!(quickOnly && m_checksSlow)) {
        const auto one = std::chrono::steady_clock::now();
        if (!Vkm::OpenGL::checkNextShader()) break;
        const auto now = std::chrono::steady_clock::now();
        if (now - one > STARTUP_CHECK_SHARE) m_checksSlow = true;
        if (now - begun > STARTUP_CHECK_SHARE) break;
    }
    const Vkm::OpenGL::ShaderCount count = Vkm::OpenGL::countShaders();
    m_programsReady = count.compiling == 0 && count.unchecked == 0;
    return m_programsReady;
}

void GLBackend::drawChrome(const RenderView& view) {
    Vkm::OpenGL::FrameBuffer::bindDefault();
    passState();
    const auto width  = static_cast<int32_t>(view.surfaceWidth);
    const auto height = static_cast<int32_t>(view.surfaceHeight);
    m_context.setViewport(0, 0, width, height);
    m_context.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    m_context.clear(true, false);
    m_context.setClearColor(glm::vec4(0.0f));

    GLFrameContext ctx = frameContext(view, m_sceneHDR);
    for (GLPass* pass : m_chrome) {
        passState();
        pass->execute(ctx);
    }
    passState();
}

GLFrameContext GLBackend::frameContext(const RenderView& view, GLTarget& sceneRender) {
    return GLFrameContext{
        view,
        m_mirror,
        m_context,
        m_screenTri,
        m_unitCube,
        m_sceneHDR,
        sceneRender,
        m_shadowAtlas,
        m_shadowPlan,
        m_ibl,
        m_atmosphere,
        m_bloom,
        m_ao,
        m_clusterGrid,
        m_fog,
        m_irradiance,
        m_skinPalette,
        m_objects,
        m_waveFields,
        m_waterShore,
        m_alphaMask,
        m_transparent,
        m_opaqueBatcher
    };
}

void GLBackend::passState() {
    // Context caches it all, so a pass that agrees pays nothing.
    m_context.setDefaultState();
    m_context.setDepthFunc(GLDepth::EYE_TEST_EQUAL);
    m_context.setFaceCulling(false);
}

void GLBackend::bakeCaptures(const RenderView& view) {
    // The lights that reach the volume, or the sky, changing light its surfels again, its geometry
    // kept; a lamp moving outside it does not.
    const IrradianceVolumeData& box = view.lists.irradianceVolume;
    const Math::AABB within{box.center - box.halfExtents, box.center + box.halfExtents};
    const Math::AABB* held = view.lists.hasIrradianceVolume ? &within : nullptr;
    const uint64_t lights  = lightingDigest(view.lists.lights, held);
    const CaptureLighting volumeLighting{lights, m_ibl.version(), m_ibl.isReady()};
    m_irradianceBaker.update(m_context, m_irradiance, view, m_mirror, m_ibl, volumeLighting);

    // The fixed lights' shadows on the fixed meshes, when either has changed and settled.
    m_lightmapBaker.update(m_context, view, m_mirror, m_objects, m_lightmaps);

    // Re-bake probes that are new, version-bumped, moved or resized and holding still, or lit by
    // an older volume, after the volume, which lights what they capture of it; refresh those
    // under other light; none while the volume is part-way through its captures.
    if (m_irradianceBaker.baking()) return;
    const bool lit = view.lists.hasIrradianceVolume && m_irradiance.isReady();
    const GLIrradianceVolume* volume = lit ? &m_irradiance : nullptr;
    // Its lights are each probe's own, those that reach it (GLProbeManager::update).
    CaptureLighting probeLighting;
    probeLighting.sky             = m_ibl.version();
    probeLighting.skyReady        = m_ibl.isReady();
    probeLighting.volumeLight     = volume ? volume->lightId() : 0u;
    probeLighting.volumeIntensity = volume ? view.lists.irradianceVolume.intensity : 0.0f;
    m_probes.update(m_context, view, m_mirror, m_ibl, volume, probeLighting);
}

void GLBackend::followHDRSky(const std::string& path) {
    if (path != m_hdrPath) {
        m_hdrPath = path;
        // Resolved here, as the project's root may be pointed elsewhere while a worker reads. Shared, as
        // the pool copies its tasks: an image named after this one leaves this one's to nobody.
        const std::string file = Roots::resolveProjectPath(path).string();
        const auto decode = std::make_shared<std::packaged_task<HDRImage()>>([file] {
            PROFILE_SCOPE("Render/DecodeSky");
            return loadHDRImage(file);
        });
        m_hdrDecode = decode->get_future();
        Platform::ThreadPool::get().addTask([decode] { (*decode)(); });
    }
    if (!m_hdrDecode.valid() || m_hdrDecode.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    const HDRImage image = m_hdrDecode.get();
    if (!image.isValid()) {
        LOG_ERROR("The sky's image '%s' could not be read; the sky keeps the light it had", path.c_str());
        return;
    }
    PROFILE_SCOPE("Render/IBLBake");
    PROFILE_GPU_SCOPE("Render/IBLBake");
    m_iblBaker.bake(m_context, m_ibl, image, path);
    m_shownSky.reset();
}

void GLBackend::forgetHDRSky() {
    m_hdrPath.clear();
    m_hdrDecode = {};
}

void GLBackend::clearEnvironment() {
    m_ibl.markUnready();
    m_shownSky.reset();
}

SkyParams GLBackend::skyParams(const Environment& env, const glm::vec3& sunDir) {
    SkyParams sky;
    sky.sunDir         = sunDir;
    sky.sunIlluminance = Atmosphere::solarIlluminance(env.sky);
    sky.air            = Atmosphere::coefficients(env.sky);
    sky.nightRadiance  = env.night.radiance;
    sky.moonDir        = env.moonDirection();
    // A fraction of the disc: the halo tracks the moon's brightness, so one dial drives both.
    sky.moonHalo = env.night.moonIntensity * 0.15f;
    return sky;
}

void GLBackend::followProceduralSky(const SkyParams& sky) {
    const bool shown  = m_ibl.isReady() && m_shownSky;
    const bool jumped = !m_askedSky || !sky.driftsFrom(*m_askedSky);
    m_askedSky = sky;
    if (shown && !jumped && !m_iblBaker.baking() && sky == *m_shownSky) return;

    if (!shown || jumped) {
        PROFILE_SCOPE("Render/IBLBake");
        PROFILE_GPU_SCOPE("Render/IBLBake");
        m_iblBaker.bakeProcedural(m_context, m_ibl, m_atmosphere, sky);
        m_shownSky = sky;
        return;
    }

    PROFILE_SCOPE("Render/IBLStep");
    PROFILE_GPU_SCOPE("Render/IBLStep");
    if (!m_iblBaker.baking()) m_iblBaker.beginProcedural(sky);
    for (int step = GLIBLBaker::stepsFor(sky, *m_shownSky); step > 0; --step) {
        if (m_iblBaker.advance(m_context, m_ibl, m_atmosphere)) {
            m_shownSky = m_iblBaker.target();
            break;
        }
    }
}

void GLBackend::onWorldReplaced(const RenderView& view, const ResourceManager& resources) {
    const uint64_t assetEpoch = resources.epoch();
    const uint64_t worldEpoch = view.worldEpoch;
    if (assetEpoch == m_assetEpoch && worldEpoch == m_worldEpoch) return;

    PROFILE_SCOPE("Render/WorldReplaced");

    if (assetEpoch != m_assetEpoch) {
        m_assetEpoch = assetEpoch;
        m_mirror.invalidate();      // asset mirrors: handles and versions restart
        LOG_INFO("Asset graph swapped; asset mirrors dropped");
    }
    if (worldEpoch != m_worldEpoch) {
        m_worldEpoch = worldEpoch;
        m_probes.invalidate();                       // cube captures: same probe pose, different scene
        m_irradianceBaker.invalidate(m_irradiance);  // SH volume: same box and grid, different scene
        m_lightmapBaker.invalidate();                // lightmaps: another scene's objects
        m_lightmaps.clear();
        m_exposure.forget();                         // adapted to the last world's light
        LOG_INFO("Scene replaced; baked captures of it dropped");
    }
    m_warmSlices = 0;
    m_eyeDrawn   = false;
    // Held shadow tiles: pictures of both the world and the meshes it is made of.
    m_shadowAtlas.forgetHeld();
    m_shadowPlan.forgetHeld();
}

GLBackend::DrawnWater GLBackend::partitionDrawables(
    const RenderView& view,
    const std::vector<uint32_t>& listed
) {
    PROFILE_SCOPE("Render/Partition");
    m_opaque.clear();
    m_alphaMask.clear();
    m_transparent.clear();
    DrawnWater drawn;
    const RenderObjects& objects = *view.objects;
    m_opaque.reserve(listed.size());

    // How far up the waves carry water, by which the cull grew a box with all of them.
    const float rise = view.lists.waves.surface.farthestReach().y;
    float nearest    = INFINITY;
    for (const uint32_t object : listed) {
        const GLMaterial* material = m_mirror.getMaterial(objects.draws[object].material);
        const MaterialType type    = material ? material->getType() : MaterialType::Opaque;
        if (const uint16_t water = objects.draws[object].water) {
            drawn.shown = true;
            // The nearest water with a waterline, by its box; level, it rests at its box's top less
            // its share of the waves' rise.
            const Water& body = objects.waters[water - 1u];
            drawn.swell = drawn.swell || body.waves > 0.0f;
            const Math::AABB& box = objects.bounds[object];
            const glm::vec3 eye   = view.camera.position;
            const float apart     = glm::distance(eye, glm::clamp(eye, box.min, box.max));
            if (body.waterline > 0.0f && apart < nearest) {
                const float share = std::max(body.waves, 0.0f);
                nearest     = apart;
                drawn.shore = {box.max.y - share * rise, body.waterline, share};
            }
        }
        if (type == MaterialType::Transparent)    m_transparent.push_back(object);
        else if (type == MaterialType::AlphaMask) m_alphaMask.push_back(object);
        else                                      m_opaque.push_back(object);
    }
    return drawn;
}

RenderView GLBackend::warmView(const RenderView& view) {
    RenderView warm = view;
    RenderView::SceneLists& lists = warm.lists;
    // A meter before the eye, small and clear: the cover hides it anyway.
    const glm::vec3 ahead = view.camera.position - glm::vec3(view.camera.invView[2]);
    ParticleData particle{};
    particle.positionSize = glm::vec4(ahead, 0.05f);
    particle.uvRect       = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    lists.particlesAdditive.push_back(particle);
    lists.particlesAlpha.push_back(particle);
    lists.particlesDistortion.push_back(particle);

    const auto first = static_cast<uint32_t>(lists.stripPoints.size());
    for (uint32_t i = 0; i < 2; ++i) {
        StripPoint point{};
        point.positionWidth = glm::vec4(ahead + glm::vec3(0.0f, 0.1f * static_cast<float>(i), 0.0f), 0.05f);
        point.params        = glm::vec4(
            static_cast<float>(i),
            static_cast<float>(first),
            static_cast<float>(first + 1),
            0.0f
        );
        lists.stripPoints.push_back(point);
    }
    const ParticleStrip strip{first, 2, 0.0f};
    lists.stripsAdditive.push_back(strip);
    lists.stripsAlpha.push_back(strip);
    lists.stripsDistortion.push_back(strip);
    return warm;
}

uint64_t GLBackend::previewLook() const {
    const PreviewScene& look = m_previewScene;
    uint64_t digest = fnv1a64Bytes(&look.skyIntensity, sizeof(look.skyIntensity));
    digest = fnv1a64Bytes(&look.tonemap, sizeof(look.tonemap), digest);
    digest = fnv1a64Bytes(&look.exposure, sizeof(look.exposure), digest);
    if (look.sun) {
        digest = fnv1a64Bytes(&look.sun->direction, sizeof(look.sun->direction), digest);
        digest = fnv1a64Bytes(&look.sun->color, sizeof(look.sun->color), digest);
        digest = fnv1a64Bytes(&look.sun->intensity, sizeof(look.sun->intensity), digest);
        digest = fnv1a64Bytes(&look.sun->sourceRadius, sizeof(look.sun->sourceRadius), digest);
    }
    return digest;
}

GpuTextureId GLBackend::renderPreview(const PreviewRequest& request, const ResourceManager& resources) {
    // Drawn with the forward programs, which would hold the window until checked: asked again later.
    // Not the frame's to stall for, so only quick checks.
    if (!programsReady(true)) return {};
    // The preview binds its own camera and lights; the next frame binds the frame's again.
    return m_preview.render(m_context, m_mirror, m_ibl, m_previewScene, m_shadowAtlas, request, resources);
}

GpuTextureId GLBackend::previewTexture(uint64_t key) const {
    return m_preview.texture(key);
}

void GLBackend::releasePreview(uint64_t key) {
    m_preview.release(key);
}

void GLBackend::releaseAllPreviews() {
    m_preview.releaseAll();
}

GpuTextureId GLBackend::textureId(const TextureHandle& handle) const {
    const Vkm::OpenGL::Texture2D* tex = m_mirror.getTexture(handle);
    return tex ? tex->getID() : 0;
}

GpuTextureId GLBackend::ensureTexture(const TextureHandle& handle, const ResourceManager& resources) {
    m_mirror.ensureTexture(handle, resources);
    return textureId(handle);
}

GpuTextureId GLBackend::chromeImage(const std::string& path) {
    if (const auto it = m_chromeImages.find(path); it != m_chromeImages.end()) {
        return it->second ? static_cast<GpuTextureId>(it->second->getID()) : 0;
    }

    // Cached on failure too, so an undecodable path is not re-read every frame.
    std::unique_ptr<Vkm::OpenGL::Texture2D>& slot = m_chromeImages[path];
    slot = uploadImageFile(path);
    if (!slot) {
        LOG_ERROR("Chrome image '%s' could not be decoded", path.c_str());
        return 0;
    }
    return static_cast<GpuTextureId>(slot->getID());
}

uint32_t GLBackend::reloadChangedShaders() {
    // A Vkm::OpenGL::Shader registers itself, so this reaches a program without its
    // owner opting in.
    const uint32_t reloaded = Vkm::OpenGL::reloadChangedShaders("shaders/opengl");
    // Held shadow tiles were drawn by old programs, and which reloaded is not told (an include
    // reaches several), so any reload forgets them all.
    if (reloaded > 0) {
        m_shadowAtlas.forgetHeld();
        m_shadowPlan.forgetHeld();
    }
    return reloaded;
}

bool GLBackend::reloadChangedGrading() {
    return m_composite && m_composite->reloadChangedGrading();
}

uint32_t GLBackend::maxAnisotropy() const {
    return static_cast<uint32_t>(Vkm::OpenGL::maxSupportedAnisotropy());
}

bool GLBackend::warmedWhole() const {
    return m_warmSlices >= WARM_SLICES;
}

RenderReadiness GLBackend::readiness() const {
    RenderReadiness readiness;
    readiness.splashPending = m_splash && m_splash->pending();
    readiness.skyPending    = m_hdrDecode.valid();
    if (!m_programsReady) {
        const Vkm::OpenGL::ShaderCount count = Vkm::OpenGL::countShaders();
        readiness.programs = count.live;
        readiness.compiled = count.live - count.unchecked;
    }
    // A world behind the loading screen is owed the slices it is drawn there in, so it shows whole.
    if (m_warmOwed) {
        readiness.done  += m_warmSlices;
        readiness.total += WARM_SLICES;
    }
    // Only what the frames go on to bake is owed: with no camera they bake nothing. Held while its
    // world arrives, its first captures are owed though none has begun.
    if (!m_bakes) return readiness;
    if (m_bakesHeld) {
        ++readiness.total;
        return readiness;
    }
    if (m_irradianceBaker.baking()) {
        readiness.done  += m_irradianceBaker.stepsTaken();
        readiness.total += m_irradianceBaker.bakeSteps();
    }
    readiness.done  += m_probes.bakedLayers();
    readiness.total += m_probes.ownedLayers();
    const RenderReadiness lightmaps = m_lightmapBaker.progress();
    readiness.done  += lightmaps.done;
    readiness.total += lightmaps.total;
    return readiness;
}

bool GLBackend::holdsPixels(const TextureHandle& texture, uint64_t version) const {
    return m_mirror.holdsPixels(texture, version);
}

bool GLBackend::readFrame(const RenderView& view) {
    return m_frameReader.read(view);
}

bool GLBackend::takeFrame(FramePixels& frame, bool wait) {
    return m_frameReader.take(frame, wait);
}

} // namespace Vkm::Engine
