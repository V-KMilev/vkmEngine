#include "overlays/playback_bar.h"

#include "core/engine.h"
#include "framework/editor_common.h"

namespace Engine {

namespace {
constexpr float BTN = 26.0f;
constexpr float GAP = 4.0f;
constexpr float PAD = 5.0f;
constexpr int   CONTROLS = 3;  // play/pause, step, stop
}

void ViewportPlaybar::draw(EditorContext& ec) {
    FrameContext&    ctx    = ec.frame;
    Engine&          engine = ec.engine;
    SimulationClock& clock  = engine.getSimulationClock();

    const bool paused = clock.isPaused();

    const float barH = BTN + PAD * 2.0f + 2.0f;
    const float barW = BTN * CONTROLS + GAP * (CONTROLS - 1) + PAD * 2.0f + 2.0f;
    ImVec2 ws = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2((ws.x - barW) * 0.5f, 8.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PAD, PAD));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(GAP, 0.0f));

    if (ImGui::BeginChild("##ViewportPlaybar", ImVec2(barW, barH), ImGuiChildFlags_Borders)) {
        // Global Play/Pause: drives the engine's simulation clock, so physics
        // and animation freeze/resume together. Highlighted while running.
        if (iconButton("vpSim", paused ? EditorIcon::Play : EditorIcon::Pause,
                       !paused, true,
                       paused ? "Play - run the simulation"
                              : "Pause - freeze the simulation", BTN)) {
            clock.setPaused(!paused);
        }

        ImGui::SameLine();
        // Step one fixed tick: advances physics AND animation by 1/60s of
        // simulation time. Only meaningful while paused.
        if (iconButton("vpStep", EditorIcon::Step, false, paused,
                       "Step one fixed tick (while paused)", BTN)) {
            clock.requestStep(1);
        }

        ImGui::SameLine();
        // Stop: back to a clean edit state - freeze the sim and rewind every
        // animation to t=0. Physics has no recorded rest pose to rewind to.
        if (iconButton("vpStop", EditorIcon::Stop, false, true,
                       "Stop - pause and rewind animations", BTN)) {
            clock.setPaused(true);
            ctx.scene.forEach<Animation>([&](EntityId, Animation& a) {
                a.playing = false;
                a.time = 0.0f;
            });
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
