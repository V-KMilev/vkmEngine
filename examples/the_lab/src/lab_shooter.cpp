#define VKM_LOG_CATEGORY "LAB"

#include "lab_shooter.h"

#include <glm/gtx/quaternion.hpp>

#include "logger.h"

#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "net/prediction/rewind.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"
#include "resource/resource_manager.h"
#include "system/physics/query/query.h"

#include "lab_actions.h"
#include "lab_follow_camera.h"
#include "lab_seat.h"
#include "lab_state.h"
#include "lab_walker.h"

namespace Lab {

namespace {

// Crosses the whole lab, so a miss means nothing was there.
constexpr float SHOT_RANGE = 120.0f;

// What a shot gives a player it knocks down, and a loose prop it strikes, kg m/s.
constexpr float SHOT_PUSH = 45.0f;
constexpr float PROP_PUSH = 12.0f;

// Scorch marks one shooter leaves before the oldest goes.
constexpr size_t MAX_SCORCHES = 40;

// A scorch mark's size across, and how deep into the surface it reaches, metres.
constexpr float SCORCH_SIZE  = 0.32f;
constexpr float SCORCH_DEPTH = 0.3f;

// How fast a tracer's streak flies, m/s, and how far from the muzzle it starts, metres.
constexpr float TRACER_SPEED = 90.0f;
constexpr float TRACER_START = 0.6f;

// A shot shorter than this shows no tracer, metres.
constexpr float SHORTEST_TRACER = 0.05f;

// The shortest a tracer flies, metres, so a point-blank shot still flashes.
constexpr float SHORTEST_FLIGHT = 0.1f;

// A surface's normal this near vertical is a floor or a ceiling.
constexpr float FLOOR_NORMAL = 0.9f;

} // namespace

void LabShooter::onStart() {
    input().define(ACTION_FIRE, { InputBinding::key(GLFW_KEY_F) });
}

glm::vec3 LabShooter::muzzle() {
    const Transform* body = tryGet<Transform>();
    const LabFollowCamera* camera = findBehavior<LabFollowCamera>();
    const float height = camera ? camera->pivotHeight : 0.0f;
    return (body ? body->position : glm::vec3(0.0f)) + Math::WORLD_UP * height;
}

void LabShooter::onFixedUpdate(float) {
    if (!isSimulated() || sitsOutOffline(scene(), net(), entity())) return;
    // Down, the ragdoll has the body and the shot waits until it stands.
    const Ragdoll* ragdoll = tryGet<Ragdoll>();
    if (ragdoll && ragdoll->active) return;
    if (input().pressed(command(), ACTION_FIRE)) fire();
}

void LabShooter::fire() {
    // Authority only: a client draws other players in the past on purpose.
    if (!isAuthority()) return;
    LabScore* score = tryGet<LabScore>();
    if (!score) return;

    const glm::vec3 from = muzzle();
    const glm::vec3 look = Math::computeForward(command().view);
    // The muzzle stands inside the shooter's own capsule.
    QueryFilter filter;
    filter.ignore = entity();
    RayHit hit;
    bool struck = false;
    {
        // Offline there is no history and nothing moves.
        NetSession::Rewind rewound(net(), scene(), entity());
        struck = raycast(scene(), from, look, SHOT_RANGE, hit, filter);
    }

    ++score->shots;
    if (!struck) {
        score->lastShot   = LabShot::Miss;
        score->lastHit    = from + look * SHOT_RANGE;
        score->lastNormal = -look;
        return;
    }
    LOG_INFO(
        "Shot from player entity %u hit entity %u at %.2f m",
        entity().slot(),
        hit.entity.slot(),
        hit.distance
    );
    score->lastHit    = hit.point;
    score->lastNormal = hit.normal;
    Rigidbody* struckBody = scene().tryGet<Rigidbody>(hit.entity);
    const bool scenery = struckBody && struckBody->motion == RigidbodyMotion::Static;
    score->lastShot = scenery ? LabShot::World : LabShot::Prop;

    // Read inside the rewind, acted on in the present.
    const EntityId struckCharacter = HierarchyOperations::findInSelfOrAncestors<Ragdoll>(scene(), hit.entity);
    if (LabWalker* other = findBehavior<LabWalker>(struckCharacter)) {
        other->knockDown(look * SHOT_PUSH, hit.point);
        score->lastShot = LabShot::Player;
        return;
    }
    if (struckBody && struckBody->motion == RigidbodyMotion::Dynamic && struckBody->mass > 0.0f) {
        struckBody->linearVelocity += look * (PROP_PUSH / struckBody->mass);
        Rigidbody::wake(*struckBody);
    }
}

void LabShooter::onUpdate(float) {
    // Every shooter's shots, at every end that draws.
    if (window()) showShot();
}

void LabShooter::showShot() {
    const LabScore* score = tryGet<LabScore>();
    if (!score || !m_shots.rose(score->shots)) return;

    const glm::vec3 from   = muzzle();
    const glm::vec3 path   = score->lastHit - from;
    const float     length = glm::length(path);
    if (length < SHORTEST_TRACER) return;
    const glm::vec3 along = path / length;

    playSoundAt(zap, from);

    // The streak leaves along the shot and dies where it struck.
    if (!tracer.empty()) {
        Transform start;
        start.position = from + along * TRACER_START;
        start.rotation = glm::rotation(Math::WORLD_UP, along);
        const EntityId streak = instantiate(tracer, start);
        if (ParticleEmitter* emitter = scene().tryGet<ParticleEmitter>(streak)) {
            const float flight = glm::max(length - TRACER_START, SHORTEST_FLIGHT) / TRACER_SPEED;
            emitter->speed    = {TRACER_SPEED, TRACER_SPEED};
            emitter->lifetime = {flight, flight};
        }
    }
    if (score->lastShot == LabShot::Miss) return;

    Transform impact;
    impact.position = score->lastHit;
    impact.rotation = glm::rotation(Math::WORLD_UP, score->lastNormal);
    if (!hitEffect.empty()) instantiate(hitEffect, impact);

    // Scenery keeps a mark; a prop moves on and a player gets up.
    if (score->lastShot == LabShot::World) leaveScorch(score->lastHit, score->lastNormal);
}

void LabShooter::leaveScorch(const glm::vec3& at, const glm::vec3& normal) {
    const MaterialHandle mark = resources().find(scorch);
    if (!mark) return;
    const EntityId burn = spawn("Scorch");
    Transform placed;
    placed.position = at;
    // Projected along its forward, into the surface; a floor's up would be that forward.
    const glm::vec3 up = glm::abs(normal.y) > FLOOR_NORMAL ? Math::WORLD_FORWARD : Math::WORLD_UP;
    placed.rotation = Math::lookRotation(-normal, up);
    placed.scale    = {SCORCH_SIZE, SCORCH_SIZE, SCORCH_DEPTH};
    scene().add(burn, std::move(placed));
    Decal decal;
    decal.material = mark;
    scene().add(burn, std::move(decal));
    m_scorches.push_back(burn);
    if (m_scorches.size() > MAX_SCORCHES) {
        if (scene().isAlive(m_scorches.front())) destroy(m_scorches.front());
        m_scorches.pop_front();
    }
}

} // namespace Lab
