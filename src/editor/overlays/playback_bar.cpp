#include "overlays/playback_bar.h"

#include <algorithm>

#include <imgui.h>

#include "core/clock.h"
#include "core/frame_context.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "ui/style/editor_icons.h"
#include "core/input_map.h"
#include "session/play_session.h"
#include "session/scene_session.h"
#include "system/audio/audio_system.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"
#include "overlays/overlay_strip.h"

namespace Vkm::Engine {

namespace {
constexpr int CONTROLS = 4;  // play/pause, step, stop, eject

constexpr float SPEEDS[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};  ///< The playback speeds offered; one is
                                                                   ///< the game as written.

/// A function: px() needs an ImGui context, absent when file-scope initializers run.
float speedWidth() { return OverlayStrip::button() * 1.6f; }

/**
 * @brief The host's playback speed (Clock::setHostScale), picked from SPEEDS.
 *
 * In the warning color while not 1x, so a slowed session is never mistaken for a slow game.
 *
 * @param clock Holds the host's speed; a pick sets it.
 */
void drawSpeed(Clock& clock) {
    const float speed = clock.getHostScale();
    char label[24];
    snprintf(label, sizeof(label), "%gx##speed", speed);
    const bool bent = speed != 1.0f;
    if (bent) ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
    if (ImGui::Button(label, ImVec2(speedWidth(), OverlayStrip::button()))) ImGui::OpenPopup("##Speeds");
    if (bent) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Playback speed; the game's own time scale is untouched");
    if (ImGui::BeginPopup("##Speeds")) {
        for (const float choice : SPEEDS) {
            char text[16];
            snprintf(text, sizeof(text), "%gx", choice);
            if (ImGui::Selectable(text, choice == speed)) clock.setHostScale(choice);
        }
        ImGui::EndPopup();
    }
}

/**
 * @brief Frame the viewport during a session, whose edits Stop throws away; the status bar says so in words.
 *
 * @param ec Supplies the viewport's rect.
 */
void drawSessionFrame(EditorContext& ec) {
    ImVec2 min = ec.viewportPos;
    ImVec2 max(min.x + ec.viewportSize.x, min.y + ec.viewportSize.y);
    // Inset so the border is not half under the neighboring panel.
    const float stroke = EditorStyle::px(2.0f);
    min.x += stroke * 0.5f;
    min.y += stroke * 0.5f;
    max.x -= stroke * 0.5f;
    max.y -= stroke * 0.5f;
    ImGui::GetWindowDrawList()->AddRect(min, max, ImGui::GetColorU32(EditorStyle::WARNING), 0.0f, 0, stroke);
}

/**
 * @brief Ask for Stop, which the editor performs before its next frame, and pause the game till then.
 *
 * The game's systems run first in that frame: paused, they tick no more once Stop is pressed.
 *
 * @param ec For the clock and the requests.
 */
void askStop(EditorContext& ec) {
    ec.frame.clock.setPaused(true);
    ec.state.requests.ask(Ask::StopPlay);
}
} // namespace

void PlaybackBar::playOrPause(EditorContext& ec) {
    PlaySession&  play    = ec.play;
    FrameContext& ctx     = ec.frame;
    const bool playing    = play.isPlaying();
    const bool running    = playing && !ctx.clock.isPaused();

    // No snapshot, no session: it is the only way back to the authored scene.
    if (!playing && !play.start(ctx, ec.state)) return;

    ctx.clock.setPaused(running);
    Audio::Device& audio = ec.audioSystem.device();
    if (running) audio.pauseAllVoices();
    else         audio.resumeAllVoices();
}

void PlaybackBar::restart(EditorContext& ec) {
    if (!ec.play.isPlaying()) return;
    ec.session.stopPlaySession(ec.frame, ec.state);
    // A failed restore keeps the session, frozen: a play now would run what Stop could not put back.
    if (ec.play.isPlaying()) return;
    playOrPause(ec);
}

void PlaybackBar::processKeys(EditorContext& ec) {
    PlaySession& play = ec.play;
    const EditorKeybinds& kb = ec.state.prefs.keybinds;
    if (isPressed(kb.playStop, false)) {
        if (play.isPlaying()) askStop(ec);
        else                  playOrPause(ec);
    } else if (isPressed(kb.pauseResume, false) && play.isPlaying()) {
        playOrPause(ec);
        ec.frame.input.discardPendingEdges();
    } else if (isPressed(kb.ejectView, false)) {
        play.setEjected(ec.frame, !play.isEjected());
    }
}

void PlaybackBar::draw(EditorContext& ec, float left, float right) {
    PlaySession& play = ec.play;
    FrameContext& ctx = ec.frame;
    Clock& clock = ctx.clock;

    const bool playing = play.isPlaying();
    const bool paused  = clock.isPaused();

    const float barH = OverlayStrip::height();
    const float barW = OverlayStrip::button() * CONTROLS + OverlayStrip::gap() * CONTROLS + speedWidth()
        + OverlayStrip::chrome();
    // Centered, unless that would put it on a strip beside it.
    const float centered = (ImGui::GetWindowSize().x - barW) * 0.5f;
    const float top      = ImGui::GetCursorStartPos().y + OverlayStrip::inset();
    const ImVec2 corner(std::max(std::min(centered, right - barW), left), top);
    ImGui::SetCursorPos(corner);

    // A stepped tick is one tick of sound: hold the voices it started. See
    // docs/reference/audio.md, "Two pauses wearing one word".
    Audio::Device& audio = ec.audioSystem.device();
    if (m_stepPending) {
        m_stepPending = false;
        audio.pauseAllVoices();
    }

    const EditorKeybinds& kb = ec.state.prefs.keybinds;
    if (OverlayStrip::begin("##PlaybackBar", ImVec2(barW, barH))) {
        const bool running = playing && !paused;
        char simTip[128];
        const char* simAction = !playing ? "Play - snapshot the scene and run the simulation"
            : running ? "Pause - freeze the simulation"
            : "Resume - continue the simulation";
        const KeyLabel simKey = keyLabel(!playing ? kb.playStop : kb.pauseResume);
        snprintf(simTip, sizeof(simTip), "%s  (%s)", simAction, simKey.buf);
        const EditorIcon simIcon = running ? EditorIcon::Pause : EditorIcon::Play;
        if (iconButton("vpSim", simIcon, running, true, simTip, OverlayStrip::button())) {
            playOrPause(ec);
        }

        ImGui::SameLine();
        const char* stepTip = "Step one fixed tick (while paused)";
        if (iconButton("vpStep", EditorIcon::Step, false, paused, stepTip, OverlayStrip::button())) {
            if (playing || play.start(ctx, ec.state)) {
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
        if (iconButton("vpStop", EditorIcon::Stop, false, playing, stopTip, OverlayStrip::button())) {
            askStop(ec);
        }

        ImGui::SameLine();
        const bool ejected = play.isEjected();
        char ejectTip[128];
        const char* ejectAction = ejected
            ? "Return - look through the game's camera again"
            : "Eject - fly the editor's view while the game runs";
        snprintf(ejectTip, sizeof(ejectTip), "%s  (%s)", ejectAction, keyLabel(kb.ejectView).buf);
        if (iconButton("vpEject", EditorIcon::Camera, ejected, playing, ejectTip, OverlayStrip::button())) {
            play.setEjected(ctx, !ejected);
        }

        ImGui::SameLine();
        drawSpeed(clock);

        m_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
        );
    } else {
        m_hovered = false;
    }
    OverlayStrip::end();

    if (playing) drawSessionFrame(ec);
}

} // namespace Vkm::Engine
