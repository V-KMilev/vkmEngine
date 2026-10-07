#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <string>
#include <sstream>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "logger.h"

#include "gl_error_handle.h"
#include "gl_shader.h"
#include "gl_shader_preprocess.h"
#include "gl_shader_reload.h"
#include "gl_texture.h"

#include "gl_frame_context.h"
#include "gl_pass.h"
#include "gl_profiler.h"
#include "asset/gl_asset_texture.h"
#include "asset/gl_material.h"
#include "pass/gl_shadow_pass.h"
#include "pass/gl_depth_prepass.h"
#include "pass/gl_resolve_pass.h"
#include "pass/gl_cluster_pass.h"
#include "pass/gl_fog_pass.h"
#include "pass/gl_reflection_pass.h"
#include "pass/gl_dof_pass.h"
#include "pass/gl_decal_pass.h"
#include "pass/gl_particle_pass.h"
#include "pass/gl_gtao_pass.h"
#include "pass/gl_forward_pass.h"
#include "pass/gl_atmosphere_pass.h"
#include "pass/gl_skybox_pass.h"
#include "pass/gl_bloom_pass.h"
#include "pass/gl_grid_pass.h"
#include "pass/gl_composite_pass.h"
#include "pass/gl_splash_pass.h"
#include "pass/gl_ui_pass.h"
#include "storage/gl_probe_array.h"
#include "core/fnv1a.h"
#include "ecs/environment.h"
#include "ecs/component/render/light.h"
#include "platform/window/window_manager.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/generate/mesh_generators.h"
#include "system/render/render_view.h"
#include "system/render/data/light_data.h"
#include "system/sky/atmosphere.h"

