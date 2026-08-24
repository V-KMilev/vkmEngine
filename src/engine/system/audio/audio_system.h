#pragma once

#include <cstdint>
#include <unordered_map>

#include "core/system.h"
#include "ecs/entity.h"
#include "system/audio/audio_device.h"

namespace Vkm::Engine {

struct AudioSource;

/**
 * @brief Turns AudioSource and AudioListener components into what the mixer hears.
 *
 * Registered at SystemStage::Transform, after the world resolve, because every
 * pose it reads is one HierarchySystem produces there. A behavior that sets
 * AudioSource::playing during Simulation is still heard on that frame, since
 * Transform runs after Simulation rather than on the next frame.
 *
 * It is not gated on simulation time, and that is deliberate: a system reads
 * the timeline its responsibility lives on, and audio is presentation. It steps
 * no time of its own either - the mixer runs on the device's thread.
 * Pausing the simulation must not cut the music, silence a menu, or stop a UI
 * click from being heard - the world simply stops moving, so 3D positions stop
 * changing because nothing moved. The one thing pause does hold back is
 * AudioSource::playOnStart, which waits for simulation time to advance so that
 * an unplayed scene open in the editor stays quiet.
 *
 * A source's voice is owned here rather than on the component, so that
 * duplicating an entity, undoing a delete or instancing a prefab copies the
 * intent to play and never a live voice two entities would then fight over.
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
         * @brief The device, for the editor's clip audition.
         *
         * The one place outside this system that plays a sound, and it plays a
         * clip rather than an entity - there is no component to reconcile, so
         * routing it through the component path would mean inventing one.
         */
        AudioDevice& device() { return m_device; }

    private:
        /**
         * @brief Point the ear at the scene's active listener, or turn it off.
         *
         * Which listener that is comes from findActiveListener, so the editor's
         * cards and the mixer cannot disagree about which one is heard from.
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
         * @brief Stop every voice and forget them.
         *
         * Used when the asset graph is replaced under the system, and at
         * shutdown. Voices hold their clip's samples alive, so this is about
         * stopping sounds that belong to a world that no longer exists rather
         * than about safety.
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

        /// Keyed by entity slot index; one voice per source, at most.
        std::unordered_map<uint32_t, ActiveVoice> m_voices;

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

        /// Whether the missing-listener warning has already been written.
        bool m_warnedNoListener = false;
};

} // namespace Vkm::Engine
