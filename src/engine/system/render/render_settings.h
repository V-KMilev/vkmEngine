#pragma once

#include <cstdint>
#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief What the composite pass writes to the screen.
 *
 * Default is the final tonemapped image; the rest blit an intermediate render
 * target for debugging. The MODE_* constants the composite shader switches on
 * are generated from this enum at configure time (render_modes.glsl).
 */
enum class RenderMode : uint8_t {
    Default,
    Depth,
    Normals,
    Roughness,
    Metalness,
    AmbientOcclusion,
    Bloom,
    ShadowAtlas,
    Fog,
    GiOnly,      ///< Indirect (ambient/IBL/GI) term only, tonemapped.
    DirectOnly,  ///< Direct light sum only, tonemapped.
    Clusters,    ///< Forward+ per-cluster light-count heatmap.
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

    // Shadows
    uint32_t shadowResolution = 4096;  ///< Per-tile shadow-atlas resolution (1024/2048/4096); costly to raise.

    // Overlays
    bool grid = false;  ///< World-space ground grid - an editor aid; the editor defaults it on, games leave it off.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::RenderMode, "Default", "Depth", "Normals", "Roughness",
               "Metalness", "Ambient Occlusion", "Bloom", "Shadow Atlas",
               "Fog", "GI Only", "Direct Only", "Light Clusters")
