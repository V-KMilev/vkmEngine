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
        void draw(EditorContext& ec, EnvironmentConfig& env);

    private:
        void drawLighting(EditorContext& ec, EnvironmentConfig& env);
        void drawCamera(EditorContext& ec, EnvironmentConfig& env);
        void drawPost(EditorContext& ec, EnvironmentConfig& env);
        void drawScene(EditorContext& ec, EnvironmentConfig& env);
        void drawPipeline(EditorContext& ec);
        void drawPresetBar(EnvironmentConfig& env);

        char m_filter[64] = {};

        // Edit buffers for the IBL HDR / LUT path InputText fields. They are
        // re-synced from the EnvironmentConfig at the top of each draw, so
        // switching the selected entity doesn't leak the in-flight edit.
        char m_hdrPathBuf[260] = {};
        char m_lutPathBuf[260] = {};

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