namespace Vkm::Engine {

namespace {

// Frames a changed irradiance volume holds still before it bakes: a bake is hundreds of
// milliseconds, and a dragged box changes every frame.
constexpr uint32_t VOLUME_SETTLE_FRAMES = 8;

} // namespace

std::string GLBackend::shaderConstants() {
    std::ostringstream out;

    out << "const int   MAX_LIGHTS              = " << Config::MAX_LIGHTS              << ";\n"
        << "const int   MAX_SHADOW_CASTERS_2D   = " << Config::MAX_SHADOW_CASTERS_2D   << ";\n"
        << "const int   MAX_SHADOW_CASTERS_CUBE = " << Config::MAX_SHADOW_CASTERS_CUBE << ";\n"
        << "const int   CLUSTER_X               = " << Config::CLUSTER_X               << ";\n"
        << "const int   CLUSTER_Y               = " << Config::CLUSTER_Y               << ";\n"
        << "const int   CLUSTER_Z               = " << Config::CLUSTER_Z               << ";\n"
        << "const int   MAX_LIGHTS_PER_CLUSTER  = " << Config::MAX_LIGHTS_PER_CLUSTER  << ";\n"
        << "const int   NUM_CLUSTERS            = " << GLClusterGrid::NUM_CLUSTERS     << ";\n"
        << "const int   MAX_PROBES              = "
        << GLBindings::ProbeTextureSlots::MAX_PROBES << ";\n"
        // A LOD index, so one less than the mip count each cube carries.
        << "const float MAX_REFLECTION_LOD      = " << (GLIBL::PREFILTER_MIPS - 1)   << ".0;\n"
        << "const float MAX_PROBE_LOD           = " << (GLProbeArray::PREFILTER_MIPS - 1) << ".0;\n"
        << "const float UI_TEXT_MARK            = " << std::to_string(UI_TEXT_MARK) << ";\n"
        << "const vec2  TRANSMITTANCE_LUT_SIZE  = vec2("
        << GLAtmosphere::TRANSMITTANCE_WIDTH << ".0, " << GLAtmosphere::TRANSMITTANCE_HEIGHT << ".0);\n"
        << "const vec2  MULTISCATTERING_LUT_SIZE = vec2("
        << GLAtmosphere::MULTISCATTERING_SIZE << ".0, " << GLAtmosphere::MULTISCATTERING_SIZE << ".0);\n"
        << "const vec2  SKY_VIEW_LUT_SIZE       = vec2("
        << GLAtmosphere::SKY_VIEW_WIDTH << ".0, " << GLAtmosphere::SKY_VIEW_HEIGHT << ".0);\n";

    // The composite pass switches on these; see VKM_RENDER_MODES. The mask has
    // a bit set for each mode the forward pass shades as radiance.
    int      mode   = 0;
    uint32_t shaded = 0;
#define VKM_RENDER_MODE_GLSL(name, label, glsl, isShaded) \
    if (isShaded) shaded |= 1u << mode;                   \
    out << "const int   MODE_" #glsl " = " << mode++ << ";\n";
    VKM_RENDER_MODES(VKM_RENDER_MODE_GLSL)
#undef VKM_RENDER_MODE_GLSL
    out << "const int   MODE_SHADED_MASK = " << shaded << ";\n";

    // ...and the display transforms it ends on; see VKM_TONEMAPS.
    int tonemap = 0;
#define VKM_TONEMAP_GLSL(name, label, glsl) \
    out << "const int   TONEMAP_" #glsl " = " << tonemap++ << ";\n";
    VKM_TONEMAPS(VKM_TONEMAP_GLSL)
#undef VKM_TONEMAP_GLSL

    // gl_bindings.h's points and units, so a shader names rather than repeats them. Macros, not
    // consts: before GLSL 4.40 a layout qualifier takes only an integer literal.
#define VKM_GL_BINDING_GLSL(group, name, value, glsl, doc) \
    out << "#define " #glsl " " << GLBindings::group::name << "\n";
    VKM_GL_BINDINGS(VKM_GL_BINDING_GLSL)
#undef VKM_GL_BINDING_GLSL

    // Scientific, so each value is a full-precision float literal, not an int or six digits.
    std::ostringstream num;
    num << std::scientific << std::setprecision(8);
    const auto number = [&num](float value) {
        num.str({});
        num << value;
        return num.str();
    };

    // The twilight band the sky's night rises through, in the sine of the
    // elevation the shader compares.
    out << "const float SKY_TWILIGHT = "
        << number(std::sin(glm::radians(NightSkySettings::TWILIGHT_DEGREES))) << ";\n";

    // The atmosphere's fixed terms, which no authored scale changes; its scattering depends on
    // the sky's scales and arrives as uniforms (Atmosphere::coefficients).
    {
        namespace A = Atmosphere;
        const glm::vec3 ozone = A::OZONE_ABSORPTION;
        out << "const float ATMOSPHERE_PLANET_RADIUS     = " << number(A::PLANET_RADIUS) << ";\n"
            << "const float ATMOSPHERE_TOP_RADIUS        = " << number(A::TOP_RADIUS) << ";\n"
            << "const float ATMOSPHERE_EYE_ALTITUDE      = " << number(A::EYE_ALTITUDE) << ";\n"
            << "const float ATMOSPHERE_RAYLEIGH_HEIGHT   = " << number(A::RAYLEIGH_SCALE_HEIGHT) << ";\n"
            << "const float ATMOSPHERE_MIE_HEIGHT        = " << number(A::MIE_SCALE_HEIGHT) << ";\n"
            << "const float ATMOSPHERE_OZONE_ALTITUDE    = " << number(A::OZONE_ALTITUDE) << ";\n"
            << "const float ATMOSPHERE_OZONE_HALF_WIDTH  = " << number(A::OZONE_HALF_WIDTH) << ";\n"
            << "const vec3  ATMOSPHERE_OZONE_ABSORPTION  = vec3("
            << number(ozone.r) << ", " << number(ozone.g) << ", " << number(ozone.b) << ");\n"
            << "const float ATMOSPHERE_GROUND_ALBEDO     = " << number(A::GROUND_ALBEDO) << ";\n"
            << "const int   ATMOSPHERE_TRANSMITTANCE_STEPS = " << A::TRANSMITTANCE_STEPS << ";\n";
    }

    // The light and material types the shaders switch on, from the enums.
    out << "const int   LIGHT_DIRECTIONAL = " << static_cast<int>(LightType::Directional) << ";\n"
        << "const int   LIGHT_POINT       = " << static_cast<int>(LightType::Point)       << ";\n"
        << "const int   LIGHT_SPOT        = " << static_cast<int>(LightType::Spot)        << ";\n"
        << "const int   LIGHT_RECT        = " << static_cast<int>(LightType::Rect)        << ";\n"
        << "const int   LIGHT_DISK        = " << static_cast<int>(LightType::Disk)        << ";\n"
        << "const int   MAT_OPAQUE        = " << static_cast<int>(MaterialType::Opaque)      << ";\n"
        << "const int   MAT_TRANSPARENT   = " << static_cast<int>(MaterialType::Transparent) << ";\n"
        << "const int   MAT_UNLIT         = " << static_cast<int>(MaterialType::Unlit)       << ";\n"
        << "const int   MAT_ALPHA_MASK    = " << static_cast<int>(MaterialType::AlphaMask)   << ";\n";
    static_assert(static_cast<int>(LightType::Count) == 5, "A light type the shaders are not told about");
    static_assert(
        static_cast<int>(MaterialType::Count) == 4,
        "A material type the shaders are not told about"
    );

    return out.str();
}

GLBackend::ConstantsInstalled::ConstantsInstalled() {
    Vkm::GL::setShaderPrelude(OPENGL_GLSL_VERSION, shaderConstants());
}

GLBackend::GLBackend()
    : m_unitCube(m_view.meshPool(), generateCube())
    , m_sceneCapture(m_unitCube, m_objects)
    , m_cubeConvolver(m_unitCube)
    , m_iblBaker(m_cubeConvolver)
    , m_irradianceBaker(m_sceneCapture)
    , m_preview(m_sceneCapture) {}

GLBackend::~GLBackend() = default;

bool GLBackend::init(WindowManager& window) {
    // GLEW and the context come with the window, already current. Presentation stays in the
    // engine loop, so this backend never swaps.
    PROFILE_GPU_CONTEXT();

    // Every unit gl_bindings.h numbers must exist in each stage that samples
    // one, or a sampler past the last binds to nothing and reads black.
    GLint fragmentUnits = 0;
    GLint computeUnits  = 0;
    VKM_GL_CHECK(glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &fragmentUnits));
    VKM_GL_CHECK(glGetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, &computeUnits));
    const GLint units = std::min(fragmentUnits, computeUnits);
    if (units <= static_cast<GLint>(GLBindings::MAX_TEXTURE_UNIT)) {
        LOG_ERROR(
            "This GPU samples %d texture units a stage; the renderer binds up to %u",
            units,
            GLBindings::MAX_TEXTURE_UNIT + 1
        );
        return false;
    }

    m_context.setDefaultState();

    // Uploads are tightly packed in a file's own channel count, so a row need not be a multiple
    // of four bytes; at GL's default alignment such a texture uploads sheared and overreads.
    VKM_GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));

    // A linear read near a face edge filters into the next face; without it a cube shows seams.
    VKM_GL_CHECK(glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS));

    // Passes compile shaders, so this runs after the context exists. The order is load-bearing:
    // see docs/reference/rendering.md, "The passes (fixed order)".
    m_passes.push_back({"Shadow",         std::make_unique<GLShadowPass>()});
    m_passes.push_back({"DepthPrepass",   std::make_unique<GLDepthPrepass>()});
    m_passes.push_back({"ResolveDepth",   std::make_unique<GLResolvePass>(GLResolvePass::Scope::Geometry)});
    m_passes.push_back({"GTAO",           std::make_unique<GLGTAOPass>()});
    m_passes.push_back({"ClusterCull",    std::make_unique<GLClusterPass>()});
    m_passes.push_back({"FogCompute",     std::make_unique<GLFogPass>()});
    m_passes.push_back({"Atmosphere",     std::make_unique<GLAtmospherePass>()});
    m_passes.push_back({"Skybox",         std::make_unique<GLSkyboxPass>()});
    m_passes.push_back({"Forward",        std::make_unique<GLForwardPass>()});
    m_passes.push_back({"Particles",      std::make_unique<GLParticlePass>()});
    m_passes.push_back({"ResolveColor",   std::make_unique<GLResolvePass>(GLResolvePass::Scope::Color)});
    m_passes.push_back({"Reflections",    std::make_unique<GLReflectionPass>()});
    m_passes.push_back({"Decals",         std::make_unique<GLDecalPass>()});
    m_passes.push_back({"DoF",            std::make_unique<GLDoFPass>()});
    m_passes.push_back({"Bloom",          std::make_unique<GLBloomPass>()});
    m_passes.push_back({"Composite",      std::make_unique<GLCompositePass>()});
    m_passes.push_back({"Grid",           std::make_unique<GLGridPass>()});
    m_passes.push_back({"UI",             std::make_unique<GLUIPass>()});
    m_passes.push_back({"Splash",         std::make_unique<GLSplashPass>()});

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
    // A minimised window or collapsed viewport has no pixels; stopping here spares every pass.
    if (view.viewportWidth == 0 || view.viewportHeight == 0) return;

    PROFILE_SCOPE("GLBackend::render");
    PROFILE_GPU_SCOPE("GPU.Frame");

    // Before anything reads a cache: what was built from a replaced world or asset graph cannot
    // be trusted, and nothing downstream can tell.
    onWorldReplaced(view, resources);

    // A material preview lights and grades as this frame does.
    const LightData* key = lowestSlotDirectional(view.lights);
    m_previewScene.sun          = key ? std::optional<LightData>(*key) : std::nullopt;
    m_previewScene.skyIntensity = view.environment.sky.intensity;
    m_previewScene.tonemap      = view.settings.tonemap;
    m_previewScene.exposure     = view.settings.exposure;

    {
        PROFILE_SCOPE("Render/SyncAssets");
        m_view.sync(view, resources);
        m_view.setTextureFiltering(
            view.settings.textureFiltering,
            static_cast<float>(view.settings.textureAnisotropy)
        );
    }
    // The reflection inputs exist only while the reflection pass reads them.
    const bool reflect = view.settings.ssr;
    m_sceneHDR.resize(view.viewportWidth, view.viewportHeight, 1, reflect);
    m_postA.resize(view.viewportWidth, view.viewportHeight);
    m_postB.resize(view.viewportWidth, view.viewportHeight);
    m_bloom.resize(view.viewportWidth, view.viewportHeight);

    // The multisample twin exists only while MSAA is on; at 4x it is the frame's largest
    // allocation. What draws where: docs/reference/rendering.md.
    const uint32_t samples = view.settings.msaaSamples;
    if (samples > 1) {
        m_sceneMS.resize(view.viewportWidth, view.viewportHeight, samples, reflect);
    } else {
        m_sceneMS.release();
    }
    GLTarget& sceneRender = (samples > 1) ? m_sceneMS : m_sceneHDR;

    // From the Environment's authored angles, which the key light is aimed from too (see
    // SkySystem), so sky and shadows agree.
    const glm::vec3 sunDir = view.environment.sunDirection();

    // The atmosphere's air tables first: what follows reads them.
    std::optional<SkyParams> sky;
    if (view.environment.sky.procedural) {
        sky = skyParams(view.environment, sunDir);
        if (m_bakedAir.changed(sky->air)) m_iblBaker.bakeAir(m_context, m_atmosphere, sky->air);
        followProceduralSky(*sky);
    } else if (view.environment.sky.hdrPath.empty()) {
        // No image-based lighting wanted, not a failed load: the last sky goes, without an error.
        if (m_ibl.isReady()) clearEnvironment();
    } else if (view.environment.sky.hdrPath != m_bakedEnvPath) {
        bakeEnvironment(view.environment.sky.hdrPath);
    }

    // Before the shadow plan, so both agree on the tile size.
    {
        PROFILE_SCOPE("Render/ShadowAtlasInit");
        m_shadowAtlas.init(m_context, view.settings.shadowResolution);
    }

    // Shadows first: the plan gives each light the atlas slot the lights SSBO carries (spot.w).
    {
        PROFILE_SCOPE("Shadow/Plan");
        m_shadowData.build(view, m_view, m_shadowAtlas.tileResolution());
        // Latched, so a plan the atlas refuses says so once.
        const bool laidOut = m_shadowAtlas.layout(m_shadowData.tileSizes());
        if (!laidOut && !m_layoutRefusedLogged) {
            LOG_ERROR("Shadow tiles did not fit the atlas; shadows keep last frame's places");
        }
        m_layoutRefusedLogged = !laidOut;
        m_shadowData.placeTiles(m_shadowAtlas);
    }

    // Per-frame UBOs: uploaded and bound once here, visible to every pass.
    {
        PROFILE_SCOPE("Render/FrameUBOs");
        const float width  = static_cast<float>(view.viewportWidth);
        const float height = static_cast<float>(view.viewportHeight);
        m_camera.update(view.camera, glm::vec2(width, height));
        m_lights.update(view.lights, m_shadowData.lightSlots());
        m_shadowData.uploadAndBind();
    }

    // Bucket the drawables once, for every pass that reads a bucket.
    {
        PROFILE_SCOPE("Render/Partition");
        partitionDrawables(view);
    }

    // Before the object upload and the batch, which both ask whether the frame posed anything.
    {
        PROFILE_SCOPE("Render/SkinPalette");
        m_skinPalette.update(view.skinMatrices);
    }

    // Every object's transform, once: an index list names objects in it.
    {
        PROFILE_SCOPE("Render/Objects");
        m_objects.upload(*view.objects, m_skinPalette.count() > 0);
    }

    // ...and batch the opaque bucket once too (see GLFrameContext::opaqueBatch).
    {
        PROFILE_SCOPE("Render/OpaqueBatch");
        m_opaqueBatcher.buildGrouped(m_opaque, *view.objects, m_view, m_skinPalette.count());
    }

    // Each pass binds and clears its own target.
    GLFrameContext ctx{
        view,
        m_view,
        m_context,
        m_screenTri,
        m_unitCube,
        m_sceneHDR,
        sceneRender,
        m_shadowAtlas,
        m_shadowData,
        m_ibl,
        m_atmosphere,
        m_bloom,
        m_ao,
        m_clusterGrid,
        m_fog,
        m_irradiance,
        m_skinPalette,
        m_objects,
        m_alphaMask,
        m_transparent,
        m_opaqueBatcher
    };

    ctx.sunDir = sunDir;
    ctx.sky    = sky ? &*sky : nullptr;

    // Post colour chain: the scene starts on the geometry target; the first
    // post pass moves it into a scratch and the chain ping-pongs from there.
    ctx.scratchA = &m_postA;
    ctx.scratchB = &m_postB;
    ctx.colorSrc = &m_sceneHDR;
    ctx.colorDst = &m_postA;

    // No camera gathers no probes, which is not the scene losing them: their bakes stay.
    ctx.probeCount = view.hasCamera ? m_probes.bind(view) : 0;

    // The plan forked the caster cull onto the pool, and everything since was
    // built while it ran. The shadow pass is first, and it reads the batches.
    {
        PROFILE_SCOPE("Shadow/Join");
        m_shadowData.finishCull();
    }

    for (const auto& entry : m_passes) {
        PROFILE_SCOPE_NAMED(entry.name);
        PROFILE_GPU_SCOPE_NAMED(entry.name);

        // Every pass starts from the same state, so none puts back what the last changed.
        // Context caches these, so an agreeing pass pays nothing.
        m_context.setDepthTest(true);
        m_context.setDepthWrite(true);
        m_context.setDepthFunc(GL_LEQUAL);
        m_context.setBlending(false);
        m_context.setFaceCulling(false);

        entry.pass->execute(ctx);
    }

    PROFILE_PLOT("Render/Drawables",   static_cast<int64_t>(view.objects->visible.size()));
    PROFILE_PLOT("Render/Opaque",      static_cast<int64_t>(m_opaque.size()));
    PROFILE_PLOT("Render/AlphaMask",   static_cast<int64_t>(m_alphaMask.size()));
    PROFILE_PLOT("Render/Transparent", static_cast<int64_t>(m_transparent.size()));
    PROFILE_PLOT("Render/Lights",      static_cast<int64_t>(view.lights.size()));
    PROFILE_PLOT("Render/SceneMeshes", static_cast<int64_t>(view.objects->scene.size()));
    PROFILE_PLOT(
        "Render/Particles",
        static_cast<int64_t>(view.particlesAdditive.size() + view.particlesAlpha.size())
    );
    PROFILE_PLOT("Render/Probes",      static_cast<int64_t>(ctx.probeCount));
    PROFILE_PLOT("Render/SkinBones",   static_cast<int64_t>(m_skinPalette.count()));

    // A frame with no camera gathered nothing to bake from.
    if (view.hasCamera) bakeCaptures(view, resources);

    PROFILE_GPU_COLLECT();
}

