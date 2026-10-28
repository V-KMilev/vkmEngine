#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_backend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <string>
#include <sstream>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "l_assert.h"
#include "logger.h"

#include "gl_shader.h"
#include "gl_shader_preprocess.h"
#include "gl_shader_reload.h"
#include "gl_texture.h"

#include "gl_frame_context.h"
#include "gl_pass.h"
#include "gl_profiler.h"
#include "gl_window_surface.h"
#include "asset/gl_asset_texture.h"
#include "asset/gl_material.h"
#include "convention/gl_depth.h"
#include "offline/gl_capture_lighting.h"
#include "pass/gl_shadow_pass.h"
#include "pass/gl_depth_prepass.h"
#include "pass/gl_resolve_pass.h"
#include "pass/gl_cluster_pass.h"
#include "pass/gl_fog_pass.h"
#include "pass/gl_planar_reflection_pass.h"
#include "pass/gl_reflection_pass.h"
#include "pass/gl_debug_draw_pass.h"
#include "pass/gl_dof_pass.h"
#include "pass/gl_decal_pass.h"
#include "pass/gl_particle_pass.h"
#include "pass/gl_gtao_pass.h"
#include "pass/gl_screen_shadow_pass.h"
#include "pass/gl_forward_pass.h"
#include "pass/gl_ocean_pass.h"
#include "pass/gl_atmosphere_pass.h"
#include "pass/gl_skybox_pass.h"
#include "pass/gl_bloom_pass.h"
#include "pass/gl_grid_pass.h"
#include "pass/gl_composite_pass.h"
#include "pass/gl_loading_bar_pass.h"
#include "pass/gl_loading_pass.h"
#include "pass/gl_splash_pass.h"
#include "pass/gl_ui_pass.h"
#include "storage/gl_ocean_fields.h"
#include "storage/gl_probe_array.h"
#include "core/fnv1a.h"
#include "ecs/environment.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/particle_emitter.h"
#include "platform/window/window_manager.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/generate/mesh_generators.h"
#include "system/render/render_view.h"
#include "system/render/data/light_data.h"
#include "system/sky/atmosphere.h"

