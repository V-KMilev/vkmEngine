#define VKM_LOG_CATEGORY "LAB"

#include "lab_walker.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/hierarchy_operations.h"
#include "net/net_session.h"
#include "net/prediction/rewind.h"
#include "net/wire/protocol.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"
#include "resource/resource_manager.h"
#include "system/physics/query/query.h"
#include "system/script/script_component.h"

namespace Lab {

namespace {

constexpr const char* ACTION_FORWARD = "Move/Forward";
constexpr const char* ACTION_BACK    = "Move/Back";
constexpr const char* ACTION_LEFT    = "Move/Left";
constexpr const char* ACTION_RIGHT   = "Move/Right";
constexpr const char* ACTION_WALK    = "Walk";
constexpr const char* ACTION_JUMP    = "Jump";
constexpr const char* ACTION_PROBE   = "Probe";
constexpr const char* ACTION_LOOK    = "Look";

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
    map.define(ACTION_PROBE,   { key(GLFW_KEY_F) });
    map.define(ACTION_LOOK,    { InputBinding{InputSource::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, 1.0f} });
}

constexpr float EYE_HEIGHT = 1.6f;

// Crosses the course, so a miss means nothing was there.
constexpr float PROBE_RANGE = 80.0f;

// An analog stick rests slightly off centre; without this the character creeps.
constexpr float STICK_DEADZONE = 0.1f;

/**
 * @brief Below this a character is standing rather than walking, in metres a second.
 *
 * Nonzero: a resting body keeps residual solver velocity, so zero would flicker.
 */
constexpr float STANDING_SPEED = 0.35f;

} // namespace

std::vector<EntityId> authoredCharacters(Scene& scene) {
    std::vector<EntityId> characters;
    scene.forEach<CharacterController, ScriptComponent>(
        [&](EntityId id, CharacterController&, ScriptComponent&) {
            characters.push_back(id);
        }
    );
    std::sort(
        characters.begin(),
        characters.end(),
        [](EntityId a, EntityId b) { return a.slot() < b.slot(); }
    );
    return characters;
}

bool isOfflineSeat(Scene& scene, EntityId entity) {
    const std::vector<EntityId> characters = authoredCharacters(scene);
    return !characters.empty() && characters.front() == entity;
}

bool LabWalker::isUnseatedOffline() {
    return net().isOffline() && !m_offlineSeat;
}

void LabWalker::play(const std::string& clip) {
    if (clip.empty() || clip == m_playing) return;
    if (!m_animator) return;

    Animator* animator = scene().tryGet<Animator>(m_animator);
    if (!animator) return;

    const auto handle = resources().findByName<AnimationClipAsset>(clip);
    if (!handle) return;

    // Every lab clip loops, the jump included (held while airborne).
    const bool looping = true;
    Animator::crossFadeTo(*animator, handle, fadeSeconds, looping);
    m_playing = clip;
}

void LabWalker::followCamera(float dt) {
    const Transform* body = tryGet<Transform>();
    if (!body) return;

    const glm::vec3 want = body->position + Math::WORLD_UP * aimHeight;

    // Seeded behind the character's facing.
    if (!m_framed) {
        float seedPitch = 0.0f;
        const glm::vec3 facing = Math::computeForward(body->rotation);
        Math::toYawPitch(facing, m_orbitYaw, seedPitch);
        m_orbitPitch = glm::radians(-15.0f);
        m_pivot = want;
        m_framed = true;
    }

    // Held on the right button: a runtime that grabbed the pointer outright traps it.
    if (input().held(ACTION_LOOK)) {
        const glm::vec2 look = input().pointerDelta() * lookSensitivity;
        m_orbitYaw   -= look.x;
        m_orbitPitch -= look.y;
        m_orbitPitch = glm::clamp(m_orbitPitch, glm::radians(minPitch), glm::radians(maxPitch));
    }

    m_pivot = glm::mix(m_pivot, want, glm::min(1.0f, followLag * dt));

    Transform* view = scene().tryGet<Transform>(findActiveCamera(scene()));
    if (!view) return;

    const glm::quat orbit = Math::fromYawPitch(m_orbitYaw, m_orbitPitch);
    view->rotation = orbit;
    view->position = m_pivot - Math::computeForward(orbit) * followDistance;
}

void LabWalker::onStart() {
    installBindings(input());
    m_animator    = HierarchyOperations::findInSelfOrDescendants<Animator>(scene(), entity());
    m_offlineSeat = isOfflineSeat(scene(), entity());
}

void LabWalker::onUpdate(float dt) {
    // One camera: without this every walker would steer it and it would snap between them.
    if (!isMine() || isUnseatedOffline()) return;

    // Per frame, not per tick: a mouse quantised to the tick rate is felt at once.
    followCamera(dt);

    // The view rides the command, so a tick steers by the view its input was aimed with.
    if (const Transform* view = scene().tryGet<Transform>(findActiveCamera(scene()))) {
        input().setView(view->rotation);
    }
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

    if (!walk->grounded)              play(jumpClip.name);
    else if (speed < STANDING_SPEED)  play(idleClip.name);
    else if (speed < runSpeed * 0.6f) play(walkClip.name);
    else                              play(runClip.name);
}

void LabWalker::onFixedUpdate(float dt) {
    CharacterController* controller = tryGet<CharacterController>();
    if (!controller) return;

    chooseClip();

    if (!isSimulated()) return;

    // Zeroed: after a session closes, a walker holding its last input would keep going.
    if (isUnseatedOffline()) {
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

    controller->moveInput = move;
    if (map.pressed(tick, ACTION_JUMP))  controller->jumpRequested = true;
    if (map.pressed(tick, ACTION_PROBE)) probe();

    // Aims the entity (forward -Z); a model facing another way is corrected on its
    // rig node - see docs/reference/animation.md.
    if (Transform* body = moving ? tryGet<Transform>() : nullptr) {
        body->rotation = glm::slerp(body->rotation, Math::lookRotation(move), glm::min(1.0f, turnSpeed * dt));
    }
}

void LabWalker::probe() {
    // Authority only: a client draws other players in the past on purpose.
    if (!isAuthority(net().role())) return;
    // Offline there is no history and nothing moves.
    NetSession::Rewind rewound(net(), scene(), entity());
    const glm::vec3 eye  = get<Transform>().position + Math::WORLD_UP * EYE_HEIGHT;
    const glm::vec3 look = Math::computeForward(command().view);
    // The eye stands inside this end's own capsule.
    QueryFilter filter;
    filter.ignore = entity();
    RayHit hit;
    if (!raycast(scene(), eye, look, PROBE_RANGE, hit, filter)) {
        LOG_INFO("Probe from player entity %u found nothing", entity().slot());
        return;
    }
    LOG_INFO(
        "Probe from player entity %u hit entity %u at %.2f m",
        entity().slot(),
        hit.entity.slot(),
        hit.distance
    );
}

} // namespace Lab
