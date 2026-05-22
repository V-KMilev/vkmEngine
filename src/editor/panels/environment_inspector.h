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
 * and grouped collapsible sections (Lighting, Camera & Exposure,
 * Post-Processing, Scene, Pipeline) - all in the tall scrollable Inspector
 * instead of the cramped bottom strip.
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
        // can mark the scene dirty once at the end. Pipeline returns true
        // when a pass enable / culling setting changes (also persisted).
        bool drawLighting(EditorContext& ec, EnvironmentConfig& env);
        bool drawCamera(EditorContext& ec, EnvironmentConfig& env);
        bool drawPost(EditorContext& ec, EnvironmentConfig& env);
        bool drawScene(EditorContext& ec, EnvironmentConfig& env);
        bool drawPipeline(EditorContext& ec);
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