void GLBackend::bakeCaptures(const RenderView& view, const ResourceManager& resources) {
    // Re-bake the irradiance volume when its box, grid or version changed. The bake binds its
    // own camera, light and shadow blocks (see GLSceneCapture).
    if (view.hasIrradianceVolume) {
        const IrradianceVolumeData& iv = view.irradianceVolume;
        const IrradianceSignature want{
            iv.center,
            iv.halfExtents,
            iv.resolutionX,
            iv.resolutionY,
            iv.resolutionZ,
            iv.bakeVersion
        };
        // A box being dragged changes every frame: until it holds still, the last bake stands. A
        // new bake version is a request to bake now.
        const bool asked = want.bakeVersion != m_irradianceRequest.bakeVersion;
        if (want == m_irradianceRequest) {
            ++m_irradianceStill;
        } else {
            m_irradianceRequest = want;
            m_irradianceStill   = 0;
        }
        const bool settled = asked || m_irradianceStill >= VOLUME_SETTLE_FRAMES || !m_bakedIrradiance.baked();
        if (settled && m_bakedIrradiance.changed(want)) {
            m_irradiance.resize(iv.resolutionX, iv.resolutionY, iv.resolutionZ);
            m_irradianceBaker.bake(m_context, m_irradiance, iv, view, m_view, resources, m_ibl);
        }
    }

    // Re-bake probes that are new, moved, resized, version-bumped or lit by an older volume, after
    // the volume, which lights what they capture of it.
    const bool lit = view.hasIrradianceVolume && m_irradiance.isReady();
    m_probes.update(m_context, view, m_view, resources, m_ibl, lit ? &m_irradiance : nullptr);
}

