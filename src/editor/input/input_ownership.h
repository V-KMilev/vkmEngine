#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief What the pointer is over, as far as a click is concerned.
 *
 * In the order a press is offered. Captured outranks all: a grabbed cursor
 * points at nothing the editor drew.
 */
enum class PointerOwner : uint8_t {
    Panel,        ///< Off the viewport.
    Overlay,      ///< Tool strip, view bar, playbar or navigation axes.
    GizmoHandle,  ///< A gizmo handle, or a gizmo drag wherever it has gone.
    GameUI,       ///< A blocking element of the game's UI.
    Scene,
    Captured      ///< Hidden and grabbed, by the fly camera or a running game.
};

/**
 * @brief Everything the frame's ownership is decided from.
 *
 * Hovers are the previous frame's: the overlays draw after what must yield to them.
 */
struct InputSignals {
    bool viewportHovered = false;  ///< Over the viewport, nothing of ImGui's above it.
    bool overlayHovered  = false;  ///< As PointerOwner::Overlay.
    bool gizmoHovered    = false;
    bool gizmoDragging   = false;  ///< Wherever the pointer has gone.
    bool gameUIHovered   = false;  ///< A blocking game UI element is under the pointer.
    bool cursorCaptured  = false;  ///< Hidden, grabbed and re-centred.
    bool playing         = false;
    bool ejected         = false;  ///< The session's viewport shows the editor's view.
    bool typing          = false;  ///< A text field has the keyboard.
    bool popupOpen       = false;  ///< A menu, popup or modal is open and owns keys.
    bool rebinding       = false;  ///< Preferences awaits the key to bind.
};

/**
 * @brief Who has the pointer and the keyboard this frame, decided once.
 *
 * Readers of a press ask this, not a hover or ImGui flag, so none disagree. In a
 * session over the viewport, keys and buttons are the game's but for the transport
 * and editor-toggle keys and the playbar; ejected, the editor owns it as in Edit mode.
 */
struct InputOwnership {
    PointerOwner pointer   = PointerOwner::Panel;
    bool         playing   = false;
    bool         ejected   = false;
    bool         typing    = false;
    bool         popupOpen = false;
    bool         rebinding = false;

    /**
     * @brief Whether the viewport shows the game: a session, not ejected.
     *
     * @return true while rendering through the game's camera.
     */
    bool gameHasViewport() const { return playing && !ejected; }

    /**
     * @brief Whether a running game has the keyboard.
     *
     * @return true while the game has the viewport and the pointer is on it.
     */
    bool gameHasKeyboard() const { return gameHasViewport() && pointer != PointerOwner::Panel; }

    /**
     * @brief Whether a key is the editor's own UI's before it is anybody's
     *        binding: a field typed into, a popup, or a key being rebound.
     *
     * @return true while no editor keybind may answer a press.
     */
    bool keysHeldByUI() const { return typing || popupOpen || rebinding; }

    /**
     * @brief Whether the editor's shortcuts may act on this frame's keys.
     *
     * @return true when neither the game nor the editor's own UI has the keyboard.
     */
    bool editorHasKeys() const { return !gameHasKeyboard() && !keysHeldByUI(); }

    /**
     * @brief Whether the play transport and editor toggle keys may act.
     *
     * They are the way out of a session that grabbed the keys, so only the
     * editor's own UI stops them.
     *
     * @return true unless a field, a popup or a rebind has the keyboard.
     */
    bool editorHasSessionKeys() const { return !keysHeldByUI(); }

    /**
     * @brief Whether the editor's own UI holds the pointer, so a panel drag
     *        does not steer the fly camera.
     *
     * @return true over a panel, an overlay or a gizmo handle.
     */
    bool panelsHoldPointer() const {
        return pointer == PointerOwner::Panel || pointer == PointerOwner::Overlay
            || pointer == PointerOwner::GizmoHandle;
    }

    /**
     * @brief The keyboard half of panelsHoldPointer.
     *
     * @return true while the editor's UI has the key, or the game has the
     *         viewport and the pointer is off it.
     */
    bool panelsHoldKeyboard() const { return keysHeldByUI() || (gameHasViewport() && !gameHasKeyboard()); }

    /**
     * @brief The pointer half of what the chrome declares to the game.
     *
     * All of it when the viewport is not the game's, so its UI does not scroll
     * under an author's cursor (a saved offset edited with no undo). What is
     * under the cursor is still resolved, so a click there picks the element.
     *
     * @return The panels' answer while the game has the viewport, otherwise true.
     */
    bool hostHoldsPointer() const { return !gameHasViewport() || panelsHoldPointer(); }

    /**
     * @brief The keyboard half of what the chrome declares to the game.
     *
     * @return The panels' answer, or true while ejected.
     */
    bool hostHoldsKeyboard() const { return ejected || panelsHoldKeyboard(); }

    /**
     * @brief Whether a transform-gizmo handle may hover and start a drag.
     *
     * @return true over a handle, the game's UI or the scene.
     */
    bool gizmoMayHover() const {
        return pointer == PointerOwner::GizmoHandle || pointer == PointerOwner::GameUI
            || pointer == PointerOwner::Scene;
    }

    /**
     * @brief Whether the navigation axes may hover.
     *
     * @return true on the viewport while the cursor is free.
     */
    bool navigationMayHover() const {
        return pointer != PointerOwner::Panel && pointer != PointerOwner::Captured;
    }

    /**
     * @brief Whether a left click in the viewport picks.
     *
     * Never while the game has the viewport: it answers every click.
     *
     * @return true over the scene or the game's UI otherwise.
     */
    bool clickPicks() const {
        return !gameHasViewport() && (pointer == PointerOwner::Scene || pointer == PointerOwner::GameUI);
    }
};

/**
 * @brief Decide the frame's ownership.
 *
 * @param signals Reported by the editor and the window.
 * @return The frame's ownership.
 */
inline InputOwnership resolveInputOwnership(const InputSignals& signals) {
    InputOwnership owner;
    owner.playing   = signals.playing;
    owner.ejected   = signals.playing && signals.ejected;
    owner.typing    = signals.typing;
    owner.popupOpen = signals.popupOpen;
    owner.rebinding = signals.rebinding;

    if (signals.cursorCaptured)        owner.pointer = PointerOwner::Captured;
    else if (signals.gizmoDragging)    owner.pointer = PointerOwner::GizmoHandle;
    else if (!signals.viewportHovered) owner.pointer = PointerOwner::Panel;
    else if (signals.overlayHovered)   owner.pointer = PointerOwner::Overlay;
    else if (signals.gizmoHovered)     owner.pointer = PointerOwner::GizmoHandle;
    else if (signals.gameUIHovered)    owner.pointer = PointerOwner::GameUI;
    else                               owner.pointer = PointerOwner::Scene;
    return owner;
}

/**
 * @brief The viewport hover the next frame's signals take.
 *
 * ImGui has no mouse while the cursor is grabbed, so its hover is meaningless;
 * taken, the first frame after release would read as a panel and hand the host
 * the keyboard with the game's keys still down. The cursor returns where it was
 * grabbed, so the pre-grab hover stands.
 *
 * @param previous Hover the signals hold now.
 * @param seen     ImGui's viewport hover this frame.
 * @param owner    This frame's ownership.
 * @return @p seen, or @p previous while the cursor is captured.
 */
inline bool nextViewportHover(bool previous, bool seen, const InputOwnership& owner) {
    return owner.pointer == PointerOwner::Captured ? previous : seen;
}

} // namespace Vkm::Engine
