#include "lab_walker.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "core/math/easing.h"
#include "core/math/rotation.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "net/wire/protocol.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"
#include "resource/resource_manager.h"
#include "system/script/script_component.h"

#include "lab_actions.h"
#include "lab_jump_pad.h"
#include "lab_seat.h"
#include "lab_switch.h"

namespace Lab {

namespace {

void installBindings(InputMap& map) {
    const auto key = [](int code) {
        return InputBinding{InputSource::Key, code, 1.0f};
    };
    map.define(ACTION_FORWARD, { key(GLFW_KEY_W), key(GLFW_KEY_UP) });
    map.define(ACTION_BACK,    { key(GLFW_KEY_S), key(GLFW_KEY_DOWN) });
    map.define(ACTION_LEFT,    { key(GLFW_KEY_A), key(GLFW_KEY_LEFT) });
    map.define(ACTION_RIGHT,   { key(GLFW_KEY_D), key(GLFW_KEY_RIGHT) });
    map.define(ACTION_WALK,    { key(GLFW_KEY_LEFT_SHIFT) });
    map.define(ACTION_JUMP,    { key(GLFW_KEY_SPACE) });
    map.define(ACTION_USE,     { key(GLFW_KEY_E) });
}

// How far the push spreads from the hit, in metres, before it falls to half: the struck limbs take most.
constexpr float PUSH_REACH = 0.35f;

// An analog stick rests slightly off centre; without this the character creeps.
constexpr float STICK_DEADZONE = 0.1f;

// Below this a character is standing rather than walking, m/s. Nonzero: a resting body keeps
// residual solver velocity, so zero would flicker.
constexpr float STANDING_SPEED = 0.35f;

// Below this share of the run speed a moving character walks rather than runs.
constexpr float WALKING_SHARE = 0.6f;

} // namespace

void LabWalker::onStart() {
    installBindings(input());
    m_animator = HierarchyOperations::findInSelfOrDescendants<Animator>(scene(), entity());
    // Offline nobody joins: the seat is taken as play starts.
    if (net().isOffline() && isOfflineSeat(scene(), entity())) seatPlayer(scene(), entity());
}

void LabWalker::play(const AssetRef<AnimationClipAsset>& clip) {
    if (clip.name == m_playing) return;
    Animator* animator = scene().tryGet<Animator>(m_animator);
    const AnimationClipHandle handle = resources().find(clip);
    if (!animator || !handle) return;

    // Every lab clip loops, the jump included (held while airborne).
    const bool looping = true;
    Animator::crossFadeTo(*animator, handle, fadeSeconds, looping);
    m_playing = clip.name;
}

void LabWalker::chooseClip() {
    // Not on replay: it would pick a clip from a past state and the live tick
    // would pick it back, visibly fighting the animation.
    if (isReplaying()) return;

    const Rigidbody*           body = tryGet<Rigidbody>();
    const CharacterController* walk = tryGet<CharacterController>();
    if (!body || !walk) return;

    const glm::vec3 flat  = {body->linearVelocity.x, 0.0f, body->linearVelocity.z};
    const float     speed = glm::length(flat);

    if (!walk->grounded)                        play(jumpClip);
    else if (speed < STANDING_SPEED)            play(idleClip);
    else if (speed < runSpeed * WALKING_SHARE)  play(walkClip);
    else                                        play(runClip);
}

void LabWalker::onFixedUpdate(float dt) {
    CharacterController* controller = tryGet<CharacterController>();
    if (!controller) return;

    chooseClip();

    // The authority stands a knocked-down character back up.
    if (m_downLeft > 0.0f && isAuthority(net().role())) {
        m_downLeft -= dt;
        Ragdoll* ragdoll = tryGet<Ragdoll>();
        if (m_downLeft <= 0.0f && ragdoll) ragdoll->active = false;
    }

    if (!isSimulated()) return;

    // Zeroed: after a session closes, a walker holding its last input would keep going.
    if (sitsOutOffline(scene(), net(), entity())) {
        controller->moveInput = glm::vec3(0.0f);
        return;
    }

    // The tick's command, not the device: reading the device drops a tap between
    // ticks. On a server it is what this entity's player sent.
    const InputCommand& tick = command();
    const InputMap&     map  = input();
    const glm::vec2 stick = {
        map.axis(tick, ACTION_RIGHT)   - map.axis(tick, ACTION_LEFT),
        map.axis(tick, ACTION_FORWARD) - map.axis(tick, ACTION_BACK)
    };

    // Camera-relative and flattened, from the command so a replay agrees.
    glm::vec3 forward = Math::WORLD_FORWARD;
    glm::vec3 right   = Math::WORLD_RIGHT;

    // Both or neither: a view forward beside a default right describes no view.
    // The length test catches looking straight down.
    const glm::vec3 look = Math::computeForward(tick.view);
    const glm::vec3 side = Math::computeRight(tick.view);
    const glm::vec3 flatLook = {look.x, 0.0f, look.z};
    const glm::vec3 flatSide = {side.x, 0.0f, side.z};
    if (glm::dot(flatLook, flatLook) > glm::epsilon<float>()
        && glm::dot(flatSide, flatSide) > glm::epsilon<float>()) {
        forward = glm::normalize(flatLook);
        right   = glm::normalize(flatSide);
    }

    const float speed = map.held(tick, ACTION_WALK) ? walkSpeed : runSpeed;

    glm::vec3 move = right * stick.x + forward * stick.y;
    const float lengthSq = glm::dot(move, move);
    const bool moving = lengthSq > STICK_DEADZONE * STICK_DEADZONE;
    move = moving ? glm::normalize(move) * speed : glm::vec3(0.0f);

    // Down, the ragdoll has the body; the controller waits until it stands.
    const Ragdoll* ragdoll = tryGet<Ragdoll>();
    if (ragdoll && ragdoll->active) {
        controller->moveInput = glm::vec3(0.0f);
        return;
    }

    controller->moveInput = move;
    if (map.pressed(tick, ACTION_JUMP)) controller->jumpRequested = true;
    if (map.pressed(tick, ACTION_USE))  use();
    bounce();

    // Aims the entity (forward -Z); a model facing another way is corrected on its
    // rig node - see docs/reference/animation.md.
    if (Transform* body = moving ? tryGet<Transform>() : nullptr) {
        body->rotation = glm::slerp(body->rotation, Math::lookRotation(move), damp(turnSpeed, dt));
    }
}

void LabWalker::knockDown(const glm::vec3& momentum, const glm::vec3& at) {
    if (!isAuthority(net().role())) return;
    Ragdoll* ragdoll = tryGet<Ragdoll>();
    if (!ragdoll || ragdoll->active || ragdoll->bones.empty()) return;
    ragdoll->active = true;
    m_downLeft      = standUpAfter > 0.0f ? standUpAfter : 0.0f;

    // Shared out so the bones nearest the blow move most: the rest follow through the joints, and
    // the body tumbles rather than sliding off whole.
    float weighted = 0.0f;
    for (const RagdollBone& bone : ragdoll->bones) {
        const Rigidbody* limb = scene().tryGet<Rigidbody>(bone.body);
        const Transform* pose = scene().tryGet<Transform>(bone.body);
        if (!limb || !pose) continue;
        const float d = glm::distance(resolvedWorldPosition(scene(), bone.body, *pose), at) / PUSH_REACH;
        weighted += limb->mass / (1.0f + d * d);
    }
    if (weighted <= 0.0f) return;
    for (const RagdollBone& bone : ragdoll->bones) {
        Rigidbody* limb = scene().tryGet<Rigidbody>(bone.body);
        const Transform* pose = scene().tryGet<Transform>(bone.body);
        if (!limb || !pose) continue;
        const float d = glm::distance(resolvedWorldPosition(scene(), bone.body, *pose), at) / PUSH_REACH;
        limb->linearVelocity += momentum / (weighted * (1.0f + d * d));
    }
}

void LabWalker::use() {
    if (!isAuthority(net().role())) return;
    const Transform* body = tryGet<Transform>();
    if (!body) return;

    // The nearest switch in reach, across the ground; ties to the lower slot.
    LabSwitch* nearest = nullptr;
    EntityId   nearestEntity{};
    float      best    = 0.0f;
    scene().forEach<ScriptComponent, Transform>([&](EntityId id, ScriptComponent&, Transform& at) {
        LabSwitch* button = findBehavior<LabSwitch>(id);
        if (!button) return;
        const glm::vec3 where  = resolvedWorldPosition(scene(), id, at) - body->position;
        const float     across = glm::length(glm::vec2(where.x, where.z));
        if (across > button->reach) return;
        if (nearest && (across > best || (across == best && id.slot() > nearestEntity.slot()))) return;
        nearest       = button;
        nearestEntity = id;
        best          = across;
    });
    if (nearest) nearest->press();
}

void LabWalker::bounce() {
    const Rigidbody* body = tryGet<Rigidbody>();
    const EntityId   pad  = body && body->supported ? body->supportBody : EntityId{};
    const bool       fresh = pad != m_lastPad;
    m_lastPad = pad;
    LabJumpPad* launcher = findBehavior<LabJumpPad>(pad);
    if (!launcher || !fresh) return;

    Rigidbody* mover = tryGet<Rigidbody>();
    mover->linearVelocity.y = launcher->launchSpeed;
    if (const Transform* padAt = scene().tryGet<Transform>(pad)) {
        mover->linearVelocity += Math::computeForward(padAt->rotation) * launcher->forward;
    }
    launcher->launched();
}

} // namespace Lab
