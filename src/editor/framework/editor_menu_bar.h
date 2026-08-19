#pragma once

#include <memory>

namespace Vkm::GL { class Texture2D; }

namespace Vkm::Engine {

struct EditorContext;
class SceneIOController;

/**
 * @brief The editor's top menu bar (File / Edit / View / Window / Entity / Help).
 *
 * Extracted from EditorSystem (god-file decomposition). Holds no command
 * state: it reads/writes EditorState and forwards scene-file intents to the
 * SceneIOController; the only owned state is a lazily-loaded brand-mark
 * texture. Draws inside the root window's menu-bar scope
 * with strict ordering (like the viewport overlays), called once per frame
 * between Begin("##Editor") and the panel layout.
 *
 * draw() also renders the scene Save-As / Load dialogs (via the controller)
 * so they stay in the same menu-bar scope they were before.
 *
 * Note: the Import Model dialog is owned by EditorSystem (not the menu bar)
 * because both the Inspector empty-state and the Hierarchy "+" menu also
 * raise `state.requestModelImport`. The menu bar just toggles the flag.
 */
class EditorMenuBar {
    public:
        EditorMenuBar();
        ~EditorMenuBar();

        void draw(EditorContext& ec, SceneIOController& sceneIO);

    private:
        // Brand mark drawn at the left of the menu bar. Lazy-loaded on first
        // draw (needs a live GL context); unique_ptr so this header only needs a
        // forward declaration of Vkm::GL::Texture2D.
        std::unique_ptr<Vkm::GL::Texture2D> m_logo;
        bool m_openAbout = false;  ///< About requested this frame; popup opens at menu-bar scope.
};

} // namespace Vkm::Engine
