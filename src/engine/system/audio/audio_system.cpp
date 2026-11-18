#define VKM_LOG_CATEGORY "AUDIO"

#include "system/audio/audio_system.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "vkm_log/assert.h"
#include "vkm_log/logger.h"

#include "core/clock.h"
#include "base/event/event_bus.h"
#include "base/math/rotation.h"
#include "debug/draw/debug_draw.h"
#include "base/profile/profiler.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/settings/environment.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

constexpr size_t OCCLUSION_VOICES_PER_LISTEN = 32;  ///< From this many playing voices each is re-asked for
                                                    ///< occlusion every few listens, its turn set by its
                                                    ///< entity slot: at most this many a listen on average.

/// What the acoustics' rays strike: static and kinematic bodies, as a crate rolling past is not a wall.
QueryFilter statics() {
    QueryFilter filter;
    filter.hitDynamic = false;
    return filter;
}

/// Whether @p box holds @p point.
bool holds(const Math::AABB& box, const glm::vec3& point) {
    return glm::all(glm::greaterThanEqual(point, box.min)) && glm::all(glm::lessThanEqual(point, box.max));
}

/// Whether @p source is one the colliders shape, given the scene's acoustics are on.
bool shaped(const AudioSource& source) { return source.spatial && source.acoustics; }

/// Whether @p source plays, or starts this frame.
bool sounding(const AudioSource& source) { return source.playing || (source.playOnStart && !source.started); }

/// The side rays aim half a source's near distance out from it, kept between a hand and an arm.
float spreadAround(float minDistance) { return std::clamp(minDistance * 0.5f, 0.25f, 1.0f); }

/**
 * @brief @p rolloff as the mixer reads it.
 *
 * @param rolloff An AudioSource's rolloff.
 * @return The mixer's rolloff.
 */
Audio::Rolloff mixerRolloffOf(AudioRolloff rolloff) {
    switch (rolloff) {
        case AudioRolloff::Linear:  return Audio::Rolloff::Linear;
        case AudioRolloff::Inverse: return Audio::Rolloff::Inverse;
        case AudioRolloff::Count:   break;
    }
    VKM_ASSERT(false, "Count is no rolloff");
    return Audio::Rolloff::Linear;
}

/// Whether a voice of @p params is past its far distance from @p ear, so silent; one with none never is.
bool outOfEarshot(const Audio::VoiceParams& params, const glm::vec3& ear) {
    return params.hasFar() && glm::distance(params.position, ear) > params.heardMaxDistance();
}

constexpr float BOUNCE_KEEPS = 0.6f;  ///< What a room ray keeps of its color at each bounce: a reflection is
                                      ///< quieter than what struck the wall.

constexpr float ESCAPE_DRAWN = 2.0f;  ///< How far a room ray's leg that struck nothing is drawn on into the
                                      ///< open, in meters, and how faint.
constexpr float ESCAPE_SHARE = 0.5f;

constexpr float BEYOND_SHARE = 0.3f;  ///< How faint a blocked ray to a sound is drawn past what stopped it.

/// The colliders AudioSystem gathered, struck as the acoustics' rays ask: all but the body a sound
/// sits in, which does not block it.
struct GatheredRays {
    const QueryParts& world;
    EntityId          ignore{};

    bool operator()(
        glm::vec3 origin,
        glm::vec3 direction,
        float reach,
        Audio::Acoustics::Strike& strike
    ) const {
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
        const Collider* collider = scene.tryGet<Collider>(id);
        const Rigidbody* body = scene.tryGet<Rigidbody>(id);
        const bool struck = collider && body && collider->enabled && !collider->isTrigger
            && body->motion != RigidbodyMotion::Dynamic;
        if (struck) return id;
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        id = node ? node->parent : EntityId{};
    }
    return {};
}

