#pragma once

namespace Engine {

struct EditorState;
class Scene;
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

    public:
        /**
         * @brief Draw the window if the toggle is on; safe to call every frame.
         *
         * Edits the scene's Environment component (the source of truth), not
         * RenderSystem's per-frame mirror; pass enable/disable still goes
         * through RenderSystem (that state lives on the render graph).
         */
        void draw(EditorState& state, Scene& scene, RenderSystem& renderSystem);
};

} // namespace Engine
