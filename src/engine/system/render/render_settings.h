#pragma once

#include <cstdint>
#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief How a texel is fetched before anisotropy is considered, for every
 * texture that has not pinned its own filter.
 *
 * Bilinear smooths within a mip level but leaves the seam between levels
 * visible; Trilinear crosses that seam and is the only mode anisotropic
 * filtering can build on. Nearest is here for a scene whose art is all point
 * sampled - a pixel-art game, or a look chosen deliberately.
 *
 * A single texture that must not be blended does not ask through this. That is
 * a claim about its own content rather than a quality trade, and it belongs to
 * the asset: see TextureParams::filterOverride, which outranks whatever is set
 * here.
 */
enum class TextureFiltering : uint8_t {
    Nearest = 0,
    Bilinear,
    Trilinear,
};


/**
 * @brief Every output the composite pass can write, once.
 *
 * Three things are this list: the enum, the names the editor's combo shows, and
 * the `MODE_*` constants the composite shader switches on. The third is a column
 * here rather than something derived from the enum elsewhere, and the backend
 * writes it into the shader prelude beside the `#version`.
 *
 * Columns: the enumerator, the label the editor shows, and the suffix the shader
 * constant carries (`MODE_<suffix>`). The last two differ where a name reads
 * better to a person than to a shader - "Light Clusters" against MODE_CLUSTERS -
 * which is exactly what a mechanical derivation could not have known.
 */
#define VKM_RENDER_MODES(X)                                       \
    X(Default,          "Default",           DEFAULT)             \
    X(Depth,            "Depth",             DEPTH)               \
    X(Normals,          "Normals",           NORMALS)             \
    X(Roughness,        "Roughness",         ROUGHNESS)           \
    X(Metalness,        "Metalness",         METALNESS)           \
    X(AmbientOcclusion, "Ambient Occlusion", AMBIENT_OCCLUSION)   \
    X(Bloom,            "Bloom",             BLOOM)               \
    X(ShadowAtlas,      "Shadow Atlas",      SHADOW_ATLAS)        \
    X(Fog,              "Fog",               FOG)                 \
    X(GiOnly,           "GI Only",           GI_ONLY)             \
    X(DirectOnly,       "Direct Only",       DIRECT_ONLY)         \
    X(Clusters,         "Light Clusters",    CLUSTERS)

/**
 * @brief What the composite pass writes to the screen.
 *
 * Default is the final tonemapped image; the rest blit an intermediate render
 * target for debugging - the indirect term alone, the direct sum alone, the
 * Forward+ per-cluster light-count heatmap. Expanded from VKM_RENDER_MODES.
 */
enum class RenderMode : uint8_t {
#define VKM_RENDER_MODE_ENUMERATOR(name, label, glsl) name,
    VKM_RENDER_MODES(VKM_RENDER_MODE_ENUMERATOR)
#undef VKM_RENDER_MODE_ENUMERATOR
    Count,  ///< Enum size marker (reflection); not a selectable mode.
};
/**
 * @brief Editable render tuning: pass toggles + per-effect parameters.
 *
 * Owned by the RenderSystem (the editor's Render Settings panel mutates it) and
 * copied into the RenderView each frame, so passes read it via ctx.view.settings
 * instead of hardcoded constants. Backend-agnostic - just data.
 */
struct RenderSettings {
    // Debug
    RenderMode renderMode = RenderMode::Default;  ///< Composite output: final image or a debug buffer.

    // Pass toggles
    bool gtao       = true;
    bool bloom      = true;
    bool probes     = true;
    bool occlusionCulling = true;  ///< Test instances against the Hi-Z pyramid before drawing them.

    // GTAO
    float gtaoRadius    = 0.6f;   ///< World-space sample radius.
    float gtaoIntensity = 1.0f;   ///< Occlusion strength.
    float gtaoPower     = 1.5f;   ///< Contrast curve.
    float gtaoBias      = 0.03f;  ///< View-space self-occlusion guard.

    // Bloom
    float bloomStrength  = 0.06f;   ///< Bloom blend amount (linear HDR, pre-tonemap).
    float bloomThreshold = 1.0f;    ///< Bright-pass threshold (HDR luminance).
    float bloomKnee      = 0.5f;    ///< Soft-knee width around the threshold.
    float bloomRadius    = 0.005f;  ///< Upsample tent-filter radius (UV space).

    // Anti-aliasing
    uint32_t msaaSamples = 4;  ///< Scene-pass MSAA samples (1 = off, 2/4/8); post runs on the resolved buffer.

    /**
     * @brief How a texel is sampled, and with it textureAnisotropy below.
     *
     * Not MaterialAsset::anisotropy, which is the brushed-metal BRDF lobe - the
     * same word for an unrelated thing.
     *
     * Two fields rather than one ladder because they are orthogonal: the filter
     * decides how a texel is sampled, the degree decides how many samples a
     * stretched footprint gets. Anisotropy means nothing without mipmapped
     * sampling, so it is ignored unless the mode is Trilinear. The editor
     * presents them as one list; the model keeps them apart.
     *
     * Both are the frame's default rather than its decree - a texture carrying
     * TextureParams::filterOverride keeps its own.
     */
    TextureFiltering textureFiltering = TextureFiltering::Trilinear;
    uint32_t textureAnisotropy = 16;  ///< Degree when the mode is Trilinear (1 = off); clamped to the driver's ceiling.

    // Shadows
    uint32_t shadowResolution = 4096;  ///< Per-tile shadow-atlas resolution (1024/2048/4096); costly to raise.

    // Overlays
    bool grid = false;  ///< World-space ground grid - an editor aid; the editor defaults it on, games leave it off.
};

} // namespace Vkm::Engine

/**
 * @brief The render fields a project ships, one (json-key, member) row each.
 *
 * One list, walked by both directions of both readers, so `project.json` and
 * the editor can never drift about what a field is called.
 *
 * What is NOT here is the point of the split. `renderMode` selects a debug
 * buffer and `grid` draws editor chrome: neither is a look anybody ships, and
 * both stay in the editor's own settings. Everything else is the author's
 * answer to what the game looks like, and by `ProjectPaths`' own test - "would
 * you commit this?" - it is project data.
 */
template <typename Settings, typename Fn>
void visitShippedRenderFields(Settings& r, Fn&& f) {
    f("gtao",              r.gtao);
    f("bloom",             r.bloom);
    f("probes",            r.probes);
    f("occlusionCulling",  r.occlusionCulling);
    f("gtaoRadius",        r.gtaoRadius);
    f("gtaoIntensity",     r.gtaoIntensity);
    f("gtaoPower",         r.gtaoPower);
    f("gtaoBias",          r.gtaoBias);
    f("bloomStrength",     r.bloomStrength);
    f("bloomThreshold",    r.bloomThreshold);
    f("bloomKnee",         r.bloomKnee);
    f("bloomRadius",       r.bloomRadius);
    f("msaaSamples",       r.msaaSamples);
    f("textureFiltering",  r.textureFiltering);
    f("textureAnisotropy", r.textureAnisotropy);
    f("shadowResolution",  r.shadowResolution);
}

#define VKM_RENDER_MODE_LABEL(name, label, glsl) label,
VKM_ENUM_NAMES(::Vkm::Engine::RenderMode, VKM_RENDER_MODES(VKM_RENDER_MODE_LABEL))
#undef VKM_RENDER_MODE_LABEL
