#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/event/bus.h"
#include "core/system.h"
#include "ecs/entity.h"
#include "system/audio/audio_device.h"
#include "system/audio/audio_events.h"

namespace Vkm::Engine {

class EventBus;
struct AudioSource;

/**
 * @brief Turns AudioSource and AudioListener components into what the mixer hears.
 *
 * Registered at SystemStage::Transform, after the world resolve, because every
 * pose it reads is one HierarchySystem produces there - and a behavior that sets
 * AudioSource::playing during Simulation is still heard on that frame.
 *
 * Not gated on simulation time, deliberately: audio is presentation, so a paused
 * world keeps its music, its menu and its UI clicks, and 3D positions stop
 * changing only because nothing moved. The one thing pause holds back is
 * playOnStart, so an unplayed scene open in the editor stays quiet. The editor's
 * transport pause is a different pause and is held by the editor itself; see
 * docs/reference/system/audio.md, "Two pauses wearing one word".
 *
 * A source's voice is owned here rather than on the component, so duplicating an
 * entity, undoing a delete or instancing a prefab copies the intent to play and
 * never a live voice two entities would fight over.
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
         * @brief The device, for the editor.
         *
         * Two uses, both of them the editor's and neither of them a game's. It
         * auditions a clip, which plays a file rather than an entity - there is
         * no component to reconcile, so routing it through the component path
         * would mean inventing one - and its transport holds every voice while
         * the world is frozen, which is the pause this system deliberately does
         * not have.
         */
        AudioDevice& device() { return m_device; }

        /**
         * @brief Run without opening a device, for a host that plays to nobody.
         *
         * Set by the host before the first frame, because what a process is for
         * is what its executable is: `vkm_server` referees a game it does not
         * present. Everything below still runs - the rest of this system is
         * written against a device that never opened - so this buys silence
         * without a second path through it.
         *
         * @param silent True to leave the device closed.
         */
        void setSilent(bool silent = true) { m_silent = silent; }

        /**
         * @brief The voice @p entity's AudioSource is being heard through.
         *
         * The other half of what an editor card needs and cannot work out for
         * itself. AudioSource::playing says the source wants to be heard, which
         * is scene state and answers nothing about the mixer: a voice the
         * editor's transport is holding keeps that flag true while it makes no
         * sound at all, so a card reading the flag alone tells the author a
         * paused world is playing. The id handed back is what the device
         * answers isVoicePaused() and voiceCursor() about, so the card can say
         * held rather than playing and say how far in it stopped.
         *
         * Read-only on purpose: nothing outside this system may stop, start or
         * re-point a source's voice, because this table is what reconciles them
         * against their components every frame and a voice let go behind its
         * back would be started again on the next one.
         *
         * @param entity Entity whose source to look up; the generation is
         *        checked, so a recycled slot answers 0 rather than the previous
         *        occupant's sound.
         * @return The live voice, or 0 when the source is silent, has no voice
         *         yet, or does not exist.
         */
        VoiceId voiceOf(EntityId entity) const;

    private:
        /**
         * @brief Point the ear at the scene's active listener, or turn it off.
         *
         * Which listener that is comes from findActiveListener, so the editor
         * and the mixer cannot disagree about which one is heard from.
         *
         * An earless scene also gets the master gain back at unity, because that
         * gain came from the listener and goes away with it. Without that, a
         * listener at low volume that is deleted or unticked leaves every 2D
         * source quiet with nothing left on screen holding the slider, and not
         * even a scene load puts it back.
         *
         * @param ctx Frame context supplying the scene to search.
         */
        void updateListener(FrameContext& ctx);

        /**
         * @brief Start, update or stop the one voice belonging to @p entity.
         *
         * @param ctx Frame context supplying the scene and the asset graph.
         * @param entity Entity carrying @p source.
         * @param source The source to reconcile; its `playing` and `started`
         *        flags are written back.
         * @param worldPosition Where the entity is, for a spatial source.
         * @param simRunning Whether simulation time advanced this frame, which
         *        is the only thing playOnStart waits for.
         */
        void reconcileSource(FrameContext& ctx, EntityId entity, AudioSource& source,
                             const glm::vec3& worldPosition, bool simRunning);

        /**
         * @brief Start every PlaySoundEvent collected since the last frame.
         *
         * Run after the component walk and after the sweep for voices whose
         * source vanished, so a request's voice is never mistaken for an
         * abandoned one. The voice is deliberately not recorded: nothing can
         * stop it, so nothing has to, and reapFinishedVoices() releases it
         * once it ends.
         *
         * @param ctx Frame context supplying the asset graph the clips live in.
         */
        void startPendingRequests(FrameContext& ctx);

