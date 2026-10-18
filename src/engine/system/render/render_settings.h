#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "l_assert.h"

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief How a texel is fetched for every texture without its own filter.
 *
 * Bilinear leaves the seam between mip levels visible; Trilinear crosses it and
 * is the only mode anisotropy builds on. TextureParams::filterOverride outranks
 * this. project.json stores the name, so enumerator order is not format.
 */
enum class TextureFiltering : uint8_t {
    Nearest = 0,
    Bilinear,
    Trilinear,
    Count       ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief Every output the composite pass can write, once.
 *
 * Columns: enumerator, editor label, shader constant suffix (`MODE_<suffix>`),
 * and whether the forward pass shades it as radiance (then tonemapped) rather
 * than the composite showing a buffer raw. Albedo, Roughness and Metalness are
 * raw buffers the forward pass writes: the surface as it samples it.
 */
#define VKM_RENDER_MODES(X)                                              \
    X(Default,          "Default",              DEFAULT,           true)  \
    X(Wireframe,        "Wireframe",            WIREFRAME,         true)  \
    X(LightingOnly,     "Lighting Only",        LIGHTING_ONLY,     true)  \
    X(Albedo,           "Albedo",               ALBEDO,            false) \
    X(Roughness,        "Roughness",            ROUGHNESS,         false) \
    X(Metalness,        "Metalness",            METALNESS,         false) \
    X(Normals,          "Normals",              NORMALS,           false) \
    X(Depth,            "Depth",                DEPTH,             false) \
    X(AmbientOcclusion, "Ambient Occlusion",    AMBIENT_OCCLUSION, false) \
    X(ScreenShadows,    "Screen Shadows",       SCREEN_SHADOWS,    false) \
    X(GiOnly,           "GI Only",              GI_ONLY,           true)  \
    X(DirectOnly,       "Direct Only",          DIRECT_ONLY,       true)  \
    X(Clusters,         "Light Clusters",       CLUSTERS,          true)  \
    X(Bloom,            "Bloom",                BLOOM,             false) \
    X(ShadowAtlas,      "Shadow Atlas",         SHADOW_ATLAS,      false) \
    X(Fog,              "Fog",                  FOG,               false)

/**
 * @brief Every display transform the composite pass can end a frame with, once.
 *
 * Columns: enumerator, label (shown, and stored in project.json as
 * `render.tonemap` - renaming one breaks saved projects), shader constant
 * suffix (`TONEMAP_<suffix>`).
 */
#define VKM_TONEMAPS(X)                                       \
    X(Reinhard,       "Reinhard",            REINHARD)        \
    X(ACES,           "ACES (filmic)",       ACES)            \
    X(KhronosNeutral, "Khronos PBR Neutral", KHRONOS_NEUTRAL) \
    X(AgX,            "AgX",                 AGX)

/**
 * @brief How linear HDR radiance is landed into the display range; a fixed curve.
 *
 * ACES (the default) is Hill's fit of the RRT and ODT: contrast, and highlights that
 * reach white. Reinhard (`c / (c + 1)`) never reaches white and flattens the mid-tones.
 * Khronos PBR Neutral holds authored albedo as it brightens. AgX (Sobotka) runs to
 * white through a wider gamut, so a bright saturated light whitens instead of turning
 * another colour, at the price of a flatter base look.
 */
enum class Tonemap : uint8_t {
#define VKM_TONEMAP_ENUMERATOR(name, label, glsl) name,
    VKM_TONEMAPS(VKM_TONEMAP_ENUMERATOR)
#undef VKM_TONEMAP_ENUMERATOR
    Count,  ///< Enum size marker (reflection); not a selectable curve.
};

/**
 * @brief What the composite pass writes to the screen.
 *
 * Default is the final tonemapped image; the rest are debug views.
 */
enum class RenderMode : uint8_t {
#define VKM_RENDER_MODE_ENUMERATOR(name, label, glsl, shaded) name,
    VKM_RENDER_MODES(VKM_RENDER_MODE_ENUMERATOR)
#undef VKM_RENDER_MODE_ENUMERATOR
    Count,  ///< Enum size marker (reflection); not a selectable mode.
};

/**
 * @brief Editable render tuning: pass toggles + per-effect parameters.
 *
 * Carried on FrameContext::render; copied into RenderView::settings each frame.
 */
struct RenderSettings {
    // Debug
    RenderMode renderMode = RenderMode::Default;

