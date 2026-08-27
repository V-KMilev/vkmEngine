#include "lab_walker.h"

#include <cmath>
#include <vector>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/core/hierarchy.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/render/camera.h"
#include "platform/input/input_map.h"
#include "platform/window/window_manager.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

constexpr const char* ACTION_FORWARD = "lab.forward";
constexpr const char* ACTION_BACK    = "lab.back";
constexpr const char* ACTION_LEFT    = "lab.left";
constexpr const char* ACTION_RIGHT   = "lab.right";
constexpr const char* ACTION_WALK    = "lab.walk";
constexpr const char* ACTION_JUMP    = "lab.jump";

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
}

// How far the stick travels before it counts as a direction rather than as
// noise. Keys are digital and never land inside it, but an analog stick rests
// slightly off centre, and without this the character creeps and re-faces
// forever. Squared where it is used, so it reads as travel here and is compared
// against a squared length there.
constexpr float STICK_DEADZONE = 0.1f;

} // namespace

void LabWalker::play(const std::string& clip) {
    if (clip.empty() || clip == m_playing) return;
    if (!m_animator || !context().scene->has<Animator>(m_animator)) return;

    const auto handle = context().resources->findByName<AnimationClipAsset>(clip);
    if (!handle) return;

    Animator& animator = context().scene->get<Animator>(m_animator);
    Animator::crossFadeTo(animator, handle, fadeSeconds);
    m_playing = clip;
}

void LabWalker::followCamera(float dt) {
    Scene& scene = *context().scene;
    if (!scene.has<Transform>(m_entity)) return;

    const Transform& body = scene.get<Transform>(m_entity);
    const glm::vec3 want = body.position + Math::WORLD_UP * aimHeight;

    // Seeded behind whatever the character faces, so the first frame is not
    // spent staring at them from the front.
    if (!m_framed) {
        float seedPitch = 0.0f;
        const glm::vec3 facing = Math::computeForward(body.rotation);
        Math::toYawPitch(facing, m_orbitYaw, seedPitch);
        m_orbitPitch = glm::radians(-15.0f);
        m_pivot = want;
        m_framed = true;
    }

    // The angle is the player's. Held rather than always-on, and on the right
    // button, because that is the gesture the editor's own camera already uses
    // and a runtime that grabbed the pointer outright would be one nobody could
    // get out of.
    if (context().window) {
        auto& mouse = context().window->getInputHandle().getMouse();
        if (mouse.isButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
            const float dx = static_cast<float>(mouse.getDeltaX());
            const float dy = static_cast<float>(mouse.getDeltaY());
            m_orbitYaw   -= dx * lookSensitivity;
            m_orbitPitch -= dy * lookSensitivity;
            m_orbitPitch = glm::clamp(m_orbitPitch, glm::radians(minPitch),
                                      glm::radians(maxPitch));
        }
    }

    // Only the pivot lags, and it lags a position rather than a facing: the
    // camera keeps up with where the character is without inheriting which way
    // they turned.
    m_pivot = glm::mix(m_pivot, want, glm::min(1.0f, followLag * dt));

    scene.forEach<Camera, Transform>(
            [&](EntityId, Camera& camera, Transform& view) {
        if (!camera.active) return;
        const glm::quat orbit = Math::fromYawPitch(m_orbitYaw, m_orbitPitch);
        view.rotation = orbit;
        // Back along its own forward by the fixed distance, so the character
        // stays the same size on screen however the camera is turned.
        view.position = m_pivot - Math::computeForward(orbit) * followDistance;
    });
}

void LabWalker::onStart() {
    installBindings(*context().input);
    m_animator = HierarchyOperations::findInSelfOrDescendants<Animator>(
        *context().scene, m_entity);
    play(idleClip.name);
}

