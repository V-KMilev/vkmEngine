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
    // --- pass toggles ---
    bool gtao       = true;   ///< Ground-truth ambient occlusion.
    bool ssr        = true;   ///< Screen-space reflections.
    bool motionBlur = true;   ///< Camera motion blur.
    bool bloom      = true;   ///< Bloom.
    bool probes     = true;   ///< Local reflection-probe contribution.

    // --- GTAO ---
    float gtaoRadius    = 0.6f;   ///< World-space sample radius.
    float gtaoIntensity = 1.0f;   ///< Occlusion strength.
    float gtaoPower     = 1.5f;   ///< Contrast curve.

    // --- SSR ---
    float ssrIntensity   = 1.0f;   ///< Reflection strength.
    float ssrMaxDistance = 30.0f;  ///< View-space ray length.

    // --- motion blur ---
    float motionBlurIntensity = 1.0f;  ///< Velocity scale.

    // --- bloom ---
    float bloomStrength = 0.06f;  ///< Bloom blend amount (linear HDR, pre-tonemap).
};

} // namespace Engine
