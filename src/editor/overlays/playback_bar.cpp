#include "overlays/playback_bar.h"

#include "core/clock.h"
#include "framework/editor_common.h"
#include "framework/scene_io_controller.h"
#include "system/audio/audio_system.h"
#include "ui/editor_style.h"

namespace Vkm::Engine {

namespace {
// Sizes in design px - font/DPI-relative via EditorStyle::px.
float BTN() { return EditorStyle::px(26.0f); }
float GAP() { return EditorStyle::px(4.0f);  }
float PAD() { return EditorStyle::px(5.0f);  }
constexpr int   CONTROLS = 3;  // play/pause, step, stop

// Frame the viewport and name the mode while a session runs. Every panel stays
// live inside one and Stop throws the world away, so a scene edited in play
// mode is a scratch copy - and the only thing on screen that said so was a
// 20px glyph changing shape. That is not something an author notices before
// typing into a field the next Stop will discard.
void drawSessionMarker(EditorContext& ec, float barBottom) {
    const ImU32 accent = ImGui::GetColorU32(EditorStyle::WARNING);
    ImVec2 min = ec.viewportPos;
    ImVec2 max(min.x + ec.viewportSize.x, min.y + ec.viewportSize.y);
    // Inset by the stroke so the whole border lands inside the viewport rect
    // rather than half of it under the neighbouring panel.
    const float stroke = EditorStyle::px(2.0f);
    min.x += stroke * 0.5f; min.y += stroke * 0.5f;
    max.x -= stroke * 0.5f; max.y -= stroke * 0.5f;
    ImGui::GetWindowDrawList()->AddRect(min, max, accent, 0.0f, 0, stroke);

    const char* label = "PLAY MODE - edits are discarded on Stop";
    const float width = ImGui::GetWindowSize().x;
    ImGui::SetCursorPos(ImVec2((width - ImGui::CalcTextSize(label).x) * 0.5f,
                               barBottom + GAP()));
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}
} // namespace

void PlaybackBar::draw(EditorContext& ec, SceneIOController& sceneIO) {
    FrameContext& ctx   = ec.frame;
    Clock&        clock = ctx.clock;

    const bool playing = sceneIO.hasSnapshot();  // a play session is active
    const bool paused  = clock.isPaused();

    const float barH = BTN() + PAD() * 2.0f + 2.0f;
    const float barW = BTN() * CONTROLS + GAP() * (CONTROLS - 1) + PAD() * 2.0f + 2.0f;
    ImVec2 ws = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2((ws.x - barW) * 0.5f, 8.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PAD(), PAD()));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(GAP(), 0.0f));

    // A stepped tick is one tick of world, so it is one tick of sound: the
    // voices it started are held here, on the first draw after it ran. Held
    // rather than never started, because what starts them is AudioSystem
    // reconciling Play-On-Start sources against a simulation that did advance,
    // and the whole seam is that no system in the engine learns an editor
    // exists. So the transport does to a step what it already does to a pause -
    // reaches the device itself - and the world sounds for exactly as long as
    // it moved, instead of the source running on for as long as nobody presses
    // Pause. Everything sounding is held, deliberately: a step is the Pause the
    // author is already in, and Pause holds an audition it finds running too.
    AudioDevice& audio = ec.audioSystem.device();
    if (m_stepPending) {
        m_stepPending = false;
        audio.pauseAllVoices();
    }

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
            // The clock does not reach the mixer, and deliberately so:
            // AudioSystem runs off the frame rather than off simulation time,
            // which is what keeps a shipped game's music, menu and UI clicks
            // alive under its own pause menu. That rule is right there and
            // wrong here - this pause froze the world to be looked at, and the
            // level's ambience playing on underneath it is noise nobody asked
            // for - so the transport holds the voices itself, through the same
            // device it auditions clips with. A clip auditioned while the
            // world is frozen is still heard: only what was already sounding
            // is held, and only what this held is let go again.
            if (running) audio.pauseAllVoices();
            else         audio.resumeAllVoices();
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
            m_stepPending = true;
        }

        ImGui::SameLine();
        // Stop: restore the snapshot (undoing every transform/spawn the sim
        // made) and return to Edit mode. Disabled when not in a play session.
        if (iconButton("vpStop", EditorIcon::Stop, false, playing,
                       "Stop - restore the scene and return to Edit mode", BTN())) {
            // The whole of Stop lives on the controller, because the quit guard
            // has to perform one too - a save cannot run inside a session.
            sceneIO.stopPlaySession(ctx, ec.state);
        }

        m_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    } else {
        m_hovered = false;
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (playing) drawSessionMarker(ec, 8.0f + barH);
}

} // namespace Vkm::Engine
