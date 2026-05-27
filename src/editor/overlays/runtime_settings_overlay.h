#pragma once

namespace Engine {

struct EditorState;
class RenderSystem;

/**
 * @brief Player-facing graphics settings overlay (F10).
 *
 * A self-contained ImGui window that lets a player toggle render passes
 * and tweak a handful of environment knobs at runtime - distinct from
 * the editor's full Environment Inspector, which exposes everything.
 * Renders independent of editor visibility, so it's reachable when the
 * editor is hidden (F5 -> hidden -> F10 still works).
 */
class RuntimeSettingsOverlay {
    public:
        RuntimeSettingsOverlay() = default;
        ~RuntimeSettingsOverlay() = default;

        RuntimeSettingsOverlay(const RuntimeSettingsOverlay& other) = delete;
        RuntimeSettingsOverlay& operator=(const RuntimeSettingsOverlay& other) = delete;

        RuntimeSettingsOverlay(RuntimeSettingsOverlay && other) = delete;
        RuntimeSettingsOverlay& operator=(RuntimeSettingsOverlay && other) = delete;

        /// Draw the window if the toggle is on. Safe to call every frame.
        void draw(EditorState& state, RenderSystem& renderSystem);
};

} // namespace Engine
