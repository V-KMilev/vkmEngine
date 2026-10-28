#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/event/bus.h"
#include "core/system.h"
#include "ecs/entity.h"
#include "resource/asset/audio_clip_asset.h"
#include "system/audio/acoustics.h"
#include "system/audio/audio_device.h"
#include "system/audio/audio_events.h"
#include "system/physics/query/query.h"

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
         * @brief Stop every voice and forget them, the requests not yet started too.
         *
         * update() does this as the asset graph is replaced - an old graph's handle can name a
         * different clip in the new one - and endWorld() as the world is, keeping what reached it
         * from the new world; a host calls it to end them sooner (a script reload).
         */
        void stopEverything();

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
            VoiceId         voice       = 0;
            uint64_t        seenOnFrame = 0;
            float           blocked     = 0.0f;  ///< What the rays last said, 0..1.
            float           occlusion   = 0.0f;  ///< Easing toward `blocked`, as heard.
        };

        /// A request as collected, and the world it reached this system in.
        struct Asked {
            PlaySoundEvent request;
            uint64_t       world = 0;  ///< Scene::epoch as it was delivered
        };

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
         * @brief Gather the static and kinematic colliders the acoustic rays strike, and re-measure the room.
         *
         * A few room rays a frame (Acoustics::ROOM_RAYS_PER_FRAME), so the estimate is a rolling
         * window; the reverb eases toward what it says. With acoustics off, no ear or no device,
         * the reverb fades out and no occlusion is asked.
         *
         * @param ctx Frame context supplying the scene and the clock.
         */
        void updateAcoustics(FrameContext& ctx);

        /**
         * @brief Draw the acoustics' rays while the host shows DebugCategory::Acoustics: the room's
         *        window out from the ear and off what each struck, and each shaped source's rays from
         *        the ear, stopped where something blocks them.
         *
         * @param ctx Frame context supplying the scene and the host's DebugDraw.
         */
        void drawAcoustics(FrameContext& ctx) const;

        /**
         * @brief The share of the rays from the ear to @p position the colliders updateAcoustics
         *        gathered stop.
         *
         * @param position    Where the sound is.
         * @param minDistance The source's near distance, which sets how far around it the side
         *                    rays aim.
         * @param body        The body the sound sits in, which does not block it; none for none.
         * @return 0 heard straight, 1 behind a wall.
         */
        float blockedShare(const glm::vec3& position, float minDistance, EntityId body = {}) const;

        /**
         * @brief Stop every voice and forget the world's room and what it asked for: what a world
         *        replaced takes along.
         *
         * A request that reached this system in the new world stays, its clip still the graph's.
         */
        void endWorld();

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
         * is not told; endWorld resets it.
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
        AudioDevice m_device;
        bool        m_silent = false;

        /// Keyed by entity slot index; one voice per source, at most.
        std::unordered_map<uint32_t, ActiveVoice> m_voices;

        /// Requests collected since the last update, drained at the end of it.
        std::vector<Asked> m_pending;

        /// Swapped out of m_pending to be walked; a member for its capacity.
        std::vector<Asked> m_starting;

        /// Kept because shutdown() takes no ctx; session-stable.
        EventBus* m_events = nullptr;

        /// Subscription to drop at shutdown; 0 when nothing is subscribed.
        ListenerId m_playListener = 0;

        /// Asset-graph identity; a change means every voice plays a clip from a gone world.
        uint64_t m_assetEpoch = 0;
        /// The world's (Scene::epoch): a new one - a load, a restart, the editor's Stop - starts in
        /// silence, its room measured afresh.
        uint64_t m_worldEpoch = 0;

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

        /// Whether the scene's acoustics are on, with an ear and an open device, this frame.
        bool m_acoustics = false;
        /// The reverb the scene asks for, the scene's AcousticsSettings::reverb.
        float m_reverbStrength = 1.0f;
        /// Seconds since the last frame, for the easing.
        float m_frameSeconds = 0.0f;
        /// The static and kinematic colliders round the ear and its sounds, gathered a frame for every ray.
        QueryParts m_world;
        /// The room rays the estimate is taken over, a window refilled a few a frame.
        std::array<Acoustics::RoomRay, Acoustics::ROOM_WINDOW> m_roomRays{};
        size_t m_roomNext   = 0;
        size_t m_roomFilled = 0;
        /// The room's reverb as heard, easing toward what the rays measure.
        Acoustics::Reverb m_reverb;
};

} // namespace Vkm::Engine