/// Round the ear as far as a room ray looks, out to each shaped source playing or starting.
Math::AABB acousticReach(Scene& scene, const glm::vec3& ear) {
    const glm::vec3 reach(Audio::Acoustics::ROOM_REACH);
    Math::AABB around{ear - reach, ear + reach};
    scene.forEach<AudioSource, Transform>([&](EntityId id, const AudioSource& source, const Transform& at) {
        // A source to start this frame is shaped as it starts, against what is gathered here.
        if (sounding(source) && shaped(source)) around.expand(resolvedWorldPosition(scene, id, at));
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
    const Listen listen = updateAcoustics(ctx);

    ++m_frame;
    const bool simRunning = ctx.clock.getSimDelta() > 0.0f;

    // Every source, not only posed ones, or a 2D source would stay silent with `playing`
    // stuck true. No pose is heard at the origin.
    Scene& scene = ctx.scene;
    scene.forEach<AudioSource>([&](EntityId id, AudioSource& source) {
        const Transform* at   = scene.tryGet<Transform>(id);
        const glm::vec3 world = at ? resolvedWorldPosition(scene, id, *at) : glm::vec3(0.0f);
        reconcileSource(ctx, listen, id, source, world, simRunning);
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

    startPendingRequests(ctx, listen);
    drawAcoustics(ctx, listen);
}

void AudioSystem::drawAcoustics(FrameContext& ctx, const Listen& listen) const {
    if (!ctx.debug || !listen.on || !ctx.debug->shown(DebugCategory::Acoustics)) return;
    DebugDraw& draw = *ctx.debug;
    // In the view's category, so the host's toggle governs every line of it.
    const auto drawn = [](DebugPalette::Color shade) {
        return DebugStyle(shade).in(DebugCategory::Acoustics);
    };
    const DebugPalette::Color color = DebugPalette::of(DebugCategory::Acoustics);

    // The room's rays from where the ear was when each was cast, each bounce dimmer.
    for (size_t slot = 0; slot < m_roomFilled; ++slot) {
        const Audio::Acoustics::RoomRay& ray = m_roomRays[slot];
        float share = 1.0f;
        for (size_t leg = 0; leg < static_cast<size_t>(ray.strikes); ++leg) {
            const DebugStyle style = drawn(color.faded(share));
            draw.line(ray.path[leg], ray.path[leg + 1], style);
            draw.point(ray.path[leg + 1], style);
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
    const auto show = [&](const glm::vec3& target, const Audio::Acoustics::Strike* stopped) {
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
        const Transform* at = scene.tryGet<Transform>(active.entity);
        if (!source || !at || !shaped(*source)) continue;
        const GatheredRays rays{m_world, bodyOf(scene, active.entity)};
        const glm::vec3 position = resolvedWorldPosition(scene, active.entity, *at);
        const float radius = spreadAround(source->minDistance);
        Audio::Acoustics::castOcclusionRays(rays, m_listenerPosition, position, radius, show);
    }
}

void AudioSystem::shutdown() {
    if (m_events != nullptr) {
        m_events->unsubscribe<PlaySoundEvent>(m_playListener);
        m_events       = nullptr;
        m_playListener = 0;
    }
    stopEverything();
    m_device.close();
}

Audio::VoiceId AudioSystem::voiceOf(EntityId entity) const {
    const auto it = m_voices.find(entity.slot());
    if (it == m_voices.end() || it->second.entity != entity) return 0;
    return it->second.voice;
}

void AudioSystem::updateListener(FrameContext& ctx) {
    Scene& scene = ctx.scene;

    // With no listener the eye hears, at unity: a game's sounds play before it places an ear.
    const EntityId listener = findActiveListener(scene);
    const EntityId entity   = listener ? listener : ctx.camera.of(scene);
    const float volume = listener ? scene.get<AudioListener>(listener).volume : 1.0f;

    m_hasListener = static_cast<bool>(entity);
    m_device.setListenerActive(m_hasListener);
    m_device.setMasterVolume(volume);
    if (!m_hasListener) return;

    const Transform& pose     = scene.get<Transform>(entity);
    const glm::quat  rotation = resolvedWorldRotation(scene, entity, pose);
    m_listenerPosition = resolvedWorldPosition(scene, entity, pose);
    m_device.setListener(m_listenerPosition, Math::computeForward(rotation), Math::computeUp(rotation));
}

AudioSystem::Listen AudioSystem::updateAcoustics(FrameContext& ctx) {
    Scene& scene = ctx.scene;
    const AcousticsSettings& settings = scene.environment().acoustics;
    Listen listen;
    listen.seconds = ctx.clock.getDeltaTime();
    // A host with no sound to hear - a server - casts no rays.
    listen.on     = settings.enabled && m_hasListener && m_device.isOpen();
    listen.reverb = settings.reverb;

    // The room shapes only a shaped sound, so with none playing or starting the ear does not listen
    // and the room it last heard holds. A sound after silence listens as it starts, against where the
    // ear is now.
    const float period  = 1.0f / Audio::Acoustics::LISTENS_A_SECOND;
    const bool  hearing = listen.on && hearsShaped(scene);
    if (hearing) {
        m_sinceListen += listen.seconds;
        const bool due = m_roomFilled == 0 || m_sinceListen >= period;
        listen.now = !m_hearing || due;
    }
    m_hearing = hearing;
    if (listen.now) {
        // The remainder over a period carries, so the rate holds at a frame rate it does not divide; the
        // periods a long frame spanned are dropped, not made up in a burst.
        m_sinceListen = std::fmod(m_sinceListen, period);
        ++m_listens;
        // Out to each request still to start too, shaped as it starts against what is gathered here.
        m_gathered = acousticReach(scene, m_listenerPosition);
        for (const Asked& asked : m_pending) {
            const PlaySoundEvent& request = asked.request;
            if (request.params.spatial && request.acoustics) m_gathered.expand(request.params.position);
        }
        m_world.gather(scene, statics(), m_gathered);

        const GatheredRays rays{m_world};
        for (size_t i = 0; i < Audio::Acoustics::ROOM_RAYS_PER_LISTEN; ++i) {
            const glm::vec3 direction = Audio::Acoustics::roomDirection(m_roomNext);
            m_roomRays[m_roomNext] = Audio::Acoustics::traceRoomRay(rays, m_listenerPosition, direction);
            m_roomNext             = (m_roomNext + 1) % Audio::Acoustics::ROOM_WINDOW;
            m_roomFilled           = std::min(m_roomFilled + 1, Audio::Acoustics::ROOM_WINDOW);
        }
        const Audio::Acoustics::Room room = Audio::Acoustics::estimateRoom(m_roomRays.data(), m_roomFilled);
        m_heard = Audio::Acoustics::reverbFor(room, listen.reverb);
    } else if (!listen.on) {
        // Refilled from the first slot, as the estimate reads the window's first m_roomFilled.
        m_world      = {};
        m_gathered   = Math::AABB::empty();
        m_roomNext   = 0;
        m_roomFilled = 0;
        m_heard      = {};
    }

    const float ease = Audio::Acoustics::ROOM_SECONDS;
    m_reverb.wet = Audio::Acoustics::approach(m_reverb.wet, m_heard.wet, listen.seconds, ease);
    // The decay is held while the level fades, so a reverb fading out does not shorten as it goes.
    if (m_heard.decaySeconds > 0.0f) {
        const float from = m_reverb.decaySeconds > 0.0f ? m_reverb.decaySeconds : m_heard.decaySeconds;
        m_reverb.decaySeconds = Audio::Acoustics::approach(from, m_heard.decaySeconds, listen.seconds, ease);
    }
    m_device.setRoomReverb(m_reverb.decaySeconds, m_reverb.wet);
    return listen;
}

bool AudioSystem::hearsShaped(Scene& scene) const {
    for (const Asked& asked : m_pending) {
        if (asked.request.params.spatial && asked.request.acoustics) return true;
    }
    bool heard = false;
    scene.forEach<AudioSource>([&](EntityId, const AudioSource& source) {
        heard = heard || (sounding(source) && shaped(source));
    });
    return heard;
}

void AudioSystem::reach(Scene& scene, const glm::vec3& position) {
    if (holds(m_gathered, position)) return;
    m_gathered.expand(position);
    m_world.gather(scene, statics(), m_gathered);
}

float AudioSystem::blockedShare(const glm::vec3& position, float minDistance, EntityId body) const {
    const GatheredRays rays{m_world, body};
    return Audio::Acoustics::blockedShare(rays, m_listenerPosition, position, spreadAround(minDistance));
}

void AudioSystem::reconcileSource(
    FrameContext& ctx,
    const Listen& listen,
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

    const bool acoustic = listen.on && shaped(source);

    Audio::VoiceParams params;
    params.volume      = source.volume;
    params.pitch       = source.pitch;
    params.loop        = source.loop;
    params.spatial     = source.spatial;
    params.position    = worldPosition;
    params.minDistance = source.minDistance;
    params.maxDistance = source.maxDistance;
    params.rolloff     = mixerRolloffOf(source.rolloff);
    params.priority    = source.priority;
    params.reverb      = acoustic && listen.reverb > 0.0f;

    if (it != m_voices.end()) {
        ActiveVoice& active = it->second;
        active.seenOnFrame = m_frame;
        if (!acoustic) {
            active.blocked = 0.0f;
        } else {
            // Re-asked as the ear listens, in turn from OCCLUSION_VOICES_PER_LISTEN voices on: each
            // every few listens. One out of earshot is silent, so is not asked.
            const size_t stride = 1 + m_voices.size() / OCCLUSION_VOICES_PER_LISTEN;
            const bool   turn   = listen.now && (entity.slot() + m_listens) % stride == 0;
            if (turn && !outOfEarshot(params, m_listenerPosition)) {
                active.blocked = blockedShare(worldPosition, source.minDistance, bodyOf(ctx.scene, entity));
            }
        }
        const float ease = Audio::Acoustics::OCCLUSION_SECONDS;
        active.occlusion = Audio::Acoustics::approach(active.occlusion, active.blocked, listen.seconds, ease);
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
    if (acoustic) reach(ctx.scene, worldPosition);
    params.occlusion = acoustic ? blockedShare(worldPosition, source.minDistance, body) : 0.0f;
    const Audio::VoiceId voice = m_device.play(clip, params);
    if (voice == 0) {
        source.playing = false;
        return;
    }
    const float occlusion = params.occlusion;
    m_voices.emplace(entity.slot(), ActiveVoice{entity, source.clip, voice, m_frame, occlusion, occlusion});
}

void AudioSystem::startPendingRequests(FrameContext& ctx, const Listen& listen) {
    // A second buffer: a synchronous emit could grow the queue under the walk; a request
    // made mid-walk waits a frame.
    m_starting.clear();
    m_starting.swap(m_pending);

    for (const Asked& asked : m_starting) {
        const PlaySoundEvent& request = asked.request;
        if (!ctx.resources.isAlive(request.clip)) continue;

        Audio::VoiceParams params = request.params;
        // A request cannot be stopped, so it must be able to end by itself.
        params.loop = false;

        // Out of the ear's range: it would hold a capped voice at zero gain for the clip's
        // whole length.
        if (params.spatial && m_hasListener && outOfEarshot(params, m_listenerPosition)) continue;

        // Shaped once, where it is made: a request neither moves nor is updated. What shapes it
        // is this system's to say, whatever the request carried.
        params.occlusion = 0.0f;
        params.reverb    = false;
        if (listen.on && params.spatial && request.acoustics) {
            const EntityId body = bodyOf(ctx.scene, request.from);
            reach(ctx.scene, params.position);
            params.occlusion    = blockedShare(params.position, params.minDistance, body);
            params.reverb       = listen.reverb > 0.0f;
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
        "A positioned sound started with no active AudioListener or camera in the scene - it "
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
    m_gathered   = Math::AABB::empty();
    m_roomNext   = 0;
    m_roomFilled = 0;
    m_reverb     = {};
    m_heard      = {};
}

} // namespace Vkm::Engine
