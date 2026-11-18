// Who has the pointer and the keyboard (InputOwnership): in a session the viewport is the game's, a
// grabbed cursor is the game's wherever it wandered, and a key pressed at a menu, a field or a rebind
// is that UI's alone.

#include "input/input_ownership.h"

#include "harness/check.h"

using namespace Vkm::Engine;
using namespace Vkm::Test;

namespace {

InputSignals overScene() {
    InputSignals signals;
    signals.viewportHovered = true;
    return signals;
}

InputSignals inSession() {
    InputSignals signals = overScene();
    signals.playing = true;
    return signals;
}

} // namespace

TEST("in a session the viewport is the game's: its keys and clicks, until the session is ejected") {
    const InputOwnership playing = resolveInputOwnership(inSession());
    CHECK(playing.gameHasKeyboard());
    CHECK(!playing.editorHasKeys());
    CHECK(!playing.hostHoldsPointer());
    CHECK(!playing.hostHoldsKeyboard());
    CHECK(!playing.clickPicks());

    InputSignals ejected = inSession();
    ejected.ejected = true;
    CHECK(resolveInputOwnership(ejected).clickPicks());

    // Outside one the editor's shortcuts act over the viewport, and the host holds the pointer, so the
    // game's UI does not scroll.
    const InputOwnership editing = resolveInputOwnership(overScene());
    CHECK(editing.editorHasKeys());
    CHECK(!editing.hostHoldsKeyboard());
    CHECK(editing.hostHoldsPointer());
}

TEST("a held terrain stroke or gizmo drag keeps every editor key from acting, undo and Play included") {
    InputSignals stroking = overScene();
    stroking.stroking = true;
    const InputOwnership stroke = resolveInputOwnership(stroking);
    CHECK(!stroke.editorHasKeys());
    CHECK(!stroke.editorHasSessionKeys());

    InputSignals dragging = overScene();
    dragging.gizmoDragging = true;
    CHECK(!resolveInputOwnership(dragging).editorHasKeys());
}

TEST("in a session a panel or the playbar under the pointer keeps it, and the panel the keyboard") {
    InputSignals overPanel = inSession();
    overPanel.viewportHovered = false;
    const InputOwnership onPanel = resolveInputOwnership(overPanel);
    CHECK(onPanel.editorHasKeys());
    CHECK(onPanel.hostHoldsKeyboard());
    CHECK(onPanel.hostHoldsPointer());

    InputSignals overPlaybar = inSession();
    overPlaybar.overlayHovered = true;
    const InputOwnership onPlaybar = resolveInputOwnership(overPlaybar);
    CHECK(onPlaybar.pointer == PointerOwner::Overlay);
    CHECK(onPlaybar.hostHoldsPointer());
    CHECK(!onPlaybar.clickPicks());

    // A gizmo drag keeps the pointer as it crosses a panel.
    InputSignals dragging = overPanel;
    dragging.playing       = false;
    dragging.gizmoDragging = true;
    CHECK(resolveInputOwnership(dragging).pointer == PointerOwner::GizmoHandle);
}

TEST("a cursor the game has grabbed is the game's wherever it wandered, but the transport keys still act") {
    InputSignals grabbed = inSession();
    grabbed.viewportHovered = false;
    grabbed.cursorCaptured  = true;
    const InputOwnership captured = resolveInputOwnership(grabbed);
    CHECK(captured.pointer == PointerOwner::Captured);
    CHECK(captured.gameHasKeyboard());
    CHECK(!captured.hostHoldsPointer());
    CHECK(!captured.hostHoldsKeyboard());
    CHECK(!captured.clickPicks());
    CHECK(!captured.gizmoMayHover());
    CHECK(!captured.navigationMayHover());
    CHECK(captured.editorHasSessionKeys());
    CHECK(resolveInputOwnership(inSession()).editorHasSessionKeys());
}

TEST("a click on the game's UI is the game's in a session, and selects the element outside one") {
    InputSignals onElement = inSession();
    onElement.gameUIHovered = true;
    CHECK(!resolveInputOwnership(onElement).clickPicks());
    onElement.playing = false;
    CHECK(resolveInputOwnership(onElement).clickPicks());
}

TEST("a key pressed in a field, at a popup or for a rebind is that UI's alone, the transport keys too") {
    InputSignals typing = inSession();
    typing.typing = true;
    CHECK(resolveInputOwnership(typing).hostHoldsKeyboard());
    CHECK(!resolveInputOwnership(typing).editorHasSessionKeys());

    for (const bool rebinding : {false, true}) {
        InputSignals held = overScene();
        held.popupOpen = !rebinding;
        held.rebinding = rebinding;
        const InputOwnership owner = resolveInputOwnership(held);
        CHECK(!owner.editorHasKeys());
        CHECK(!owner.editorHasSessionKeys());
        // The fly camera hears none of it.
        CHECK(owner.panelsHoldKeyboard());
    }
}

TEST("the frame a game frees the cursor, the pointer is where it was grabbed, and the game keeps the keys") {
    InputSignals signals = inSession();
    CHECK(resolveInputOwnership(signals).gameHasKeyboard());

    // Grabbed, ImGui hears no mouse and reports nothing hovered.
    signals.cursorCaptured = true;

    const InputOwnership grabbed = resolveInputOwnership(signals);
    signals.viewportHovered = nextViewportHover(signals.viewportHovered, false, grabbed);
    signals.cursorCaptured  = false;

    const InputOwnership freed = resolveInputOwnership(signals);
    CHECK(freed.pointer == PointerOwner::Scene);
    CHECK(freed.gameHasKeyboard());
    CHECK(!freed.hostHoldsKeyboard());
    CHECK(!freed.hostHoldsPointer());

    // A free cursor that leaves the viewport is believed.
    signals.viewportHovered = nextViewportHover(signals.viewportHovered, false, freed);
    CHECK(resolveInputOwnership(signals).pointer == PointerOwner::Panel);
}
