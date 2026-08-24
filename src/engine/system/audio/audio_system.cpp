#define VKM_LOG_CATEGORY "AUDIO"

#include "system/audio/audio_system.h"

#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/clock.h"
#include "core/math/rotation.h"
#include "debug/profiler.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/scene.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

void AudioSystem::init(FrameContext& ctx) {
    // Whatever the graph is now is the one the first voices will belong to;
    // starting at 0 would read as a replacement on the first frame.
    m_assetEpoch = ctx.resources.epoch();

    // A host with no sound card, no driver, or no permission to open one is a
    // host the engine still runs on. open() says which it was.
    m_device.open();
}

void AudioSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("AudioSystem::update");

    // A closed device is not a reason to skip this. Every call below is a
    // no-op without one, and running anyway is what makes a silent host behave
    // like a host where every sound is zero-length rather than one where no
    // sound ever ends: play() answers 0, reconcileSource clears `playing`, and
    // gameplay that waits for a one-shot to finish gets its answer. Skipping
    // left that flag stuck true forever, which is the one thing a component
    // whose whole contract is "read it back to learn the sound finished" must
    // not do on a state the engine calls normal.

    // The asset graph was replaced under us - a scene load, or the editor's
    // Stop restoring its snapshot. Every voice is playing a clip that belonged
    // to a world which no longer exists, and no gameplay is left to stop them.
    if (ctx.resources.epoch() != m_assetEpoch) {
        m_assetEpoch = ctx.resources.epoch();
        stopEverything();
    }

    // Sounds that ran out, including any the editor started to audition a clip
    // and has no id left for. A voice this drops that a source still tracks
    // reads as finished on the pass below, which is what it is.
    m_device.reapFinishedVoices();

    updateListener(ctx);

    ++m_frame;
    const bool simRunning = ctx.clock.getSimDelta() > 0.0f;

    // Every source, not the posed ones only. A 2D source reads no position at
    // all, and the entities most likely to carry one - a UI button, anything a
    // behavior spawned - have no Transform, so joining on one would leave them
    // silent forever with `playing` stuck true. An entity with no pose is heard
    // where a default Transform would put it.
    Scene& scene = ctx.scene;
    scene.forEach<AudioSource>([&](EntityId id, AudioSource& source) {
        const glm::vec3 world = scene.has<Transform>(id)
            ? resolvedWorldPosition(scene, id, scene.get<Transform>(id))
            : glm::vec3(0.0f);
        reconcileSource(ctx, id, source, world, simRunning);
    });

    // Anything the walk did not visit has lost its AudioSource or its entity,
    // and there is nobody left to ask it to stop.
    for (auto it = m_voices.begin(); it != m_voices.end(); ) {
        if (it->second.seenOnFrame == m_frame) {
            ++it;
            continue;
        }
        m_device.stopVoice(it->second.voice);
        it = m_voices.erase(it);
    }
}

void AudioSystem::shutdown() {
    stopEverything();
    m_device.close();
}

void AudioSystem::updateListener(FrameContext& ctx) {
    Scene& scene = ctx.scene;

    const EntityId entity = findActiveListener(scene);

    m_hasListener = static_cast<bool>(entity);
    m_device.setListenerActive(m_hasListener);
    if (!m_hasListener) return;

    const Transform& pose     = scene.get<Transform>(entity);
    const glm::quat  rotation = resolvedWorldRotation(scene, entity, pose);
    m_device.setListener(resolvedWorldPosition(scene, entity, pose),
                         Math::computeForward(rotation), Math::computeUp(rotation));
    m_device.setMasterVolume(scene.get<AudioListener>(entity).volume);
}

void AudioSystem::reconcileSource(FrameContext& ctx, EntityId entity, AudioSource& source,
                                  const glm::vec3& worldPosition, bool simRunning) {
    // The authored "start by itself" waits for simulation time, so a scene sat
    // open in the editor stays quiet until someone presses Play. Explicitly
    // requested sounds do not wait, which is what lets a pause menu click.
    if (source.playOnStart && !source.started && simRunning) {
        source.started = true;
        source.playing = true;
    }

    auto it = m_voices.find(entity.index);
    // A slot recycled by an unrelated entity: the source that owned this voice
    // is gone, and the entity now wearing its number must not inherit it.
    if (it != m_voices.end() && it->second.entity != entity) {
        m_device.stopVoice(it->second.voice);
        m_voices.erase(it);
        it = m_voices.end();
    }

    if (!source.playing) {
        if (it != m_voices.end()) {
            m_device.stopVoice(it->second.voice);
            m_voices.erase(it);
        }
        return;
    }

    // A one-shot that reached its end. Writing `playing` back is how gameplay
    // asks whether a sound has finished without holding a handle to anything.
    if (it != m_voices.end() && !m_device.isVoicePlaying(it->second.voice)) {
        m_device.stopVoice(it->second.voice);
        m_voices.erase(it);
        source.playing = false;
        return;
    }

    VoiceParams params;
    params.volume      = source.volume;
    params.pitch       = source.pitch;
    params.loop        = source.loop;
    params.spatial     = source.spatial;
    params.position    = worldPosition;
    params.minDistance = source.minDistance;
    params.maxDistance = source.maxDistance;

    if (it != m_voices.end()) {
        it->second.seenOnFrame = m_frame;
        m_device.updateVoice(it->second.voice, params);
        return;
    }

    // Asked to play with nothing to play. Clearing the request rather than
    // retrying is what keeps a source that never got a clip from asking again
    // sixty times a second for the life of the scene.
    if (!ctx.resources.isAlive(source.clip)) {
        source.playing = false;
        return;
    }

    // Said here rather than from the listener pass, because a project with no
    // audio in it at all should not be told it is missing an ear. What makes
    // the absence a problem is a sound that wanted to be positioned by one.
    if (params.spatial && !m_hasListener && !m_warnedNoListener) {
        m_warnedNoListener = true;
        LOG_WARNING("A positioned sound started with no active AudioListener in the scene - it "
                    "is silent until one exists (non-spatial sources are unaffected)");
    }

    const VoiceId voice = m_device.play(ctx.resources.get(source.clip), params);
    if (voice == 0) {
        source.playing = false;
        return;
    }
    m_voices.emplace(entity.index, ActiveVoice{entity, voice, m_frame});
}

void AudioSystem::stopEverything() {
    m_device.stopAllVoices();
    m_voices.clear();
    m_warnedNoListener = false;
    m_hasListener      = false;
}

} // namespace Vkm::Engine
