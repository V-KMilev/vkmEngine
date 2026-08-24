#include "ui/audition_transport.h"

#include <cstdio>

#include <imgui.h>

#include "resource/asset/audio_clip_asset.h"
#include "ui/editor_icons.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {
// Gap between the two buttons, matching the Inspector card's other rows.
constexpr float BUTTON_GAP = 8.0f;
}  // namespace

bool auditionTransport(const char* idStr, AudioDevice& device, VoiceId& voice,
                       bool mine, const AudioClipAsset* clip, float size) {
    const bool open     = device.isOpen();
    const bool live     = mine && device.isVoiceActive(voice);
    const bool held     = live && device.isVoicePaused(voice);
    const bool sounding = live && !held;

    bool started = false;
    char id[48];

    snprintf(id, sizeof(id), "%sPlay", idStr);
    if (iconButton(id, sounding ? EditorIcon::Pause : EditorIcon::Play, sounding,
                   open && (clip != nullptr || live),
                   !open     ? "No audio device - this cannot be heard"
                   : sounding ? "Pause the audition"
                   : held     ? "Resume the audition"
                              : "Audition the clip (does not change the scene)", size)) {
        if (sounding) {
            device.pauseVoice(voice);
        } else if (held) {
            device.resumeVoice(voice);
        } else if (clip != nullptr) {
            device.stopVoice(voice);
            VoiceParams audition;
            audition.spatial = false;
            voice   = device.play(*clip, audition);
            started = true;
        }
    }

    ImGui::SameLine(0, BUTTON_GAP);
    snprintf(id, sizeof(id), "%sStop", idStr);
    // A held voice stops here like any other: it is a place in a clip, not a
    // sound that has already finished.
    if (iconButton(id, EditorIcon::Stop, false, live, "Stop the audition", size)) {
        device.stopVoice(voice);
        voice = 0;
    }

    return started;
}

void auditionScrubber(const char* idStr, AudioDevice& device, VoiceId voice,
                      float duration, float width) {
    if (duration <= 0.0f) return;

    const bool live = device.isVoiceActive(voice);

    char label[48];
    snprintf(label, sizeof(label), "##%s", idStr);
    char timeFmt[32];
    snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", static_cast<double>(duration));

    float cursor = live ? device.voiceCursor(voice) : 0.0f;
    ImGui::BeginDisabled(!live);
    ImGui::SetNextItemWidth(width);
    if (ImGui::SliderFloat(label, &cursor, 0.0f, duration, timeFmt, PROP_CLAMP))
        device.seekVoice(voice, cursor);
    ImGui::EndDisabled();
}

} // namespace Vkm::Engine
