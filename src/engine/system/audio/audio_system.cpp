#define VKM_LOG_CATEGORY "AUDIO"

#include "system/audio/audio_system.h"

#include <algorithm>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/clock.h"
#include "core/event/event_bus.h"
#include "core/math/rotation.h"
#include "debug/debug_draw.h"
#include "debug/profiler.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/environment.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

/// From this many playing voices each is re-asked for occlusion every few frames, its turn set by
/// its entity slot: at most this many a frame on average.
constexpr size_t OCCLUSION_VOICES_PER_FRAME = 32;

/// Whether @p source is one the colliders shape, given the scene's acoustics are on.
bool shaped(const AudioSource& source) { return source.spatial && source.acoustics; }

/// The side rays aim half a source's near distance out from it, kept between a hand and an arm.
float spreadAround(float minDistance) { return std::clamp(minDistance * 0.5f, 0.25f, 1.0f); }

/// The pulses running out along the room's rays, as sound leaves the ear and comes back off the walls.
constexpr DebugPulse ROOM_PULSE{0.8f, 6.0f, 5.0f, 0.0f, 0.2f};

/// How bright a room ray glows on its first leg, and the point where a leg strikes.
constexpr float ROOM_GLOW   = 2.0f;
constexpr float STRIKE_GLOW = 3.0f;

/// What a room ray keeps of its color and glow at each bounce: a reflection is quieter than what
/// struck the wall.
constexpr float BOUNCE_KEEPS = 0.6f;

/// How far a room ray's leg that struck nothing is drawn on into the open, in metres, and how faint.
constexpr float ESCAPE_DRAWN = 2.0f;
constexpr float ESCAPE_SHARE = 0.5f;

/// How faint a blocked ray to a sound is drawn past what stopped it.
constexpr float BEYOND_SHARE = 0.3f;

/// The colliders AudioSystem gathered, struck as the acoustics' rays ask: all but the body a sound
/// sits in, which does not block it.
struct GatheredRays {
    const QueryParts& world;
    EntityId          ignore{};

    bool operator()(glm::vec3 origin, glm::vec3 direction, float reach, Acoustics::Strike& strike) const {
        RayHit hit;
        if (!world.raycast(origin, direction, reach, hit, ignore)) return false;
        strike = {hit.distance, hit.point, hit.normal};
        return true;
    }
};

/// The body a sound on @p id sits in: the nearest at or above it the rays strike; none for none.
EntityId bodyOf(const Scene& scene, EntityId id) {
    // Bounded, as a hierarchy walk is: a broken chain cannot hold it.
    for (size_t step = 0; id && step < HierarchyOperations::MAX_DEPTH; ++step) {
        // As the gather takes them: no ray strikes a trigger, a collider off or a dynamic body.
        const Collider*  collider = scene.tryGet<Collider>(id);
        const Rigidbody* body     = scene.tryGet<Rigidbody>(id);
        const bool       struck   = collider && body && collider->enabled && !collider->isTrigger
            && body->motion != RigidbodyMotion::Dynamic;
        if (struck) return id;
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        id = node ? node->parent : EntityId{};
    }
    return {};
}

/// Round the ear as far as a room ray looks, out to each shaped source playing or starting.
Math::AABB acousticReach(Scene& scene, const glm::vec3& ear) {
    const glm::vec3 reach(Acoustics::ROOM_REACH);
    Math::AABB around{ear - reach, ear + reach};
    scene.forEach<AudioSource, Transform>([&](EntityId id, const AudioSource& source, const Transform& at) {
        // A source to start this frame is shaped as it starts, against what is gathered here.
        const bool sounding = source.playing || (source.playOnStart && !source.started);
        if (sounding && shaped(source)) around.expand(resolvedWorldPosition(scene, id, at));
    });
    return around;
}

} // namespace

void AudioSystem::init(FrameContext& ctx) {
    // Starting at 0 would read as a graph replacement on the first frame.
    m_assetEpoch = ctx.resources.epoch();
    m_worldEpoch = ctx.scene.epoch();

    // A host with no usable device still runs; open() says why.
    if (!m_silent) m_device.open();

    // Collect here, start in update(): emit() is synchronous, and a voice must not be
    // created mid-way through another system's walk.
    m_events = &ctx.events;
    m_playListener = m_events->subscribe<PlaySoundEvent>(
        [this, scene = &ctx.scene](const PlaySoundEvent& request) {
            m_pending.push_back({request, scene->epoch()});
        }
    );
}

void AudioSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("AudioSystem");

    // Runs whole on an unopened device, which behaves as if every sound were zero-length,
    // so `playing` still clears; see docs/reference/audio.md.

    // The asset graph was replaced under us (see ResourceManager::epoch), or the world was.
    if (ctx.resources.epoch() != m_assetEpoch) {
        m_assetEpoch = ctx.resources.epoch();
        m_worldEpoch = ctx.scene.epoch();
        stopEverything();
    } else if (ctx.scene.epoch() != m_worldEpoch) {
        m_worldEpoch = ctx.scene.epoch();
        endWorld();
    }

    // Sounds that ran out; a tracked one reads as finished on the pass below.
    m_device.reapFinishedVoices();

    updateListener(ctx);
    updateAcoustics(ctx);

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
    drawAcoustics(ctx);
}