void GLBackend::bakeEnvironment(const std::string& path) {
    PROFILE_SCOPE("Render/IBLBake");
    PROFILE_GPU_SCOPE("Render/IBLBake");

    // A load failure leaves m_ibl not-ready.
    m_iblBaker.bake(m_context, m_ibl, path);
    m_bakedEnvPath = path;
    m_shownSky.reset();
}

void GLBackend::clearEnvironment() {
    m_ibl.markUnready();
    m_bakedEnvPath.clear();
    m_shownSky.reset();
}

SkyParams GLBackend::skyParams(const Environment& env, const glm::vec3& sunDir) {
    SkyParams sky;
    sky.sunDir         = sunDir;
    sky.sunIlluminance = Atmosphere::solarIlluminance(env.sky);
    sky.air            = Atmosphere::coefficients(env.sky);
    sky.mieG           = env.sky.mieG;
    sky.nightRadiance  = env.night.radiance;
    sky.moonDir        = env.moonDirection();
    // A fraction of the disc: the halo tracks the moon's brightness, so one dial drives both.
    sky.moonHalo       = env.night.moonIntensity * 0.15f;
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
        m_bakedEnvPath.clear();  // force an HDR re-bake if the user switches back
        return;
    }

    PROFILE_SCOPE("Render/IBLStep");
    PROFILE_GPU_SCOPE("Render/IBLStep");
    if (!m_iblBaker.baking()) m_iblBaker.beginProcedural(sky);
    if (m_iblBaker.advance(m_context, m_ibl, m_atmosphere)) m_shownSky = m_iblBaker.target();
}