    // Pass toggles
    bool gtao          = true;
    bool bloom         = true;
    bool probes        = true;
    bool ssr           = true;   ///< Screen-space reflections, over the probes and the sky.
    bool screenShadows = true;   ///< The cascaded sun's contacts its cascades are too coarse for.

    // GTAO
    float gtaoRadius    = 0.6f;   ///< World-space sample radius, MIN_GTAO_RADIUS..MAX_GTAO_RADIUS.
    float gtaoIntensity = 1.0f;   ///< Occlusion strength.
    float gtaoPower     = 1.5f;   ///< Contrast curve.

    // Screen-space reflections
    float ssrMaxRoughness = 0.6f;   ///< Rougher surfaces reflect the probes and the sky alone.
    float ssrMaxDistance  = 50.0f;  ///< World-space trace length.
    /// A ray every two by two pixels, gathered back to each by the surfaces they lie on: the
    /// reflected picture keeps its detail, only where a ray lands is coarser.
    bool  ssrHalfResolution = false;

    // Bloom
    float bloomStrength  = 0.06f;   ///< Bloom scale, added pre-tonemap.
    /// On a pixel's brightest channel, after the exposure: past a sunlit white's, so what glows is
    /// a highlight, an emitter or the sun.
    float bloomThreshold = 2.0f;
    float bloomKnee      = 0.5f;    ///< Soft-knee width around the threshold.
    float bloomRadius    = 0.005f;  ///< Upsample tent radius, fraction of frame width.

    // Display transform
    Tonemap tonemap = Tonemap::ACES;

    /**
     * @brief The exposure in stops: the frame is scaled by 2^exposure before the tonemap.
     *
     * 0 is as lit. One stop up by default, the daylight balance the procedural sky is built for.
     * With autoExposure it is added to the adapted stops.
     */
    float exposure = 1.0f;

    /// Adapt the exposure to the frame's brightness, as an eye does (system/render/exposure.h).
    bool  autoExposure    = false;
    /// The adapted stops' range, before exposure is added: how far it darkens a bright frame
    /// and brightens a dark one.
    float exposureMinimum = -4.0f;
    float exposureMaximum = 8.0f;
    /// How fast it brightens a frame grown dark, and darkens one grown bright: the share of the
    /// gap closed in a second is 1 - e^-rate. By default it brightens slower, as an eye does.
    float adaptBrighter   = 1.0f;
    float adaptDarker     = 3.0f;

    // Culling
    float cullMaxDistance = 500.0f;  ///< World-space.
    float cullMinPixels   = 3.0f;    ///< Screen-pixel size; 0 disables.
    /**
     * @brief Cull the opaque geometry other geometry hides, on the GPU.
     *
     * Off by default: it pays only where the GPU bounds the frame and much of the scene is
     * hidden (docs/reference/visibility.md).
     */
    bool occlusionCulling = false;

    // Anti-aliasing
    /// Scene-pass MSAA samples, one of MSAA_SAMPLE_COUNTS; post runs on the resolved buffer.
    uint32_t msaaSamples = 4;

    /**
     * @brief The frame's default texel sampling; TextureParams::filterOverride outranks it.
     *
     * textureAnisotropy is ignored unless this is Trilinear. Unrelated to
     * MaterialAsset::anisotropy (the BRDF lobe).
     */
    TextureFiltering textureFiltering = TextureFiltering::Trilinear;
    /// Degree when the mode is Trilinear (1 = off); clamped to the driver's ceiling.
    uint32_t textureAnisotropy = 16;

    // Shadows
    /// The largest shadow tile's edge, one of SHADOW_RESOLUTIONS, the sun's near cascades'; costly to raise.
    uint32_t shadowResolution = 4096;
    /**
     * @brief Shadows soft as each light's sourceRadius makes them (PCSS); off, every shadow takes
     *        the hard 3x3 filter, as a light of no size does.
     *
     * The soft path's blocker search and its wider filter are the forward pass's dearest work.
     */
    bool softShadows = true;

    // Resolution
    /**
     * @brief The scene's pixels as a share of the viewport's on each axis,
     *        MIN_RENDER_SCALE..MAX_RENDER_SCALE.
     *
     * The scene, its effects and their targets render at RenderView::sceneWidth/sceneHeight, and
     * the composite scales the frame up to the viewport, where the UI draws at full resolution.
     */
    float renderScale = 1.0f;

