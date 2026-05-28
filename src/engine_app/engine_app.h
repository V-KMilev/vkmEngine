#pragma once

namespace Engine {

class Engine;
class CameraController;
class EventSystem;
class VisibilitySystem;
class RenderSystem;

/**
 * @brief Engine-side systems exposed to the application after bootstrap.
 *
 * Returned by setupEngineApp() so the caller can pass them to extras the
 * bootstrap deliberately does not know about - chief among them the
 * editor's EditorSystem, which the runtime binary never instantiates.
 */
struct EngineAppSystems {
    CameraController& camera;
    EventSystem&      events;
    VisibilitySystem& visibility;
    RenderSystem&     render;
};

/**
 * @brief Wire up the standard engine application against @p engine.
 *
 * Registers all engine-tier systems (camera, event, animation, hierarchy,
 * visibility, render, file-watcher), loads every pipeline shader, builds
 * the default forward pipeline, then seeds the default scene + IBL
 * environment. The editor is NOT touched here; the editor binary adds
 * EditorSystem from main() after this returns.
 *
 * @param engine Engine instance the caller owns (already constructed with
 *               its window created).
 * @return References to the systems the caller may need to wire further
 *         (the editor takes all four; the runtime ignores them).
 */
EngineAppSystems setupEngineApp(Engine& engine);

} // namespace Engine