void GLBackend::onWorldReplaced(const RenderView& view, const ResourceManager& resources) {
    const uint64_t assetEpoch = resources.epoch();
    const uint64_t worldEpoch = view.worldEpoch;
    if (assetEpoch == m_assetEpoch && worldEpoch == m_worldEpoch) return;

    PROFILE_SCOPE("Render/WorldReplaced");

    if (assetEpoch != m_assetEpoch) {
        m_assetEpoch = assetEpoch;
        m_view.invalidate();      // asset mirrors: handles and versions restart
        LOG_INFO("Asset graph swapped; asset mirrors dropped");
    }
    if (worldEpoch != m_worldEpoch) {
        m_worldEpoch = worldEpoch;
        m_probes.invalidate();    // cube captures: same probe pose, different scene
        m_bakedIrradiance.invalidate();  // SH volume: same box and grid, different scene
        LOG_INFO("Scene replaced; baked captures of it dropped");
    }
    // Held shadow tiles: pictures of both the world and the meshes it is made of.
    m_shadowAtlas.forgetHeld();
}

void GLBackend::partitionDrawables(const RenderView& view) {
    m_opaque.clear();
    m_alphaMask.clear();
    m_transparent.clear();
    const RenderObjects& objects = *view.objects;
    m_opaque.reserve(objects.visible.size());

    for (const uint32_t object : objects.visible) {
        const GLMaterial* material = m_view.getMaterial(objects.draws[object].material);
        const MaterialType type = material ? material->getType() : MaterialType::Opaque;
        if (type == MaterialType::Transparent)    m_transparent.push_back(object);
        else if (type == MaterialType::AlphaMask) m_alphaMask.push_back(object);
        else                                      m_opaque.push_back(object);
    }
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
    // The preview binds its own camera and lights; the next frame binds the frame's again.
    return m_preview.render(m_context, m_view, m_ibl, m_previewScene, m_shadowAtlas, request, resources);
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
    const Vkm::GL::Texture2D* tex = m_view.getTexture(handle);
    return tex ? tex->getID() : 0;
}