    // Overlays
    // The editor's world grid: each axis's line, and the plane of every two axes on.
    bool gridAxisX = false;
    bool gridAxisY = false;
    bool gridAxisZ = false;

    /// The search divides by the radius, so it is never zero.
    static constexpr float MIN_GTAO_RADIUS = 0.05f;
    /// Past this the search reads the screen, not a neighbourhood.
    static constexpr float MAX_GTAO_RADIUS = 5.0f;

    static constexpr uint32_t MSAA_SAMPLE_COUNTS[] = {1, 2, 4, 8};  ///< 1 is off.
    static constexpr uint32_t SHADOW_RESOLUTIONS[] = {1024, 2048, 4096};  ///< What shadowResolution may be.

    /// Below half, the upscale is a blur no tier wants.
    static constexpr float MIN_RENDER_SCALE = 0.5f;
    /// The scene never renders past the viewport's resolution.
    static constexpr float MAX_RENDER_SCALE = 1.0f;

    /**
     * @brief The renderScale that renders a viewport @p viewportHeight pixels tall at @p height.
     *
     * For a settings screen that offers resolutions (WindowManager::renderResolutions): the window
     * keeps its size and the frame is scaled up to it.
     *
     * @param height         The chosen resolution's height.
     * @param viewportHeight The viewport's.
     * @return The scale, clamped to MIN_RENDER_SCALE..MAX_RENDER_SCALE; 1 for an empty viewport.
     */
    static float renderScaleFor(uint32_t height, uint32_t viewportHeight) {
        if (viewportHeight == 0) return 1.0f;
        const float share = static_cast<float>(height) / static_cast<float>(viewportHeight);
        return std::clamp(share, MIN_RENDER_SCALE, MAX_RENDER_SCALE);
    }

    /**
     * @brief @p viewportPixels at the scene's scale, renderScale clamped to
     *        MIN_RENDER_SCALE..MAX_RENDER_SCALE.
     *
     * @param viewportPixels A viewport extent.
     * @return The scene's extent; 0 only for 0.
     */
    uint32_t sceneExtent(uint32_t viewportPixels) const {
        const float scale = std::clamp(renderScale, MIN_RENDER_SCALE, MAX_RENDER_SCALE);
        return static_cast<uint32_t>(std::lround(static_cast<float>(viewportPixels) * scale));
    }

    /**
     * @brief Whether the editor's world grid draws at all.
     *
     * @return True while any of its axes is on.
     */
    bool gridShown() const {
        return gridAxisX || gridAxisY || gridAxisZ;
    }

