#define VKM_LOG_CATEGORY "EDITOR"

#include "session/play_session.h"

#include <string>

#include "vkm_log/logger.h"

#include "core/clock.h"
#include "core/frame_context.h"
#include "core/input_map.h"
#include "editor_state.h"
#include "vkm_platform/window/window_manager.h"
#include "session/asset/asset_writes.h"

namespace Vkm::Engine {

bool PlaySession::start(FrameContext& ctx, EditorState& state) {
    // A tier Render Settings previews is not the project's: the session starts from its own.
    state.endRenderPreview(ctx.graphics);
    return capture(ctx, state, "Play");
}

bool PlaySession::capture(FrameContext& ctx, EditorState& state, const char* action) {
    // The snapshot names assets the library must hand back, or an unrecorded import comes back
    // empty; a terrain comes back from the file written here.
    const AssetWrites writes = writeAssets(ctx.resources);

    // Stop puts each terrain back as its file holds it, which would undo the sculpting not written.
    if (!writes.terrains) {
        const char* lost = " refused: a terrain could not be written, and the scene put back would lose it";
        state.toasts.push(ToastKind::Error, action + std::string(lost));
        return false;
    }

    if (!m_snapshot.capture(ctx.scene, ctx.resources, state.sceneDirty)) {
        LOG_ERROR("PlaySession::capture: failed to serialize scene");
        const char* unsnapped = " refused: the scene could not be snapshotted";
        state.toasts.push(ToastKind::Error, action + std::string(unsnapped));
        return false;
    }
    m_marks = {state.selection, state.hiddenInView, state.lockedInView};
    // The session's steps address the copy Stop discards; see
    // docs/reference/editor.md, "A play session owns the scene".
    state.commands.park(ctx.scene);
    ctx.input.reset();

    // A partial cook does not stop the session, as it does not stop a save.
    if (!writes.cooked) {
        const char* uncooked = ": some assets did not cook and may not come back";
        state.toasts.push(ToastKind::Error, action + std::string(uncooked));
    }
    return true;
}

void PlaySession::end(FrameContext& ctx) {
    // Edit mode: paused, at 1x, the cursor back with the editor, and the project's render settings
    // rather than a tier the game chose.
    if (isPlaying()) ctx.graphics.reset();
    m_snapshot.release();
    m_marks = {};
    ctx.clock.setPaused(true);
    ctx.clock.setTimeScale(1.0f);
    ctx.window.setCursorMode(Platform::CursorMode::Normal);
    // The next session needs the command slots this one's actions hold.
    ctx.input.reset();
}

void PlaySession::setEjected(FrameContext& ctx, bool ejected) {
    m_snapshot.setEjected(ejected, ctx.window);
}

void PlaySession::holdEjectedCursorFree(FrameContext& ctx) {
    m_snapshot.holdCursorFree(ctx.window);
}

} // namespace Vkm::Engine