GpuTextureId GLBackend::ensureTexture(const TextureHandle& handle, const ResourceManager& resources) {
    m_view.ensureTexture(handle, resources);
    return textureId(handle);
}

GpuTextureId GLBackend::chromeImage(const std::string& path) {
    if (const auto it = m_chromeImages.find(path); it != m_chromeImages.end()) {
        return it->second ? static_cast<GpuTextureId>(it->second->getID()) : 0;
    }

    // Cached on failure too, so an undecodable path is not re-read every frame.
    std::unique_ptr<Vkm::GL::Texture2D>& slot = m_chromeImages[path];
    slot = uploadImageFile(path);
    if (!slot) {
        LOG_ERROR("Chrome image '%s' could not be decoded", path.c_str());
        return 0;
    }
    return static_cast<GpuTextureId>(slot->getID());
}

uint32_t GLBackend::reloadChangedShaders() {
    // A Vkm::GL::Shader registers itself, so this reaches a program without its
    // owner opting in.
    const uint32_t reloaded = Vkm::GL::reloadChangedShaders("shaders");
    // Held shadow tiles were drawn by old programs, and which reloaded is not told (an include
    // reaches several), so any reload forgets them all.
    if (reloaded > 0) m_shadowAtlas.forgetHeld();
    return reloaded;
}

