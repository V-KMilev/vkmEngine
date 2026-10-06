#include "overlays/playback_bar.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/clock.h"
#include "core/system.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "ui/editor_icons.h"
#include "platform/input/input_map.h"
#include "session/scene_io_controller.h"
#include "system/audio/audio_system.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

using EditorStyle::overlayButton;
using EditorStyle::overlayGap;
using EditorStyle::overlayInset;
using EditorStyle::overlayStripChrome;
using EditorStyle::overlayStripHeight;

namespace {
constexpr int CONTROLS = 4;  // play/pause, step, stop, eject

// Frame the viewport during a session: edits there are a scratch copy Stop throws away.
// The label also says when ejected, as nothing else shows the game is deaf.
void drawSessionMarker(EditorContext& ec, float barBottom, bool ejected) {
    const ImU32 accent = ImGui::GetColorU32(EditorStyle::WARNING);
    ImVec2 min = ec.viewportPos;
    ImVec2 max(min.x + ec.viewportSize.x, min.y + ec.viewportSize.y);
    // Inset so the border is not half under the neighbouring panel.
    const float stroke = EditorStyle::px(2.0f);
    min.x += stroke * 0.5f;
    min.y += stroke * 0.5f;
    max.x -= stroke * 0.5f;
    max.y -= stroke * 0.5f;
    ImGui::GetWindowDrawList()->AddRect(min, max, accent, 0.0f, 0, stroke);

    const char* label = ejected
        ? "PLAY MODE, EJECTED - the game hears no input; edits are discarded on Stop"
        : "PLAY MODE - edits are discarded on Stop";
    const float width = ImGui::GetWindowSize().x;
    ImGui::SetCursorPos(ImVec2((width - ImGui::CalcTextSize(label).x) * 0.5f, barBottom + overlayGap()));
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}
} // namespace

void PlaybackBar::playOrPause(EditorContext& ec, SceneIOController& sceneIO) {
    FrameContext& ctx     = ec.frame;
    const bool    playing = sceneIO.isPlaying();
    const bool    running = playing && !ctx.clock.isPaused();

    // No snapshot, no session: it is the only way back to the authored scene.
    if (!playing && !sceneIO.captureSnapshot(ctx, ec.state)) return;

    ctx.clock.setPaused(running);
    AudioDevice& audio = ec.audioSystem.device();
    if (running) audio.pauseAllVoices();
    else         audio.resumeAllVoices();
}

void PlaybackBar::processKeys(EditorContext& ec, SceneIOController& sceneIO) {
    const EditorKeybinds& kb = ec.state.prefs.keybinds;
    if (isPressed(kb.playStop, false)) {
        if (sceneIO.isPlaying()) sceneIO.stopPlaySession(ec.frame, ec.state);
        else                     playOrPause(ec, sceneIO);
    } else if (isPressed(kb.pauseResume, false) && sceneIO.isPlaying()) {
        playOrPause(ec, sceneIO);
        ec.frame.input.discardPendingEdges();
    } else if (isPressed(kb.ejectView, false)) {
        sceneIO.setEjected(ec.frame, !sceneIO.isEjected());
    }
}

void PlaybackBar::draw(EditorContext& ec, SceneIOController& sceneIO, float left, float right) {
    FrameContext& ctx   = ec.frame;
    Clock&        clock = ctx.clock;

    const bool playing = sceneIO.isPlaying();
    const bool paused  = clock.isPaused();

    const float barH = overlayStripHeight();
    const float barW = overlayButton() * CONTROLS + overlayGap() * (CONTROLS - 1) + overlayStripChrome();
    // Centred, unless that would put it on a strip beside it.
    const float centred = (ImGui::GetWindowSize().x - barW) * 0.5f;
    ImGui::SetCursorPos(ImVec2(std::max(std::min(centred, right - barW), left), overlayInset()));

    // A stepped tick is one tick of sound: hold the voices it started. See
    // docs/reference/audio.md, "Two pauses wearing one word".
    AudioDevice& audio = ec.audioSystem.device();
    if (m_stepPending) {
        m_stepPending = false;
        audio.pauseAllVoices();
    }

    const EditorKeybinds& kb = ec.state.prefs.keybinds;
    if (beginOverlayStrip("##PlaybackBar", ImVec2(barW, barH))) {
        const bool running = playing && !paused;
        char simTip[128];
        const char* simAction = !playing ? "Play - snapshot the scene and run the simulation"
            : running ? "Pause - freeze the simulation"
            : "Resume - continue the simulation";
        const KeyLabel simKey = keyLabel(!playing ? kb.playStop : kb.pauseResume);
        snprintf(simTip, sizeof(simTip), "%s  (%s)", simAction, simKey.buf);
        const EditorIcon simIcon = running ? EditorIcon::Pause : EditorIcon::Play;
        if (iconButton("vpSim", simIcon, running, true, simTip, overlayButton())) {
            playOrPause(ec, sceneIO);
        }

        ImGui::SameLine();
        const char* stepTip = "Step one fixed tick (while paused)";
        if (iconButton("vpStep", EditorIcon::Step, false, paused, stepTip, overlayButton())) {
            if (playing || sceneIO.captureSnapshot(ctx, ec.state)) {
                if (!playing) clock.setPaused(true);
                clock.requestStep(1);
                m_stepPending = true;
            }
        }

        ImGui::SameLine();
        char stopTip[128];
        snprintf(
            stopTip,
            sizeof(stopTip),
            "Stop - restore the scene and return to Edit mode  (%s)",
            keyLabel(kb.playStop).buf
        );
        if (iconButton("vpStop", EditorIcon::Stop, false, playing, stopTip, overlayButton())) {
            // Stop lives on the controller: the unsaved-changes guard performs one too.
            sceneIO.stopPlaySession(ctx, ec.state);
        }

        ImGui::SameLine();
        const bool ejected = sceneIO.isEjected();
        char ejectTip[128];
        const char* ejectAction = ejected
            ? "Return - look through the game's camera again"
            : "Eject - fly the editor's view while the game runs";
        snprintf(ejectTip, sizeof(ejectTip), "%s  (%s)", ejectAction, keyLabel(kb.ejectView).buf);
        if (iconButton("vpEject", EditorIcon::Camera, ejected, playing, ejectTip, overlayButton())) {
            sceneIO.setEjected(ctx, !ejected);
        }

        m_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
        );
    } else {
        m_hovered = false;
    }
    endOverlayStrip();

    if (playing) drawSessionMarker(ec, overlayInset() + barH, sceneIO.isEjected());
}

} // namespace Vkm::Engine