void AudioSystem::drawAcoustics(FrameContext& ctx) const {
    if (!ctx.debug || !m_acoustics || !ctx.debug->shown(DebugCategory::Acoustics)) return;
    DebugDraw& draw = *ctx.debug;
    // In the view's category, so the host's toggle governs every line of it.
    const auto drawn = [](DebugPalette::Color shade) {
        return DebugStyle(shade).in(DebugCategory::Acoustics);
    };
    const DebugPalette::Color color = DebugPalette::of(DebugCategory::Acoustics);

    // The room's rays from where the ear was when each was cast, each bounce dimmer, the window's
    // pulses out of step with one another.
    const float window = static_cast<float>(Acoustics::ROOM_WINDOW);
    for (size_t slot = 0; slot < m_roomFilled; ++slot) {
        const Acoustics::RoomRay& ray = m_roomRays[slot];
        const float start = ROOM_PULSE.spacing * static_cast<float>(slot) / window;
        float share = 1.0f;
        float along = 0.0f;
        for (size_t leg = 0; leg < static_cast<size_t>(ray.strikes); ++leg) {
            DebugPulse pulse = ROOM_PULSE;
            pulse.phase      = start - along;  // carries on round the bounce
            const DebugStyle style = drawn(color.faded(share));
            draw.pulse(&ray.path[leg], 2, DebugStyle(style).glowing(ROOM_GLOW * share), pulse);
            draw.point(ray.path[leg + 1], DebugStyle(style).glowing(STRIKE_GLOW * share));
            along += glm::distance(ray.path[leg], ray.path[leg + 1]);
            share *= BOUNCE_KEEPS;
        }
        if (ray.escape != glm::vec3(0.0f)) {
            const glm::vec3& from = ray.path[static_cast<size_t>(ray.strikes)];
            draw.line(from, from + ray.escape * ESCAPE_DRAWN, drawn(color.faded(share * ESCAPE_SHARE)));
        }
    }

    // Each shaped voice's rays from the ear: whole where nothing stands between, red up to what
    // stops one and faint past it.
    const DebugStyle clear   = drawn(color);
    const DebugStyle blocked = drawn(DebugPalette::OCCLUDED);
    const DebugStyle beyond  = drawn(DebugPalette::OCCLUDED.faded(BEYOND_SHARE));
    const auto show = [&](const glm::vec3& target, const Acoustics::Strike* stopped) {
        if (!stopped) {
            draw.line(m_listenerPosition, target, clear);
            return;
        }
        draw.line(m_listenerPosition, stopped->point, blocked);
        draw.point(stopped->point, blocked);
        draw.line(stopped->point, target, beyond);
    };
    Scene& scene = ctx.scene;
    for (const auto& [slot, active] : m_voices) {
        const AudioSource* source = scene.tryGet<AudioSource>(active.entity);
        const Transform*   at     = scene.tryGet<Transform>(active.entity);
        if (!source || !at || !shaped(*source)) continue;
        const GatheredRays rays{m_world, bodyOf(scene, active.entity)};
        const glm::vec3    position = resolvedWorldPosition(scene, active.entity, *at);
        const float        radius   = spreadAround(source->minDistance);
        Acoustics::castOcclusionRays(rays, m_listenerPosition, position, radius, show);
    }
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

void AudioSystem::updateAcoustics(FrameContext& ctx) {
    Scene& scene = ctx.scene;
    const AcousticsSettings& settings = scene.environment().acoustics;
    m_frameSeconds   = ctx.clock.getDeltaTime();
    // A host with no sound to hear - a server - casts no rays.
    m_acoustics      = settings.enabled && m_hasListener && m_device.isOpen();
    m_reverbStrength = settings.reverb;

    Acoustics::Reverb target;
    if (m_acoustics) {
        // Static and kinematic bodies only: a crate rolling past is not a wall.
        QueryFilter statics;
        statics.hitDynamic = false;
        // Out to each request still to start too, shaped as it starts against what is gathered here.
        Math::AABB reach = acousticReach(scene, m_listenerPosition);
        for (const Asked& asked : m_pending) {
            const PlaySoundEvent& request = asked.request;
            if (request.params.spatial && request.acoustics) reach.expand(request.params.position);
        }
        m_world.gather(scene, statics, reach);

        const GatheredRays rays{m_world};
        for (size_t i = 0; i < Acoustics::ROOM_RAYS_PER_FRAME; ++i) {
            const glm::vec3 direction = Acoustics::roomDirection(m_roomNext);
            m_roomRays[m_roomNext] = Acoustics::traceRoomRay(rays, m_listenerPosition, direction);
            m_roomNext   = (m_roomNext + 1) % Acoustics::ROOM_WINDOW;
            m_roomFilled = std::min(m_roomFilled + 1, Acoustics::ROOM_WINDOW);
        }
        const Acoustics::Room room = Acoustics::estimateRoom(m_roomRays.data(), m_roomFilled);
        target = Acoustics::reverbFor(room, m_reverbStrength);
    } else {
        // Refilled from the first slot, as the estimate reads the window's first m_roomFilled.
        m_world      = {};
        m_roomNext   = 0;
        m_roomFilled = 0;
    }

    const float ease = Acoustics::ROOM_SECONDS;
    m_reverb.wet = Acoustics::approach(m_reverb.wet, target.wet, m_frameSeconds, ease);
    // The decay is held while the level fades, so a reverb fading out does not shorten as it goes.
    if (target.decaySeconds > 0.0f) {
        const float from = m_reverb.decaySeconds > 0.0f ? m_reverb.decaySeconds : target.decaySeconds;
        m_reverb.decaySeconds = Acoustics::approach(from, target.decaySeconds, m_frameSeconds, ease);
    }
    m_device.setRoomReverb(m_reverb.decaySeconds, m_reverb.wet);
}

float AudioSystem::blockedShare(const glm::vec3& position, float minDistance, EntityId body) const {
    const GatheredRays rays{m_world, body};
    return Acoustics::blockedShare(rays, m_listenerPosition, position, spreadAround(minDistance));
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

    const bool acoustic = m_acoustics && shaped(source);

    VoiceParams params;
    params.volume      = source.volume;
    params.pitch       = source.pitch;
    params.loop        = source.loop;
    params.spatial     = source.spatial;
    params.position    = worldPosition;
    params.minDistance = source.minDistance;
    params.maxDistance = source.maxDistance;
    params.rolloff     = source.rolloff;
    params.priority    = source.priority;
    params.reverb      = acoustic && m_reverbStrength > 0.0f;

    if (it != m_voices.end()) {
        ActiveVoice& active = it->second;
        active.seenOnFrame = m_frame;
        if (!acoustic) {
            active.blocked = 0.0f;
        } else {
            // Re-asked in turn from OCCLUSION_VOICES_PER_FRAME voices on: each every few frames.
            const size_t stride = 1 + m_voices.size() / OCCLUSION_VOICES_PER_FRAME;
            if ((entity.slot() + m_frame) % stride == 0) {
                active.blocked = blockedShare(worldPosition, source.minDistance, bodyOf(ctx.scene, entity));
            }
        }
        const float ease = Acoustics::OCCLUSION_SECONDS;
        active.occlusion = Acoustics::approach(active.occlusion, active.blocked, m_frameSeconds, ease);
        params.occlusion = active.occlusion;
        m_device.updateVoice(active.voice, params);
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

    // A sound starts as occluded as it is, rather than easing in from open.
    const EntityId body = bodyOf(ctx.scene, entity);
    params.occlusion    = acoustic ? blockedShare(worldPosition, source.minDistance, body) : 0.0f;
    const VoiceId voice = m_device.play(clip, params);
    if (voice == 0) {
        source.playing = false;
        return;
    }
    const float occlusion = params.occlusion;
    m_voices.emplace(entity.slot(), ActiveVoice{entity, source.clip, voice, m_frame, occlusion, occlusion});
}

void AudioSystem::startPendingRequests(FrameContext& ctx) {
    // A second buffer: a synchronous emit could grow the queue under the walk; a request
    // made mid-walk waits a frame.
    m_starting.clear();
    m_starting.swap(m_pending);

    for (const Asked& asked : m_starting) {
        const PlaySoundEvent& request = asked.request;
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

        // Shaped once, where it is made: a request neither moves nor is updated. What shapes it
        // is this system's to say, whatever the request carried.
        params.occlusion = 0.0f;
        params.reverb    = false;
        if (m_acoustics && params.spatial && request.acoustics) {
            const EntityId body = bodyOf(ctx.scene, request.from);
            params.occlusion    = blockedShare(params.position, params.minDistance, body);
            params.reverb       = m_reverbStrength > 0.0f;
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
    m_pending.clear();
    m_starting.clear();
    endWorld();
}

void AudioSystem::endWorld() {
    m_device.stopAllVoices();
    m_voices.clear();
    // What the old world asked for as it went - a behavior's onDestroy at Stop - goes with it.
    const auto old = [this](const Asked& asked) { return asked.world != m_worldEpoch; };
    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), old), m_pending.end());
    m_warnedNoListener = false;
    m_hasListener      = false;
    m_warnedStereoClips.clear();
    // The old world's room is not the new one's, and its colliders are going.
    m_world      = {};
    m_roomNext   = 0;
    m_roomFilled = 0;
    m_reverb     = {};
}

} // namespace Vkm::Engine