uint32_t GLBackend::maxAnisotropy() const {
    return static_cast<uint32_t>(Vkm::GL::maxSupportedAnisotropy());
}

bool GLBackend::holdsPixels(const TextureHandle& texture, uint64_t version) const {
    return m_view.holdsPixels(texture, version);
}

bool GLBackend::readFrame(const RenderView& view, std::vector<uint8_t>& pixels) {
    pixels.clear();
    const GLsizei width  = static_cast<GLsizei>(view.viewportWidth);
    const GLsizei height = static_cast<GLsizei>(view.viewportHeight);
    if (width <= 0 || height <= 0) return false;

    // The default framebuffer's own read buffer, unchanged: a window's back buffer, or a
    // pbuffer's only one. The read binding and pack alignment are put back.
    GLint readBinding   = 0;
    GLint packAlignment = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readBinding);
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);

    const GLint glX = static_cast<GLint>(view.viewportX);
    const GLint glY = static_cast<GLint>(view.surfaceHeight) - static_cast<GLint>(view.viewportY) - height;
    std::vector<uint8_t> rows(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(glX, glY, width, height, GL_RGB, GL_UNSIGNED_BYTE, rows.data());
    glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readBinding));

    // GL reads bottom row first.
    const size_t stride = static_cast<size_t>(width) * 3;
    pixels.resize(rows.size());
    for (GLsizei y = 0; y < height; ++y) {
        std::copy_n(
            rows.data() + static_cast<size_t>(height - 1 - y) * stride,
            stride,
            pixels.data() + static_cast<size_t>(y) * stride
        );
    }
    return true;
}

} // namespace Vkm::Engine