namespace Vkm::Engine {

std::unique_ptr<WindowSurface> GLBackend::windowSurface() {
    return std::make_unique<GLWindowSurface>();
}

std::string GLBackend::shaderConstants() {
    using CullSet = GLOcclusionCull::Set;
    std::ostringstream out;

    out << "const int   MAX_LIGHTS              = " << Config::MAX_LIGHTS              << ";\n"
        << "const int   MAX_SHADOW_CASTERS_2D   = " << Config::MAX_SHADOW_CASTERS_2D   << ";\n"
        << "const int   MAX_SHADOW_CASTERS_CUBE = " << Config::MAX_SHADOW_CASTERS_CUBE << ";\n"
        << "const int   CLUSTER_X               = " << Config::CLUSTER_X               << ";\n"
        << "const int   CLUSTER_Y               = " << Config::CLUSTER_Y               << ";\n"
        << "const int   CLUSTER_Z               = " << Config::CLUSTER_Z               << ";\n"
        << "const int   MAX_LIGHTS_PER_CLUSTER  = " << Config::MAX_LIGHTS_PER_CLUSTER  << ";\n"
        << "const int   NUM_CLUSTERS            = " << GLClusterGrid::NUM_CLUSTERS     << ";\n"
        << "const int   MAX_OCEAN_SURGES        = " << Ocean::MAX_SURGES << ";\n"
        << "const int   SWELL_FIELD             = " << SWELL_FIELD << ";\n"
        << "const int   MAX_SWELL_CASCADES      = " << MAX_SWELL_CASCADES << ";\n"
        << "const int   OCEAN_RIPPLE_FIELDS     = " << RIPPLE_FIELDS << ";\n"
        << "const int   OCEAN_INVERSE_STEPS     = " << OceanSurface::INVERSE_STEPS << ";\n"
        << "const float OCEAN_GRAVITY           = " << std::to_string(OceanSpectrum::GRAVITY) << ";\n"
        << "const float OCEAN_SURGE_WHOLE       = " << std::to_string(OceanSurface::SURGE_WHOLE) << ";\n"
        << "const float JONSWAP_GAMMA           = " << std::to_string(JONSWAP_GAMMA) << ";\n"
        << "const float JONSWAP_SIGMA_BELOW     = " << std::to_string(JONSWAP_SIGMA_BELOW) << ";\n"
        << "const float JONSWAP_SIGMA_ABOVE     = " << std::to_string(JONSWAP_SIGMA_ABOVE) << ";\n"
        << "const int   OCEAN_FIELD             = " << RIPPLE_FIELD_SIZE << ";\n"
        << "const int   EXPOSURE_BINS           = " << Exposure::BINS << ";\n"
        << "const int   EXPOSURE_STRIDE         = " << Exposure::STRIDE << ";\n"
        << "const float EXPOSURE_MIN_LOG        = " << std::to_string(Exposure::MIN_LOG) << ";\n"
        << "const float EXPOSURE_MAX_LOG        = " << std::to_string(Exposure::MAX_LOG) << ";\n"
        << "const int   MAX_PROBES              = "
        << GLBindings::ProbeTextureSlots::MAX_PROBES << ";\n"
        << "const int   MAX_PLANAR_REFLECTORS   = " << GLPlanarReflectionPass::MAX_DRAWN << ";\n"
        << "const float PLANAR_BEND             = "
        << std::to_string(GLPlanarReflectionPass::PICTURE_BEND) << ";\n"
        // A LOD index, so one less than the mip count each cube carries.
        << "const float MAX_REFLECTION_LOD      = " << (GLIBL::PREFILTER_MIPS - 1)   << ".0;\n"
        << "const float MAX_PROBE_LOD           = " << (GLProbeArray::PREFILTER_MIPS - 1) << ".0;\n"
        << "const float UI_TEXT_MARK            = " << std::to_string(UI_TEXT_MARK) << ";\n"
        << "const uint  INSTANCE_FLAG_VIEWMODEL = " << INSTANCE_FLAG_VIEWMODEL << "u;\n"
        << "const int   CULL_SET_SEEN           = " << static_cast<int>(CullSet::Seen) << ";\n"
        << "const int   CULL_SET_REVEALED       = " << static_cast<int>(CullSet::Revealed) << ";\n"
        << "const int   CULL_SET_SHOWN          = " << static_cast<int>(CullSet::Shown) << ";\n"
        << "const float VIEWMODEL_DEPTH_SHARE   = " << std::to_string(VIEWMODEL_DEPTH_SHARE) << ";\n"
        << "const float NEAR_DEPTH              = " << std::to_string(GLDepth::EYE_NEAR) << ";\n"
        << "const float FAR_DEPTH               = " << std::to_string(GLDepth::EYE_CLEAR) << ";\n"
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

    // The light and material types and particle blends the shaders switch on, from the enums.
    using LightmapKind = GLLightmapBaker::LightKind;
    out << "const int   LIGHT_DIRECTIONAL   = " << static_cast<int>(LightType::Directional)    << ";\n"
        << "const int   LIGHT_POINT         = " << static_cast<int>(LightType::Point)          << ";\n"
        << "const int   LIGHT_SPOT          = " << static_cast<int>(LightType::Spot)           << ";\n"
        << "const int   LIGHT_RECT          = " << static_cast<int>(LightType::Rect)           << ";\n"
        << "const int   LIGHT_DISK          = " << static_cast<int>(LightType::Disk)           << ";\n"
        << "const int   MAT_OPAQUE          = " << static_cast<int>(MaterialType::Opaque)      << ";\n"
        << "const int   MAT_TRANSPARENT     = " << static_cast<int>(MaterialType::Transparent) << ";\n"
        << "const int   MAT_UNLIT           = " << static_cast<int>(MaterialType::Unlit)       << ";\n"
        << "const int   MAT_ALPHA_MASK      = " << static_cast<int>(MaterialType::AlphaMask)   << ";\n"
        << "const int   MATERIAL_MAX_LAYERS = " << MaterialAsset::MAX_LAYERS                  << ";\n"
        << "const int   LAYER_COPY_FILL     = " << static_cast<int>(GLLayerMaps::Copy::Fill)   << ";\n"
        << "const int   LAYER_COPY_ALBEDO   = " << static_cast<int>(GLLayerMaps::Copy::Albedo) << ";\n"
        << "const int   LAYER_COPY_NORMAL   = " << static_cast<int>(GLLayerMaps::Copy::Normal) << ";\n"
        << "const int   LAYER_COPY_AO_ROUGHNESS_METALLIC = "
        << static_cast<int>(GLLayerMaps::Copy::AoRoughnessMetallic) << ";\n"
        << "const int   LAYER_COPY_ROUGHNESS_METALLIC = "
        << static_cast<int>(GLLayerMaps::Copy::RoughnessMetallic) << ";\n"
        << "const int   PARTICLE_ADDITIVE   = " << static_cast<int>(ParticleBlend::Additive)   << ";\n"
        << "const int   PARTICLE_ALPHA      = " << static_cast<int>(ParticleBlend::Alpha)      << ";\n"
        << "const int   PARTICLE_DISTORTION = " << static_cast<int>(ParticleBlend::Distortion) << ";\n"
        << "const uint  DEBUG_GHOSTED       = " << static_cast<int>(DebugDepth::Ghosted)       << "u;\n"
        << "const uint  DEBUG_HIDDEN        = " << static_cast<int>(DebugDepth::Hidden)        << "u;\n"
        << "const uint  DEBUG_ON_TOP        = " << static_cast<int>(DebugDepth::OnTop)         << "u;\n"
        << "const int   LIGHTMAP_KIND_SUN   = " << static_cast<int>(LightmapKind::Sun)   << ";\n"
        << "const int   LIGHTMAP_KIND_SPOT  = " << static_cast<int>(LightmapKind::Spot)  << ";\n"
        << "const int   LIGHTMAP_KIND_POINT = " << static_cast<int>(LightmapKind::Point) << ";\n"
        << "const int   RELIGHT_DIRECTIONAL_HEAD = " << GLIrradianceBaker::RELIGHT_DIRECTIONAL_HEAD << ";\n"
        << "const int   RELIGHT_OUTSIDE_HEAD     = " << GLIrradianceBaker::RELIGHT_OUTSIDE_HEAD << ";\n"
        << "const int   RELIGHT_CELL_HEADS       = " << GLIrradianceBaker::RELIGHT_CELL_HEADS << ";\n";
    static_assert(static_cast<int>(LightType::Count) == 5, "A light type the shaders are not told about");
    static_assert(
        static_cast<int>(MaterialType::Count) == 4,
        "A material type the shaders are not told about"
    );
    static_assert(
        static_cast<int>(ParticleBlend::Count) == 3,
        "A particle blend the shaders are not told about"
    );
    static_assert(
        static_cast<int>(DebugDepth::Count) == 3,
        "A debug depth mode the shaders are not told about"
    );
    static_assert(
        static_cast<int>(GLLayerMaps::Copy::Count) == 5,
        "A layer copy the shaders are not told about"
    );
    static_assert(
        static_cast<int>(LightmapKind::Count) == 3,
        "A lightmap light kind the shaders are not told about"
    );

    return out.str();
}

GLBackend::ConstantsInstalled::ConstantsInstalled() {
    Vkm::GL::setShaderPrelude(OPENGL_GLSL_VERSION, shaderConstants());
}

namespace {

/// The passes init() adds; init() asserts the count.
[[maybe_unused]] constexpr size_t PASS_COUNT = 27;

/// The least time between two presents while the passes build: each waits on the display.
constexpr std::chrono::milliseconds STARTUP_PRESENT_INTERVAL{100};

/// A frame's share of checking startup programs (programsReady()), in which many quick checks fit.
/// One check longer than it says the driver builds as it checks - some do, holding every GL call
/// while they build - so checking all at once would be one long frozen frame.
constexpr std::chrono::milliseconds STARTUP_CHECK_SHARE{4};

/**
 * @brief Present black, then answer the desktop: the window is not hung while the passes build,
 *        and shows nothing before the splash does.
 *
 * @param gl     The context, whose cached state the clear goes through.
 * @param window The window shown.
 */
void presentBlack(Vkm::GL::Context& gl, WindowManager& window) {
    if (window.getWidth() == 0 || window.getHeight() == 0) return;
    Vkm::GL::FrameBuffer::bindDefault();
    gl.setViewport(0, 0, static_cast<int32_t>(window.getWidth()), static_cast<int32_t>(window.getHeight()));
    gl.setColorMask(true, true, true, true);
    gl.setClearColor(glm::vec4(0.0f));
    gl.clear(true, false, false);
    window.swapBuffers();
    window.pumpEvents();
}

} // namespace

GLBackend::GLBackend()
    : m_unitCube(m_view.meshPool(), generateCube())
    , m_sceneCapture(m_unitCube, m_objects, m_forwardPrograms)
    , m_cubeConvolver(m_unitCube)
    , m_iblBaker(m_cubeConvolver)
    , m_irradianceBaker(m_sceneCapture)
    , m_preview(m_sceneCapture) {}

GLBackend::~GLBackend() = default;

bool GLBackend::init(WindowManager& window) {
    // GLEW and the context come with the window, already current. Presentation is the engine
    // loop's, but for the black presented below while the passes build, before its first frame.
    PROFILE_GPU_CONTEXT();

    // Every unit gl_bindings.h numbers must exist in each stage that samples
    // one, or a sampler past the last binds to nothing and reads black.
    // A vertex stage samples too (shaders/particle/vertex.shader).
    const int32_t units = std::min({
        m_context.integer(GL_MAX_TEXTURE_IMAGE_UNITS),
        m_context.integer(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS),
        m_context.integer(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS),
    });
    if (units <= static_cast<int32_t>(GLBindings::MAX_TEXTURE_UNIT)) {
        LOG_ERROR(
            "This GPU samples %d texture units a stage; the renderer binds up to %u",
            units,
            GLBindings::MAX_TEXTURE_UNIT + 1
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
    // What a first frame draws - the splash, the loading screen and its bar - is built before the
    // other passes, so the driver compiles its programs ahead of theirs, though behind those this
    // backend's members built as it was made; then the first sync's layered materials, then the
    // forward programs, the slowest, as the world waits longest on them.
    auto splash  = std::make_unique<GLSplashPass>();
    auto loading = std::make_unique<GLLoadingPass>();
    auto cover   = std::make_unique<GLUIPass>(GLUIPass::List::Cover);
    auto bar     = std::make_unique<GLLoadingBarPass>();
    // And what the first frame's sync runs: it builds the layered materials before any pass draws.
    m_view.buildPrograms();
    m_forwardPrograms.build();
    add({"Shadow",         std::make_unique<GLShadowPass>()});
    add({"DepthPrepass",   std::make_unique<GLDepthPrepass>()});
    add({"ResolveDepth",   std::make_unique<GLResolvePass>(GLResolvePass::Scope::Geometry)});
    add({"GTAO",           std::make_unique<GLGTAOPass>()});
    add({"ScreenShadows",  std::make_unique<GLScreenShadowPass>()});
    auto lightCull = std::make_unique<GLClusterPass>();
    GLClusterPass& clusterCull = *lightCull;
    add({"ClusterCull",    std::move(lightCull)});
    add({"FogCompute",     std::make_unique<GLFogPass>()});
    add({"Atmosphere",     std::make_unique<GLAtmospherePass>()});
    // Before the mirrors, so the water a mirror shows has this frame's ripples and shore.
    add({"Ocean",          std::make_unique<GLOceanPass>()});
    auto particles = std::make_unique<GLParticlePass>();
    auto planar    = std::make_unique<GLPlanarReflectionPass>(
        m_sceneCapture,
        m_forwardPrograms,
        m_camera,
        *particles,
        clusterCull
    );
    m_planar = planar.get();
    add({"PlanarReflections", std::move(planar)});
    add({"Skybox",         std::make_unique<GLSkyboxPass>()});
    auto forward = std::make_unique<GLForwardPass>(m_forwardPrograms);
    add({"Forward",        std::move(forward)});
    add({"Particles",      std::move(particles)});
    add({"ResolveColor",   std::make_unique<GLResolvePass>(GLResolvePass::Scope::Color)});
    add({"Reflections",    std::make_unique<GLReflectionPass>()});
    add({"Decals",         std::make_unique<GLDecalPass>()});
    add({"DoF",            std::make_unique<GLDoFPass>()});
    add({"DebugGlow",      std::make_unique<GLDebugDrawPass>(GLDebugDrawPass::Stage::Glow)});
    add({"Bloom",          std::make_unique<GLBloomPass>()});
    add({"Composite",      std::make_unique<GLCompositePass>()});
    add({"Grid",           std::make_unique<GLGridPass>()});
    add({"DebugLines",     std::make_unique<GLDebugDrawPass>(GLDebugDrawPass::Stage::Lines)});
    add({"UI",             std::make_unique<GLUIPass>()});
    m_chrome = {splash.get(), loading.get(), cover.get(), bar.get()};
    add({"Splash",         std::move(splash)});
    add({"Loading",        std::move(loading)});
    add({"Cover",          std::move(cover)});
    add({"LoadingBar",     std::move(bar)});
    VKM_ASSERT(m_passes.size() == PASS_COUNT, "PASS_COUNT counts the passes init() adds");

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
    // A minimised window or collapsed viewport has no pixels; stopping here spares every pass. The
    // stream's frame still ends: an editor preview may have written into it.
    if (view.viewportWidth == 0 || view.viewportHeight == 0) {
        m_view.stream().endFrame();
        return;
    }

    PROFILE_SCOPE("GLBackend::render");
    PROFILE_GPU_SCOPE("GPU.Frame");

    // Before anything reads a cache: what was built from a replaced world or asset graph cannot
    // be trusted, and nothing downstream can tell.
    onWorldReplaced(view, resources);

    // A material preview lights and grades as this frame does, at the authored exposure: an adapting
    // one would re-render every thumbnail each frame.
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
    // The splash must not stall.
    if (!programsReady(view.splash.isShowing())) {
        drawChrome(view);
        m_view.stream().endFrame();
        return;
    }
    // The reflection inputs exist while the reflection pass may read them: ssr is on, or a planar
    // reflector may be drawn. A camera-less frame lists no reflectors and draws none: it keeps them.
    const bool kept    = !view.hasCamera && m_sceneHDR.hasReflectInputs();
    const bool reflect = view.settings.ssr || !view.planarReflectors.empty() || kept;
    m_sceneHDR.resize(m_context, view.sceneWidth(), view.sceneHeight(), 1, reflect);
    m_postA.resize(view.sceneWidth(), view.sceneHeight());
    m_postB.resize(view.sceneWidth(), view.sceneHeight());
    m_bloom.resize(view.sceneWidth(), view.sceneHeight());

    // The multisample twin exists only while MSAA is on; at 4x it is the frame's largest
    // allocation. What draws where: docs/reference/rendering.md.
    const uint32_t samples = view.settings.msaaSamples;
    if (samples > 1) {
        m_sceneMS.resize(m_context, view.sceneWidth(), view.sceneHeight(), samples, reflect);
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
        // On its units for what draws before a pass binds them - a bake's capture, which binds only
        // its own sun: the forward programs declare them as shadow samplers, and the driver
        // validates a declared sampler at draw time.
        m_shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
        m_shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);
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

    // Bucket the drawables once, for every pass that reads a bucket; and see whether water is drawn,
    // which the ocean's block says.
    {
        PROFILE_SCOPE("Render/Partition");
        partitionDrawables(view);
    }

    // Per-frame UBOs: uploaded and bound once here, visible to every pass.
    {
        PROFILE_SCOPE("Render/FrameUBOs");
        const float width  = static_cast<float>(view.sceneWidth());
        const float height = static_cast<float>(view.sceneHeight());
        m_camera.update(view.camera, glm::vec2(width, height));
        m_ocean.update(view.ocean, m_waterShown);
        m_lights.update(view.lights, m_shadowData.lightSlots(), false, m_lightmaps.channels(view.lights));
        m_shadowData.uploadAndBind();
    }

    // Before the object upload and the batch, which both ask whether the frame posed anything.
    {
        PROFILE_SCOPE("Render/SkinPalette");
        m_skinPalette.update(view.skinMatrices, m_view.stream());
    }

    // The GPU occlusion cull runs from a camera alone, reading every object's world box.
    const bool occlusionCulled = view.settings.occlusionCulling && view.hasCamera;

    // Every object's transform, once: an index list names objects in it.
    {
        PROFILE_SCOPE("Render/Objects");
        m_objects.upload(*view.objects, m_skinPalette.count() > 0, occlusionCulled, m_view.stream());
        m_lightmaps.prepare(*view.objects, m_view);
        m_lightmaps.bindObjects();
    }

    // ...and batch the opaque bucket once too (see GLFrameContext::opaqueBatch).
    {
        PROFILE_SCOPE("Render/OpaqueBatch");
        m_opaqueBatcher.buildGrouped(m_opaque, *view.objects, m_view, m_skinPalette.count());
    }

    // What each mirror sees, out of the scene list whose materials the sync readied.
    {
        PROFILE_SCOPE("Render/PlanarPrepare");
        m_planar->prepare(view, m_view);
    }

    // Each pass binds and clears its own target.
    GLFrameContext ctx = frameContext(view, sceneRender);

    ctx.sunDir     = sunDir;
    ctx.waterShown = m_waterShown;
    ctx.exposure   = m_exposure.stops(view.settings, view.frameSeconds);
    ctx.sky        = sky ? &*sky : nullptr;
    ctx.lightmaps  = &m_lightmaps;
    if (occlusionCulled) {
        PROFILE_SCOPE("Render/OcclusionPrepare");
        if (m_occlusion.prepare(m_opaqueBatcher, *view.objects)) {
            ctx.occlusion = &m_occlusion;
        }
    }
    m_oceanFields.beginFrame(view.ocean);
    m_oceanShore.beginFrame();

    // Post color chain: the scene starts on the geometry target; the first
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

    // Under a loading screen nothing of the world shows: the frame draws the screen alone, and the
    // bakes after it have the time the world would have taken. A held shadow tile or layer is signed
    // only when drawn (GLShadowAtlas), so a skipped frame leaves none held that was not.
    const bool covered = view.loadingOpacity >= 1.0f;
    if (covered) {
        drawChrome(view);
    } else {
        // Every pass starts from the same state, so none puts back what the last changed; and so
        // does what runs after them.
        for (const auto& entry : m_passes) {
            PROFILE_SCOPE_NAMED(entry.name);
            PROFILE_GPU_SCOPE_NAMED(entry.name);
            passState();
            entry.pass->execute(ctx);
        }
        passState();
    }

    PROFILE_PLOT("Render/Drawables",   static_cast<int64_t>(view.objects->visible.size()));
    PROFILE_PLOT("Render/Opaque",      static_cast<int64_t>(m_opaque.size()));
    PROFILE_PLOT("Render/AlphaMask",   static_cast<int64_t>(m_alphaMask.size()));
    PROFILE_PLOT("Render/Transparent", static_cast<int64_t>(m_transparent.size()));
    PROFILE_PLOT("Render/Lights",      static_cast<int64_t>(view.lights.size()));
    PROFILE_PLOT("Render/SceneMeshes", static_cast<int64_t>(view.objects->scene.size()));
    PROFILE_PLOT(
        "Render/Particles",
        static_cast<int64_t>(
            view.particlesAdditive.size() + view.particlesAlpha.size() + view.particlesDistortion.size()
        )
    );
    PROFILE_PLOT("Render/Probes",      static_cast<int64_t>(ctx.probeCount));
    PROFILE_PLOT("Render/SkinBones",   static_cast<int64_t>(m_skinPalette.count()));

    // The scene as lit, before the bloom and the tonemap, so what it measures does not depend on
    // the exposure it sets.
    if (view.settings.autoExposure && view.hasCamera && !covered) {
        PROFILE_GPU_SCOPE("Render/Exposure");
        m_exposure.measure(*ctx.colorSrc, view.sceneWidth(), view.sceneHeight());
    }
    PROFILE_PLOT("Render/Exposure", static_cast<double>(ctx.exposure));

    // A frame with no camera gathered nothing to bake from; one whose world still arrives waits for
    // it (RenderView::worldArriving).
    m_bakes     = view.hasCamera;
    m_bakesHeld = m_bakes && view.worldArriving;
    if (m_bakes && !m_bakesHeld) bakeCaptures(view);
    // The bakes draw into targets of their own and leave one bound; what draws after the frame - an
    // editor's UI - draws into the window.
    Vkm::GL::FrameBuffer::bindDefault();

    // Every command reading what the frame streamed is issued: fence its region.
    m_view.stream().endFrame();

    PROFILE_GPU_COLLECT();
}

bool GLBackend::programsReady(bool quickOnly) {
    if (m_programsReady) return true;
    // A frame's share at most; for an asker that must not stall, only while checks stay quick. A
    // program that failed throws as it is checked, at startup, not at its first use.
    const auto begun = std::chrono::steady_clock::now();
    while (!(quickOnly && m_checksSlow)) {
        const auto one = std::chrono::steady_clock::now();
        if (!Vkm::GL::checkNextShader()) break;
        const auto now = std::chrono::steady_clock::now();
        if (now - one > STARTUP_CHECK_SHARE) m_checksSlow = true;
        if (now - begun > STARTUP_CHECK_SHARE) break;
    }
    const Vkm::GL::ShaderCount count = Vkm::GL::countShaders();
    m_programsReady = count.compiling == 0 && count.unchecked == 0;
    return m_programsReady;
}

void GLBackend::drawChrome(const RenderView& view) {
    Vkm::GL::FrameBuffer::bindDefault();
    passState();
    const auto width  = static_cast<int32_t>(view.surfaceWidth);
    const auto height = static_cast<int32_t>(view.surfaceHeight);
    m_context.setViewport(0, 0, width, height);
    m_context.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    m_context.clear(true, false, false);
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
        m_oceanFields,
        m_oceanShore,
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
    const IrradianceVolumeData& box = view.irradianceVolume;
    const Math::AABB within{box.center - box.halfExtents, box.center + box.halfExtents};
    const uint64_t   lights = lightingDigest(view.lights, view.hasIrradianceVolume ? &within : nullptr);
    const CaptureLighting volumeLighting{lights, m_ibl.version(), m_ibl.isReady()};
    m_irradianceBaker.update(m_context, m_irradiance, view, m_view, m_ibl, volumeLighting);

    // The fixed lights' shadows on the fixed meshes, when either has changed and settled.
    m_lightmapBaker.update(m_context, view, m_view, m_objects, m_lightmaps);

    // Re-bake probes that are new, version-bumped, moved or resized and holding still, or lit by
    // an older volume, after the volume, which lights what they capture of it; refresh those
    // under other light; none while the volume is part-way through its captures. Probes turned
    // off are neither: nothing reads them, and the ones gone stale meanwhile refresh in turn
    // once they are back.
    m_bakesProbes = view.settings.probes;
    if (!m_bakesProbes || m_irradianceBaker.baking()) return;
    const bool                lit    = view.hasIrradianceVolume && m_irradiance.isReady();
    const GLIrradianceVolume* volume = lit ? &m_irradiance : nullptr;
    // Its lights are each probe's own, those that reach it (GLProbeManager::update).
    CaptureLighting probeLighting;
    probeLighting.sky             = m_ibl.version();
    probeLighting.skyReady        = m_ibl.isReady();
    probeLighting.volumeLight     = volume ? volume->lightId() : 0u;
    probeLighting.volumeIntensity = volume ? view.irradianceVolume.intensity : 0.0f;
    m_probes.update(m_context, view, m_view, m_ibl, volume, probeLighting);
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
        m_view.invalidate();      // asset mirrors: handles and versions restart
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
    // Held shadow tiles: pictures of both the world and the meshes it is made of.
    m_shadowAtlas.forgetHeld();
}

void GLBackend::partitionDrawables(const RenderView& view) {
    m_opaque.clear();
    m_alphaMask.clear();
    m_transparent.clear();
    m_waterShown = false;
    const RenderObjects& objects = *view.objects;
    m_opaque.reserve(objects.visible.size());

    for (const uint32_t object : objects.visible) {
        const GLMaterial* material = m_view.getMaterial(objects.draws[object].material);
        const MaterialType type = material ? material->getType() : MaterialType::Opaque;
        m_waterShown = m_waterShown || (material && material->water());
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
    // Drawn with the forward programs, which would hold the window until checked: asked again later.
    // Not the frame's to stall for, so only quick checks.
    if (!programsReady(true)) return {};
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

RenderReadiness GLBackend::readiness() const {
    RenderReadiness readiness;
    if (!m_programsReady) {
        const Vkm::GL::ShaderCount count = Vkm::GL::countShaders();
        readiness.programs = count.live;
        readiness.compiled = count.live - count.unchecked;
    }
    // Only what the frames go on to bake is owed: with no camera they bake nothing. Held while its
    // world arrives, its first captures are owed though none has begun.
    if (!m_bakes) return readiness;
    if (m_bakesHeld) {
        ++readiness.total;
        return readiness;
    }
    if (m_irradianceBaker.baking()) {
        readiness.done  += m_irradianceBaker.cellsCaptured();
        readiness.total += m_irradianceBaker.bakingCells();
    }
    if (m_bakesProbes) {
        readiness.done  += m_probes.bakedLayers();
        readiness.total += m_probes.ownedLayers();
    }
    const RenderReadiness lightmaps = m_lightmapBaker.progress();
    readiness.done  += lightmaps.done;
    readiness.total += lightmaps.total;
    return readiness;
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
    // pbuffer's only one. Rows are packed tight (init's pixel alignment).
    const GLint glX = static_cast<GLint>(view.viewportX);
    const GLint glY = static_cast<GLint>(view.surfaceHeight) - static_cast<GLint>(view.viewportY) - height;
    std::vector<uint8_t> rows(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    Vkm::GL::FrameBuffer::readDefault(
        glX,
        glY,
        width,
        height,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        rows.size(),
        rows.data()
    );

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
