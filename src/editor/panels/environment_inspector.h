#pragma once

#include <string>

#include "framework/asset_picker.h"

namespace Engine {

struct EditorContext;
struct EnvironmentConfig;

/**
 * @brief Inspector body for the singleton Environment entity.
 *
 * Renders the whole rendering/post stack the way modern engines edit a
 * Post-Process Volume / WorldEnvironment: a search box, a quality-preset bar,
 * and seven topical sections grouped by user intent rather than feature
 * implementation:
 *   1. World            - IBL, ambient, background, tone mapping
 *   2. Lighting & Shadows - shadow atlas, OIT, occlusion
 *   3. Camera FX        - exposure, DoF, motion blur, TAA (temporal +
 *                         camera-bound effects)
 *   4. Image Post       - bloom, lens dirt, lens flare, color grading
 *   5. Screen-Space FX  - GTAO, SSR (screen-space rendering effects)
 *   6. Diagnostics      - render mode, grid, AABB debug
 *   7. Performance      - per-pass toggles, visibility culling (advanced)
 *
 * Owned by InspectorPanel; called when the selected entity has an
 * EnvironmentConfig component. Holds the toggle-memo state so flipping an
 * effect off/on is lossless.
 */
class EnvironmentInspector {
    public:
        EnvironmentInspector() = default;
        ~EnvironmentInspector() = default;

        EnvironmentInspector(const EnvironmentInspector& other) = delete;
        EnvironmentInspector& operator=(const EnvironmentInspector& other) = delete;

        EnvironmentInspector(EnvironmentInspector && other) = delete;
        EnvironmentInspector& operator=(EnvironmentInspector && other) = delete;

    public:
        void draw(EditorContext& ec, EnvironmentConfig& env);

    private:
        // Each section returns true if any input mutated env, so draw()
        // can mark the scene dirty once at the end. drawPerformance returns
        // true when a pass enable / culling setting changes (also persisted).
        bool drawWorld(EditorContext& ec, EnvironmentConfig& env);
        bool drawLightingShadows(EditorContext& ec, EnvironmentConfig& env);
        bool drawCameraFX(EditorContext& ec, EnvironmentConfig& env);
        bool drawImagePost(EditorContext& ec, EnvironmentConfig& env);
        bool drawScreenSpaceFX(EditorContext& ec, EnvironmentConfig& env);
        bool drawDiagnostics(EditorContext& ec, EnvironmentConfig& env);
        bool drawPerformance(EditorContext& ec);
        bool drawPresetBar(EnvironmentConfig& env);

        char m_filter[64] = {};

        // Edit buffers for the IBL HDR / LUT path InputText fields. The
        // buffer is refilled from EnvironmentConfig only when the source
        // string actually changed (e.g. picker write-back, scene swap) -
        // refreshing every frame would clobber an in-flight edit before
        // Enter/Apply could commit it.
        char        m_hdrPathBuf[260] = {};
        char        m_lutPathBuf[260] = {};
        std::string m_hdrPathLastSync;
        std::string m_lutPathLastSync;

        // Remembered values so a header toggle can switch an effect fully off
        // and back on without losing the user's tuning.
        std::string m_iblPathMemo;
        std::string m_lutPathMemo;
        float       m_bloomStrengthMemo = 0.04f;

        // One picker per browse button so popup ids stay unique and caches
        // survive open-close cycles.
        AssetPicker m_iblPicker;
        AssetPicker m_lutPicker;
};

} // namespace Engine
