#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/event/bus.h"
#include "core/system.h"
#include "ecs/entity.h"
#include "resource/asset/audio_clip_asset.h"
#include "system/audio/audio_device.h"
#include "system/audio/audio_events.h"

namespace Vkm::Engine {

class EventBus;
struct AudioSource;

/**
 * @brief Turns AudioSource and AudioListener components into what the mixer hears.
 *
 * Runs after HierarchySystem in the Transform stage, as it reads world poses. Not
 * gated on simulation time, except playOnStart: a paused world keeps its music and UI
 * clicks (docs/reference/audio.md, "Two pauses wearing one word"). Voices are owned
 * here, not on the component, so copying an entity never copies a live voice.
 */
class AudioSystem : public System {
    public:
        AudioSystem() = default;
        ~AudioSystem() override = default;

        AudioSystem(const AudioSystem& other) = delete;
        AudioSystem& operator=(const AudioSystem& other) = delete;

        AudioSystem(AudioSystem && other) = delete;
        AudioSystem& operator=(AudioSystem && other) = delete;

    public:
        void init(FrameContext& ctx) override;
        void update(FrameContext& ctx) override;
        void shutdown() override;

        /**
         * @brief The device, for a tool (auditioning, a transport pause), not a game.
         *
         * @return The device this system mixes through.
         */
        AudioDevice& device() { return m_device; }

        /**
         * @brief Run without opening a device, for a host that plays to nobody; set before init().
         *
         * @param silent True to leave the device closed.
         */
        void setSilent(bool silent = true) { m_silent = silent; }

        /**
         * @brief The voice @p entity's AudioSource is being heard through.
         *
         * For asking the device isVoicePaused() or voiceCursor(). Read-only: a voice
         * stopped behind this system's back reads as finished and clears `playing`.
         *
         * @param entity Entity whose source to look up; generation-checked.
         * @return The live voice, or 0 when the source is silent, has no voice
         *         yet, or does not exist.
         */
        VoiceId voiceOf(EntityId entity) const;

        /**
         * @brief Stop every voice and forget them.
         *
         * For when the world goes before the asset graph is replaced (update() handles
         * that case itself). Pending requests go too, since an old graph's handle can
         * name a different clip in the new one.
         */
        void stopEverything();

    private:
        /**
         * @brief Point the ear at findActiveListener's listener, or turn it off.
         *
         * An earless scene resets the master gain to unity, since it came from the listener.
         *
         * @param ctx Frame context supplying the scene to search.
         */
        void updateListener(FrameContext& ctx);

        /**
         * @brief Start, update or stop the one voice belonging to @p entity.
         *
         * @param ctx Frame context supplying the scene and the asset graph.
         * @param entity Entity carrying @p source.
         * @param source Source to reconcile; `playing` and `started` are written back.
         * @param worldPosition Where the entity is, for a spatial source.
         * @param simRunning Whether simulation time advanced; all playOnStart waits for.
         */
        void reconcileSource(
            FrameContext& ctx,
            EntityId entity,
            AudioSource& source,
            const glm::vec3& worldPosition,
            bool simRunning
        );

        /**
         * @brief Start every PlaySoundEvent collected since the last frame.
         *
         * Voices are not recorded; reapFinishedVoices() releases them. A request out of
         * the ear's range is dropped silently, which is safe because it neither moves nor
         * loops; one with no attenuation is never out of range.
         *
         * @param ctx Frame context supplying the asset graph the clips live in.
         */
        void startPendingRequests(FrameContext& ctx);

        /**
         * @brief Say once per world that a positioned sound has no ear to hear it.
         *
         * Said at voice start, not in the listener pass, so a project with no audio
         * is not told; stopEverything resets it.
         *
         * @param spatial Whether the voice about to start is positioned; nothing is said for a flat one.
         */
        void warnIfNoListener(bool spatial);

        /**
         * @brief Warn once that a positioned voice plays a clip whose channels cannot cross.
         *
         * See docs/reference/audio.md, "A positioned source wants a mono clip". Keyed by
         * clip name, since a handle's slot index is inherited by the next asset.
         *
         * @param asset Clip, read for its channel count and name.
         * @param spatial Whether the voice is positioned; stereo is correct on a flat one.
         */
        void warnIfStereoSpatial(const AudioClipAsset& asset, bool spatial);

    private:
        /**
         * @brief One entity's live voice.
         *
         * The whole entity is kept so a recycled slot is not handed the previous
         * occupant's sound; the clip, so a re-pointed source is heard playing the new one.
         */
        struct ActiveVoice {
            EntityId        entity;
            AudioClipHandle clip;
            VoiceId         voice = 0;
            uint64_t        seenOnFrame = 0;
        };

    private:
        AudioDevice m_device;
        bool        m_silent = false;

        /// Keyed by entity slot index; one voice per source, at most.
        std::unordered_map<uint32_t, ActiveVoice> m_voices;

        /// Requests collected since the last update, drained at the end of it.
        std::vector<PlaySoundEvent> m_pending;

        /// Swapped out of m_pending to be walked; a member for its capacity.
        std::vector<PlaySoundEvent> m_starting;

        /// Kept because shutdown() takes no ctx; session-stable.
        EventBus* m_events = nullptr;

        /// Subscription to drop at shutdown; 0 when nothing is subscribed.
        ListenerId m_playListener = 0;

        /// Asset-graph identity; a change means every voice plays a clip from a gone world.
        uint64_t m_assetEpoch = 0;

        /// Frame counter used to spot voices whose source stopped existing.
        uint64_t m_frame = 0;

        /// Whether the last listener pass found one; read when a voice starts.
        bool m_hasListener = false;
        /// Where the ear was this frame.
        glm::vec3 m_listenerPosition{0.0f};

        /// Whether the missing-listener warning has been written for this world.
        bool m_warnedNoListener = false;

        /// Clips already reported as multi-channel on a positioned voice, by name.
        std::unordered_set<std::string> m_warnedStereoClips;
};

} // namespace Vkm::Engine
