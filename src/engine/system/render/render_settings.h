#pragma once

namespace Engine {

/**
 * @brief Editable render tuning: pass toggles + per-effect parameters.
 *
 * Owned by the RenderSystem (the editor's Render Settings panel mutates it) and
 * copied into the RenderView each frame, so passes read it via ctx.view.settings
 * instead of hardcoded constants. Backend-agnostic - just data.
 */
struct RenderSettings {
    // Pass toggles
    bool gtao       = true;
    bool ssr        = true;
    bool motionBlur = true;
    bool bloom      = true;
    bool probes     = true;

    // GTAO
    float gtaoRadius    = 0.6f;   ///< World-space sample radius.
    float gtaoIntensity = 1.0f;   ///< Occlusion strength.
    float gtaoPower     = 1.5f;   ///< Contrast curve.
    float gtaoBias      = 0.03f;  ///< View-space self-occlusion guard.

    // SSR
    float ssrIntensity   = 1.0f;   ///< Reflection strength.
    float ssrMaxDistance = 30.0f;  ///< View-space ray length.

    // Motion blur
    float motionBlurIntensity   = 1.0f;   ///< Velocity scale.
    float motionBlurMaxVelocity = 0.05f;  ///< Per-pixel smear clamp (UV space).
    int   motionBlurSamples     = 8;      ///< Taps along the velocity vector.

    // Bloom
    float bloomStrength  = 0.06f;   ///< Bloom blend amount (linear HDR, pre-tonemap).
    float bloomThreshold = 1.0f;    ///< Bright-pass threshold (HDR luminance).
    float bloomKnee      = 0.5f;    ///< Soft-knee width around the threshold.
    float bloomRadius    = 0.005f;  ///< Upsample tent-filter radius (UV space).
};

} // namespace Engine
