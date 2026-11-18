#pragma once

#include <imgui.h>

#include "ui/editor_widgets.h"
#include "ui/style/editor_icons.h"
#include "ui/style/editor_style.h"

namespace Vkm::Engine {

/**
 * @brief What a clip transport row did this frame.
 */
struct ClipTransport {
    bool rewound = false;  ///< Stop pressed: at zero, not playing.
    bool looped  = false;  ///< Loop changed, which the scene saves.
    bool sped    = false;  ///< Speed changed, which the scene saves.
};

/**
 * @brief A clip player's transport row: Play/Pause, Stop, Loop and Speed.
 *
 * Play, Pause and Stop are previews, not edits; Loop and Speed are authored and reported
 * for the caller to record. Leaves the cursor on the row for the caller's controls.
 *
 * @tparam Player Anything with playing, time, looping and speed.
 * @param id Prefix unique among the transports in one window.
 * @param player The player the row drives.
 * @param minSpeed The drag's lowest speed: 0 where the player cannot run backwards.
 * @param speedWidth Width of the speed drag; -1 fills the row.
 * @return What was pressed or changed.
 */
template <typename Player>
ClipTransport clipTransport(const char* id, Player& player, float minSpeed, float speedWidth) {
    ClipTransport result;
    const float gap    = EditorStyle::px(8.0f);
    const float height = ImGui::GetFrameHeight();

    ImGui::PushID(id);
    const EditorIcon playIcon = player.playing ? EditorIcon::Pause : EditorIcon::Play;
    const char* playTip = player.playing ? "Pause" : "Play";
    if (iconButton("play", playIcon, player.playing, true, playTip, height)) {
        player.playing = !player.playing;
    }
    ImGui::SameLine(0, gap);
    if (iconButton("stop", EditorIcon::Stop, false, true, "Stop (rewind to start)", height)) {
        player.playing = false;
        player.time    = 0.0f;
        result.rewound = true;
    }
    ImGui::SameLine(0, gap);
    const char* loopTip = player.looping ? "Looping" : "Play once";
    if (iconButton("loop", EditorIcon::Loop, player.looping, true, loopTip, height)) {
        player.looping = !player.looping;
        result.looped  = true;
    }
    ImGui::SameLine(0, gap);
    ImGui::SetNextItemWidth(speedWidth);
    result.sped = ImGui::DragFloat(
        "##speed",
        &player.speed,
        0.005f,
        minSpeed,
        10.0f,
        "Speed %.2fx",
        PROP_CLAMP
    );
    ImGui::PopID();
    return result;
}

} // namespace Vkm::Engine
