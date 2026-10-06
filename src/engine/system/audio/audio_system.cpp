#define VKM_LOG_CATEGORY "AUDIO"

#include "system/audio/audio_system.h"

#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/clock.h"
#include "core/event/event_bus.h"
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
    // Starting at 0 would read as a graph replacement on the first frame.
    m_assetEpoch = ctx.resources.epoch();

    // A host with no usable device still runs; open() says why.
    if (!m_silent) m_device.open();

    // Collect here, start in update(): emit() is synchronous, and a voice must not be
    // created mid-way through another system's walk.
    m_events = &ctx.events;
    m_playListener = m_events->subscribe<PlaySoundEvent>(
        [this](const PlaySoundEvent& request) { m_pending.push_back(request); }
    );
}

void AudioSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("AudioSystem");

    // Runs whole on an unopened device, which behaves as if every sound were zero-length,
    // so `playing` still clears; see docs/reference/audio.md.

    // The asset graph was replaced under us (see ResourceManager::epoch).
    if (ctx.resources.epoch() != m_assetEpoch) {
        m_assetEpoch = ctx.resources.epoch();
        stopEverything();
    }

    // Sounds that ran out; a tracked one reads as finished on the pass below.
    m_device.reapFinishedVoices();

    updateListener(ctx);

    ++m_frame;
    const bool simRunning = ctx.clock.getSimDelta() > 0.0f;

    // Every source, not only posed ones, or a 2D source would stay silent with `playing`
    // stuck true. No pose is heard at the origin.
    Scene& scene = ctx.scene;
    scene.forEach<AudioSource>([&](EntityId id, AudioSource& source) {
        const Transform* at = scene.tryGet<Transform>(id);
        const glm::vec3 world = at ? resolvedWorldPosition(scene, id, *at) : glm::vec3(0.0f);
        reconcileSource(ctx, id, source, world, simRunning);
    });

    // Unvisited voices have lost their AudioSource or entity; nobody else will stop them.
    for (auto it = m_voices.begin(); it != m_voices.end(); ) {
        if (it->second.seenOnFrame == m_frame) {
            ++it;
            continue;
        }
        m_device.stopVoice(it->second.voice);
        it = m_voices.erase(it);
    }

    startPendingRequests(ctx);
}

void AudioSystem::shutdown() {
    if (m_events != nullptr) {
        m_events->unsubscribe<PlaySoundEvent>(m_playListener);
        m_events = nullptr;
        m_playListener = 0;
    }
    stopEverything();
    m_device.close();
}

VoiceId AudioSystem::voiceOf(EntityId entity) const {
    const auto it = m_voices.find(entity.slot());
    if (it == m_voices.end() || it->second.entity != entity) return 0;
    return it->second.voice;
}

void AudioSystem::updateListener(FrameContext& ctx) {
    Scene& scene = ctx.scene;

    const EntityId entity = findActiveListener(scene);

    m_hasListener = static_cast<bool>(entity);
    m_device.setListenerActive(m_hasListener);
    if (!m_hasListener) {
        m_device.setMasterVolume(1.0f);
        return;
    }

    const Transform& pose     = scene.get<Transform>(entity);
    const glm::quat  rotation = resolvedWorldRotation(scene, entity, pose);
    m_listenerPosition = resolvedWorldPosition(scene, entity, pose);
    m_device.setListener(m_listenerPosition, Math::computeForward(rotation), Math::computeUp(rotation));
    m_device.setMasterVolume(scene.get<AudioListener>(entity).volume);
}

void AudioSystem::reconcileSource(
    FrameContext& ctx,
    EntityId entity,
    AudioSource& source,
    const glm::vec3& worldPosition,
    bool simRunning
) {
    if (source.playOnStart && !source.started && simRunning) {
        source.started = true;
        source.playing = true;
    }

    auto it = m_voices.find(entity.slot());
    // A slot recycled by an unrelated entity must not inherit this voice.
    if (it != m_voices.end() && it->second.entity != entity) {
        m_device.stopVoice(it->second.voice);
        m_voices.erase(it);
        it = m_voices.end();
    }

    // Re-pointed while playing: restart on the new clip. Ahead of the finished check, so
    // a clip changed as the old one ran out still starts.
    if (it != m_voices.end() && it->second.clip != source.clip) {
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

    // A one-shot that ended; `playing` written back is how gameplay learns it finished.
    if (it != m_voices.end() && !m_device.isVoiceActive(it->second.voice)) {
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

    // Nothing to play: clear the request rather than retry every frame.
    if (!ctx.resources.isAlive(source.clip)) {
        source.playing = false;
        return;
    }

    const AudioClipAsset& clip = ctx.resources.get(source.clip);
    warnIfNoListener(params.spatial);
    warnIfStereoSpatial(clip, params.spatial);

    const VoiceId voice = m_device.play(clip, params);
    if (voice == 0) {
        source.playing = false;
        return;
    }
    m_voices.emplace(entity.slot(), ActiveVoice{entity, source.clip, voice, m_frame});
}

void AudioSystem::startPendingRequests(FrameContext& ctx) {
    // A second buffer: a synchronous emit could grow the queue under the walk; a request
    // made mid-walk waits a frame.
    m_starting.clear();
    m_starting.swap(m_pending);

    for (const PlaySoundEvent& request : m_starting) {
        if (!ctx.resources.isAlive(request.clip)) continue;

        VoiceParams params = request.params;
        // A request cannot be stopped, so it must be able to end by itself.
        params.loop = false;

        // Out of the ear's range: it would hold a capped voice at zero gain for the clip's
        // whole length. A range not past minDistance attenuates nothing, so has no far.
        if (params.spatial && m_hasListener && params.hasFar()
            && glm::distance(params.position, m_listenerPosition) > params.heardMaxDistance()) {
            continue;
        }

        const AudioClipAsset& clip = ctx.resources.get(request.clip);
        warnIfNoListener(params.spatial);
        warnIfStereoSpatial(clip, params.spatial);
        m_device.play(clip, params);
    }
}

void AudioSystem::warnIfNoListener(bool spatial) {
    if (!spatial || m_hasListener || m_warnedNoListener) return;

    m_warnedNoListener = true;
    LOG_WARNING(
        "A positioned sound started with no active AudioListener in the scene - it "
        "is silent until one exists (non-spatial sources are unaffected)"
    );
}

void AudioSystem::warnIfStereoSpatial(const AudioClipAsset& asset, bool spatial) {
    if (!spatial || asset.channels <= 1) return;
    if (!m_warnedStereoClips.insert(asset.name()).second) return;

    LOG_WARNING(
        "A positioned sound is playing the %u-channel clip '%s' - the mixer routes each "
        "channel to the output channel it was authored for and attenuates it there, so "
        "sound in one channel is never heard from the other side however the emitter "
        "moves; positioning wants a mono clip",
        asset.channels,
        asset.name().c_str()
    );
}

void AudioSystem::stopEverything() {
    m_device.stopAllVoices();
    m_voices.clear();
    m_pending.clear();
    m_starting.clear();
    m_warnedNoListener = false;
    m_hasListener      = false;
    m_warnedStereoClips.clear();
}

} // namespace Vkm::Engine
