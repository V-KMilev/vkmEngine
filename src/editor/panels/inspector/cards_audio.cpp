#include "panels/inspector/cards.h"

#include <cstdint>
#include <cstdio>

#include <imgui.h>

#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/scene/transform.h"
#include "resource/asset/audio_clip_asset.h"
#include "ui/audition_transport.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"
#include "editor_state.h"
#include "system/audio/audio_system.h"
namespace Vkm::Engine {

CardInfo cardInfo(Tag<AudioSource>) {
    return {"Audio Source", AddSection::Audio, "Plays a sound from here", EditorIcon::Audio};
}

void drawCard(Rows<AudioSource>& c, CardContext& card) {
    const AudioSource& source = c.value();
    c.row<&AudioSource::clip>();
    const AudioClipAsset* clip = card.resources.tryGet(source.clip);
    if (clip) {
        const double bytes   = static_cast<double>(clip->sampleCount() * sizeof(int16_t));
        const double mb      = bytes / (1024.0 * 1024.0);
        const double seconds = static_cast<double>(clip->duration());
        const uint32_t channels = clip->channels;
        const uint32_t rate     = clip->sampleRate;
        ImGui::TextDisabled("%.2fs, %u channels, %u Hz, %.1f MB", seconds, channels, rate, mb);
    }
    c.slider<&AudioSource::volume>();
    c.row<&AudioSource::pitch>("%.2fx");
    c.row<&AudioSource::loop>();
    c.row<&AudioSource::playOnStart>();
    c.row<&AudioSource::priority>();
    c.row<&AudioSource::spatial>();
    if (source.spatial) {
        c.row<&AudioSource::minDistance>("%.2f m");
        c.row<&AudioSource::maxDistance>("%.1f m");
        c.row<&AudioSource::rolloff>();
        c.row<&AudioSource::acoustics>();
        if (source.acoustics && !card.scene.environment().acoustics.enabled) {
            cardNote(Note::Info, "The scene's Acoustics are off.");
        }
        if (source.maxDistance <= source.minDistance) {
            cardNote(Note::Warning, "Max Distance is not past Min: nothing is attenuated.");
        }
        if (clip && clip->channels > 1) {
            cardNote(Note::Warning, "This clip has %u channels: each sticks to one ear.", clip->channels);
        }
        // Without one it plays at the world origin.
        if (!card.scene.has<Transform>(card.id)) {
            cardNote(Note::Warning, "A positioned sound needs a Transform to have a position.");
        }
        // AudioSystem::warnIfNoListener waits for a spatial voice, so at edit time only this says it.
        if (!findActiveListener(card.scene)) {
            cardNote(Note::Warning, "No active Audio Listener in the scene: this is silent.");
        }
    }
    // Not spatial alone: a muted mix silences everything, auditions too, though the cursor runs.
    Audio::Device& device = card.audio.device();
    if (device.masterVolume() <= 0.0f) {
        cardNote(Note::Warning, "The Audio Listener's volume is 0: nothing is heard, the audition too.");
    }

    // The Asset Browser's transport; see auditionTransport.
    ImGui::Spacing();
    Audio::VoiceId& voice = card.memory.auditionVoice;
    const bool mine = card.memory.auditionOwner == card.id;
    if (auditionTransport("audition", device, voice, mine, clip, ImGui::GetFrameHeight())) {
        card.memory.auditionOwner = card.id;
    }
    ImGui::SameLine(0, EditorStyle::px(8.0f));
    ImGui::AlignTextToFramePadding();
    // From the mixer: `playing` is the scene's word, not the sound's.
    const Audio::VoiceId sourceVoice = card.audio.voiceOf(card.id);
    if (device.isVoiceActive(sourceVoice)) {
        const char*  state  = device.isVoicePaused(sourceVoice) ? "held" : "playing";
        const double cursor = static_cast<double>(device.voiceCursor(sourceVoice));
        ImGui::TextDisabled("Source: %s %.2fs", state, cursor);
    } else {
        ImGui::TextDisabled("%s", source.playing ? "Source: playing" : "Source: idle");
    }
    // Disabled, not hidden, so the card keeps its height when an audition ends.
    auditionScrubber("auditionTime", device, voice, clip ? clip->duration() : 0.0f, -1.0f);
}

CardInfo cardInfo(Tag<AudioListener>) {
    const char* hint = "Where sound is heard from: usually the camera";
    return {"Audio Listener", AddSection::Audio, hint, EditorIcon::Listener};
}

void drawCard(Rows<AudioListener>& c, CardContext& card) {
    const AudioListener& listener = c.value();
    c.row<&AudioListener::active>();
    c.slider<&AudioListener::volume>();
    // findActiveListener joins on the Transform, so one without lost for the reason below.
    const bool     placed = card.scene.has<Transform>(card.id);
    const EntityId heard  = findActiveListener(card.scene);
    if (listener.active && heard && heard != card.id && placed) {
        char winner[64] = {};
        getEntityDisplayName(card.scene, heard, winner, sizeof(winner));
        cardNote(Note::Warning, "Not the ear: '%s' is heard from, the lower-numbered entity.", winner);
    }
    if (listener.active && !placed) cardNote(Note::Warning, "It needs a Transform to have a position.");
}

} // namespace Vkm::Engine