        /**
         * @brief Say once per world that a positioned sound has no ear to hear it.
         *
         * Called from both start paths, a source's voice and a request's,
         * because either can be the only positioned sound a project has: one
         * that plays entirely through requests owns no AudioSource, so the
         * Inspector's card has nothing to warn on and this line is all that
         * would explain the silence.
         *
         * Not said from the listener pass, because a project with no audio in
         * it at all should not be told it is missing an ear - what makes the
         * absence a problem is a sound that wanted to be positioned by one.
         * Once per world rather than once per voice, since the failure is the
         * scene's and does not read better repeated per footstep. Per world
         * and not per run because stopEverything clears the flag along with
         * the voices, so the scene that replaces this one - a load, or the
         * editor's Stop - is diagnosed on its own account rather than
         * inheriting a line written about a world it never shared.
         *
         * @param spatial Whether the voice about to start is positioned; a flat
         *        one is unaffected by the absence, so nothing is said for it.
         */
        void warnIfNoListener(bool spatial);

        /**
         * @brief Warn once that a positioned voice plays a clip whose channels cannot cross.
         *
         * The backend mixes a voice's channels one-to-one and attenuates each by
         * the gain for the speaker it landed on, so half a stereo clip's field is
         * unreachable from any position - and a clip whose channels are identical
         * is indistinguishable from mono, which is what makes the mistake quiet.
         * The measurements: docs/reference/system/audio.md, "A positioned source
         * wants a mono clip".
         *
         * Once per clip rather than once per world, because two stereo clips are
         * two authoring mistakes, and remembered against the clip's name: a
         * handle's id is a slot index, which the next asset in that slot inherits.
         *
         * @param asset The clip itself, read for its channel count and name.
         * @param spatial Whether the voice about to start is positioned; a flat
         *        voice is mixed without a spatializer, so stereo is correct there.
         */
        void warnIfStereoSpatial(const AudioClipAsset& asset, bool spatial);

        /**
         * @brief Stop every voice and forget them.
         *
         * Used when the asset graph is replaced under the system, and at
         * shutdown. Voices hold their clip's samples alive, so this is about
         * stopping sounds that belong to a world that no longer exists rather
         * than about safety.
         *
         * Requests waiting to start go too, and bluntly: every one of them,
         * including a request made against the graph that just arrived. The
         * two cannot be told apart - both were emitted between the same pair
         * of updates - and a handle from the old graph is not merely dead,
         * since a swap hands the new graph its own generations and an old
         * index can be alive there naming a different clip. Only a load that
         * lands after this system in the frame can lose a good request, which
         * is the editor's UI stage; at runtime the load is in Simulation and
         * the flip is consumed on that same frame.
         */
        void stopEverything();

    private:
        /**
         * @brief One entity's live voice.
         *
         * The entity is kept whole, not just its slot: a destroyed source's
         * slot can be recycled by an unrelated entity, and without the
         * generation this table would hand that entity the previous
         * occupant's sound.
         */
        struct ActiveVoice {
            EntityId entity;
            VoiceId  voice = 0;
            uint64_t seenOnFrame = 0;
        };

    private:
        AudioDevice m_device;
        bool        m_silent = false;

        /// Keyed by entity slot index; one voice per source, at most.
        std::unordered_map<uint32_t, ActiveVoice> m_voices;

        /// Requests collected since the last update, drained at the end of it.
        std::vector<PlaySoundEvent> m_pending;

        /**
         * @brief The bus subscribed to, kept because shutdown() takes no ctx.
         *
         * Session-stable: the Engine owns one EventBus by value for the life
         * of the run, which is the same guarantee BehaviorContext relies on.
         */
        EventBus* m_events = nullptr;

        /// Subscription to drop at shutdown; 0 when nothing is subscribed.
        ListenerId m_playListener = 0;

        /**
         * @brief Asset-graph identity, watched for replacement.
         *
         * A scene load hands the ResourceManager a whole new graph, and every
         * voice is then playing a clip from a world that has gone. The epoch is
         * the engine's existing signal for exactly that, so audio uses it
         * rather than asking the editor to call something.
         */
        uint64_t m_assetEpoch = 0;

        /// Frame counter used to spot voices whose source stopped existing.
        uint64_t m_frame = 0;

        /// Whether the last listener pass found one; read when a voice starts.
        bool m_hasListener = false;

        /// Whether the missing-listener warning has been written for this world.
        bool m_warnedNoListener = false;

        /// Clips already reported as multi-channel on a positioned voice, by name.
        std::unordered_set<std::string> m_warnedStereoClips;
};

} // namespace Vkm::Engine
