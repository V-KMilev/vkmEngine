#include "overlays/playback_bar.h"

#include "core/engine.h"
#include "framework/editor_common.h"
#include "framework/scene_io_controller.h"

namespace Engine {

namespace {
// Sizes in design px - font/DPI-relative via EditorStyle::px.
float BTN() { return EditorStyle::px(26.0f); }
float GAP() { return EditorStyle::px(4.0f);  }
float PAD() { return EditorStyle::px(5.0f);  }
constexpr int   CONTROLS = 3;  // play/pause, step, stop
} // namespace

void PlaybackBar::draw(EditorContext& ec, SceneIOController& sceneIO) {
    FrameContext&    ctx    = ec.frame;
    Engine&          engine = ec.engine;
    Clock& clock = engine.getClock();

    const bool playing = sceneIO.hasSnapshot();  // a play session is active
    const bool paused  = clock.isPaused();

    const float barH = BTN() + PAD() * 2.0f + 2.0f;
    const float barW = BTN() * CONTROLS + GAP() * (CONTROLS - 1) + PAD() * 2.0f + 2.0f;
    ImVec2 ws = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2((ws.x - barW) * 0.5f, 8.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PAD(), PAD()));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(GAP(), 0.0f));

    if (ImGui::BeginChild("##PlaybackBar", ImVec2(barW, barH), ImGuiChildFlags_Borders)) {
        // In Edit mode this is "Play": snapshot the authored scene (so Stop can
        // restore it), then run the clock. In a play session it toggles it.
        const bool running = playing && !paused;
        if (iconButton("vpSim", running ? EditorIcon::Pause : EditorIcon::Play,
                       running, true,
                       !playing ? "Play - snapshot the scene and run the simulation"
                                : running ? "Pause - freeze the simulation"
                                          : "Resume - continue the simulation", BTN())) {
            if (!playing) sceneIO.captureSnapshot(ctx, ec.state);
            // New paused state: pause if it was running, otherwise run (start
            // from Edit mode, or resume a paused session).
            clock.setPaused(running);
        }

        ImGui::SameLine();
        // Step one fixed tick (physics + animation + scripts). Meaningful only
        // while paused; from Edit mode it begins a paused play session first so
        // the step never mutates the authored scene irreversibly.
        if (iconButton("vpStep", EditorIcon::Step, false, paused,
                       "Step one fixed tick (while paused)", BTN())) {
            if (!playing) {
                sceneIO.captureSnapshot(ctx, ec.state);
                clock.setPaused(true);
            }
            clock.requestStep(1);
        }

        ImGui::SameLine();
        // Stop: restore the snapshot (undoing every transform/spawn the sim
        // made) and return to Edit mode. Disabled when not in a play session.
        if (iconButton("vpStop", EditorIcon::Stop, false, playing,
                       "Stop - restore the scene and return to Edit mode", BTN())) {
            clock.setPaused(true);
            sceneIO.restoreSnapshot(ctx, ec.state);
        }

        m_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    } else {
        m_hovered = false;
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace Engine