    /**
     * @brief Whether @p samples is one of MSAA_SAMPLE_COUNTS.
     *
     * @param samples A requested msaaSamples.
     * @return True when it is allowed.
     */
    static bool isMsaaSampleCount(uint32_t samples) {
        for (const uint32_t count : MSAA_SAMPLE_COUNTS) {
            if (count == samples) return true;
        }
        return false;
    }
};

/**
 * @brief The render fields a project ships, one (json-key, member) row each, for load and save.
 *
 * `renderMode` and `grid` are debug/editor aids, so they are not shipped.
 *
 * @tparam Settings RenderSettings, const or not.
 * @tparam Fn       Callable as `f(const char* key, auto& field)`.
 * @param r Settings visited.
 * @param f Called once per shipped field.
 */
template <typename Settings, typename Fn>
void visitShippedRenderFields(Settings& r, Fn&& f) {
    f("gtao",              r.gtao);
    f("bloom",             r.bloom);
    f("probes",            r.probes);
    f("ssr",               r.ssr);
    f("screenShadows",     r.screenShadows);
    f("gtaoRadius",        r.gtaoRadius);
    f("gtaoIntensity",     r.gtaoIntensity);
    f("gtaoPower",         r.gtaoPower);
    f("ssrMaxRoughness",   r.ssrMaxRoughness);
    f("ssrMaxDistance",    r.ssrMaxDistance);
    f("ssrHalfResolution", r.ssrHalfResolution);
    f("bloomStrength",     r.bloomStrength);
    f("bloomThreshold",    r.bloomThreshold);
    f("bloomKnee",         r.bloomKnee);
    f("bloomRadius",       r.bloomRadius);
    f("tonemap",           r.tonemap);
    f("exposure",          r.exposure);
    f("autoExposure",      r.autoExposure);
    f("exposureMinimum",   r.exposureMinimum);
    f("exposureMaximum",   r.exposureMaximum);
    f("adaptBrighter",     r.adaptBrighter);
    f("adaptDarker",       r.adaptDarker);
    f("cullMaxDistance",   r.cullMaxDistance);
    f("cullMinPixels",     r.cullMinPixels);
    f("occlusionCulling",  r.occlusionCulling);
    f("msaaSamples",       r.msaaSamples);
    f("textureFiltering",  r.textureFiltering);
    f("textureAnisotropy", r.textureAnisotropy);
    f("shadowResolution",  r.shadowResolution);
    f("softShadows",       r.softShadows);
    f("renderScale",       r.renderScale);
}

/**
 * @brief A quality tier a game offers its players as one choice.
 */
enum class GraphicsPreset : uint8_t {
    Low,
    Medium,
    High,
    Ultra,
    Count  ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief @p project's settings at @p preset's tier: the fields that cost frame time - anti-aliasing,
 *        the shadow tile, the anisotropy, the render scale, half-resolution reflections - the
 *        tier's, and each screen-space effect and the soft shadows on only where both the project
 *        and the tier have them.
 *
 * The rest - an effect the project turned off, the tonemap, the exposure, each effect's tuning,
 * the culling, the debug view - stays @p project's. Built from the project's settings each time,
 * so a tier chosen after a lower one brings back what that one turned off.
 *
 * @param project The project's own settings.
 * @param preset  The tier; Count, which is none, asserts, and reads as the last.
 * @return The settings to render with.
 */
inline RenderSettings withPreset(const RenderSettings& project, GraphicsPreset preset) {
    VKM_ASSERT(preset < GraphicsPreset::Count, "withPreset takes a tier, not the Count sentinel");
    struct Tier {
        uint32_t msaa;
        uint32_t shadows;
        bool     gtao;
        bool     screenShadows;
        bool     ssr;
        bool     bloom;
        uint32_t anisotropy;
        bool     softShadows;
        float    renderScale;
        bool     ssrHalf;
    };
    // Low to Ultra, in GraphicsPreset order.
    static constexpr Tier TIERS[] = {
        {1, 1024, false, false, false, false, 4,  false, 0.67f, true},   // Low
        {2, 2048, true,  true,  false, true,  8,  false, 0.85f, true},   // Medium
        {4, 4096, true,  true,  true,  true,  16, true,  1.0f,  true},   // High
        {8, 4096, true,  true,  true,  true,  16, true,  1.0f,  false},  // Ultra
    };
    static_assert(std::size(TIERS) == static_cast<size_t>(GraphicsPreset::Count));
    const Tier&    tier = TIERS[std::min(static_cast<size_t>(preset), std::size(TIERS) - 1)];
    RenderSettings r    = project;
    r.msaaSamples       = tier.msaa;
    r.shadowResolution  = tier.shadows;
    r.gtao              = project.gtao && tier.gtao;
    r.screenShadows     = project.screenShadows && tier.screenShadows;
    r.ssr               = project.ssr && tier.ssr;
    r.bloom             = project.bloom && tier.bloom;
    r.textureAnisotropy = tier.anisotropy;
    r.softShadows       = project.softShadows && tier.softShadows;
    r.renderScale       = tier.renderScale;
    r.ssrHalfResolution = tier.ssrHalf;
    return r;
}

} // namespace Vkm::Engine

#define VKM_TONEMAP_LABEL(name, label, glsl) label,
VKM_ENUM_NAMES(::Vkm::Engine::Tonemap, VKM_TONEMAPS(VKM_TONEMAP_LABEL))
#undef VKM_TONEMAP_LABEL

#define VKM_RENDER_MODE_LABEL(name, label, glsl, shaded) label,
VKM_ENUM_NAMES(::Vkm::Engine::RenderMode, VKM_RENDER_MODES(VKM_RENDER_MODE_LABEL))
#undef VKM_RENDER_MODE_LABEL

VKM_ENUM_NAMES(::Vkm::Engine::TextureFiltering, "Nearest", "Bilinear", "Trilinear")

VKM_ENUM_NAMES(::Vkm::Engine::GraphicsPreset, "Low", "Medium", "High", "Ultra")