void LabWalker::onUpdate(float dt) {
    // The camera follows the frame, not the tick: a mouse quantised to the
    // simulation rate is felt at once, where steering a tick late is not.
    followCamera(dt);

    // Which leaves the tick unable to ask where the view points, because by the
    // time it runs the answer has moved. Handed to the command instead, so a
    // tick steers by the view its own input was aimed with.
    Scene& scene = *context().scene;
    scene.forEach<Camera, Transform>([&](EntityId, Camera& camera, Transform& view) {
        if (camera.active) context().input->setView(view.rotation);
    });
}

void LabWalker::onFixedUpdate(float dt) {
    Scene& scene = *context().scene;
    if (!scene.has<CharacterController>(m_entity)) return;

    // The command this tick was given, not whatever the device holds now: input
    // arrives on the frame clock, so reading the device here would drop a tap
    // taken between two ticks and repeat a press across every tick of a slow
    // frame.
    const InputMap& input = *context().input;
    const InputCommand& command = input.command();
    const auto axis = [&](const std::string& action) {
        const int slot = input.indexOf(action);
        return slot >= 0 ? command.axis[static_cast<size_t>(slot)] : 0.0f;
    };
    const glm::vec2 stick = {
        axis(ACTION_RIGHT)   - axis(ACTION_LEFT),
        axis(ACTION_FORWARD) - axis(ACTION_BACK)
    };

    // Camera-relative, flattened: a course is walked while looking at it, and
    // world-relative controls make that unusable the moment the view turns.
    // Taken from the command, not from the camera: the camera has turned since
    // this tick's input was read, and a tick that asks it walks somewhere the
    // same command replayed would not.
    glm::vec3 forward = {0.0f, 0.0f, -1.0f};
    glm::vec3 right   = {1.0f, 0.0f, 0.0f};   // screen-right is +X

    // Both come from the same rotation, and both are taken or neither is: a
    // forward from the view beside a right from the default is a basis that
    // describes no view, and the character would strafe at an angle to what it
    // walks. Straight down has no horizontal direction at all, which is what
    // the length test catches.
    const glm::vec3 look = Math::computeForward(command.view);
    const glm::vec3 side = Math::computeRight(command.view);
    const glm::vec3 flatLook = {look.x, 0.0f, look.z};
    const glm::vec3 flatSide = {side.x, 0.0f, side.z};
    if (glm::dot(flatLook, flatLook) > glm::epsilon<float>()
        && glm::dot(flatSide, flatSide) > glm::epsilon<float>()) {
        forward = glm::normalize(flatLook);
        right   = glm::normalize(flatSide);
    }

    // From the command like every other action: held() answers for the device
    // this frame, and a tick that asks the device is the thing the command
    // exists to stop. A bound key reads 1 or 0, so the halfway point splits it.
    const bool walking = axis(ACTION_WALK) > 0.5f;
    const float speed = walking ? walkSpeed : runSpeed;

    glm::vec3 move = right * stick.x + forward * stick.y;
    const float lengthSq = glm::dot(move, move);
    const bool moving = lengthSq > STICK_DEADZONE * STICK_DEADZONE;
    move = moving ? glm::normalize(move) * speed : glm::vec3(0.0f);

    CharacterController& controller = scene.get<CharacterController>(m_entity);
    controller.moveInput = move;
    const int jumpSlot = input.indexOf(ACTION_JUMP);
    if (jumpSlot >= 0 && (command.pressed & (uint32_t(1) << jumpSlot))) {
        controller.jumpRequested = true;
    }

    // Face the way it travels: this aims the entity, whose forward is -Z. A
    // model that faces another way carries the correction on its rig node in
    // the scene, not here - see docs/reference/system/animation.md.
    if (moving && scene.has<Transform>(m_entity)) {
        Transform& body = scene.get<Transform>(m_entity);
        body.rotation = glm::slerp(body.rotation, Math::lookRotation(move),
                                   glm::min(1.0f, turnSpeed * dt));
    }

    // Airborne beats moving, and moving beats standing: what the character is
    // doing is decided by the engine's own answer, not by what was asked for.
    if (!controller.grounded)  play(jumpClip.name);
    else if (!moving)          play(idleClip.name);
    else if (walking)          play(walkClip.name);
    else                       play(runClip.name);
}

} // namespace Vkm::Engine
