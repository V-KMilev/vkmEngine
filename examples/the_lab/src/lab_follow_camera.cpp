#include "lab_follow_camera.h"

#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "core/math/easing.h"
#include "core/math/rotation.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"
#include "system/physics/query/query.h"

#include "lab_actions.h"
#include "lab_seat.h"

namespace Lab {

namespace {

// Where the orbit starts: behind the character, looking a little down.
constexpr float START_PITCH = -15.0f;

// How far short of a wall the camera stops, metres, so its near plane does not cut into it.
constexpr float WALL_MARGIN = 0.3f;

// The nearest the camera comes however close the wall, metres.
constexpr float MIN_REACH = 0.6f;

// How fast it eases back out once a wall is gone, per second.
constexpr float REACH_RECOVERY = 3.0f;

} // namespace

void LabFollowCamera::onStart() {
    input().define(ACTION_LOOK, { InputBinding{InputSource::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, 1.0f} });
}

float LabFollowCamera::clearDistance(const glm::vec3& pivot, const glm::vec3& back) {
    // Scenery, not the players and props moving about, nor the character itself.
    QueryFilter filter;
    filter.hitDynamic = false;
    filter.ignore     = entity();
    RayHit hit;
    if (!raycast(scene(), pivot, back, followDistance + WALL_MARGIN, hit, filter)) return followDistance;
    return glm::max(hit.distance - WALL_MARGIN, MIN_REACH);
}

void LabFollowCamera::onUpdate(float dt) {
    // One camera: every character but this player's would otherwise steer it too.
    if (!isMine() || sitsOutOffline(scene(), net(), entity())) return;
    const Transform* body = tryGet<Transform>();
    if (!body) return;

    const glm::vec3 want = body->position + Math::WORLD_UP * pivotHeight;
    if (!m_framed) {
        float facingPitch = 0.0f;
        Math::toYawPitch(Math::computeForward(body->rotation), m_yaw, facingPitch);
        m_pitch  = glm::radians(START_PITCH);
        m_pivot  = want;
        m_reach  = followDistance;
        m_framed = true;
    }

    // Held on the right button: a runtime that grabbed the pointer outright traps it.
    if (input().held(ACTION_LOOK)) {
        const glm::vec2 look = input().pointerDelta() * lookSensitivity;
        m_yaw   -= look.x;
        m_pitch  = glm::clamp(m_pitch - look.y, glm::radians(minPitch), glm::radians(maxPitch));
    }

    // Per frame, not per tick: a mouse quantised to the tick rate is felt at once. The pivot follows
    // position, never facing: a camera that trailed facing would move "forward" under the hand.
    m_pivot = glm::mix(m_pivot, want, damp(followLag, dt));

    Transform* view = scene().tryGet<Transform>(findActiveCamera(scene()));
    if (!view) return;
    const glm::quat orbit = Math::fromYawPitch(m_yaw, m_pitch);
    const glm::vec3 back  = -Math::computeForward(orbit);
    const float     clear = clearDistance(m_pivot, back);
    m_reach = clear < m_reach ? clear : glm::min(clear, m_reach + REACH_RECOVERY * dt);
    view->rotation = orbit;
    view->position = m_pivot + back * m_reach;

    // The view rides the command, so a tick steers and aims by the view its input was made with.
    input().setView(view->rotation);
}

} // namespace Lab
