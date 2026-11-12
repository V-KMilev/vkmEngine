// The editor's view: its own viewpoint, flown with the right button held, orbited about what it is on,
// and framing a selection whole. It is no entity, so looking around writes nothing into the scene.

#include <cmath>
#include <vector>

#include "actions/create_entity.h"
#include "actions/edit_actions.h"
#include "base/math/rotation.h"
#include "ecs/component/render/camera.h"
#include "vkm_platform/input/input_handle.h"
#include "undo/component_edit.h"
#include "view/view_framing.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;
using namespace Vkm::Platform;
using namespace Vkm::Engine::EditorActions;

namespace {

/**
 * @brief The editor's view over a frame of its own, handed the mouse and keys a frame at a time.
 */
struct Flying : Bench {
    glm::vec2 cursor{640.0f, 360.0f};

    Flying() {
        frame.clock.setPaused(true);
        frame.clock.beginFrame();
    }

    InputHandle& hand() { return frame.window.getInputHandle(); }

    /// One frame of one tick, the cursor moved by @p move; the view reads it as the Input stage does.
    void step(const glm::vec2& move = glm::vec2(0.0f)) {
        cursor += move;
        hand().moveTo(cursor.x, cursor.y);
        frame.clock.requestStep(1);
        frame.clock.beginFrame();
        while (frame.clock.consumeFixedStep()) {}
        view.update(frame.ctx);
        hand().clearStrikes();
    }

    /// Frames of one tick in a second.
    int second() const { return static_cast<int>(1.0f / frame.clock.getFixedStep() + 0.5f); }

    glm::vec3 forward() const {
        return Math::computeForward(Math::fromYawPitch(view.viewpoint().yaw, view.viewpoint().pitch));
    }
};

EditorViewpoint at(const glm::vec3& position) {
    EditorViewpoint viewpoint;
    viewpoint.position = position;
    return viewpoint;
}

} // namespace

TEST("the view flies on the keys only while the right button holds it, and writes nothing in the scene") {
    Flying flying;
    openProject();
    flying.view.setViewpoint(at({0.0f, 0.0f, 0.0f}));
    const float speed = flying.view.getSettings().moveSpeed;

    flying.hand().onKeyEvent(Key::W, true);
    for (int frame = 0; frame < flying.second(); ++frame) flying.step();
    CHECK(flying.view.viewpoint().position == glm::vec3(0.0f));

    flying.hand().setButton(MouseButton::Right, true);
    for (int frame = 0; frame < flying.second(); ++frame) flying.step();
    CHECK_NEAR(flying.view.viewpoint().position, glm::vec3(0.0f, 0.0f, -speed), 1e-3f);
    CHECK(flying.frame.window.cursorMode() == CursorMode::Disabled);

    flying.hand().onKeyEvent(Key::W, false);
    flying.hand().onKeyEvent(Key::D, true);
    flying.hand().onKeyEvent(Key::LeftShift, true);
    for (int frame = 0; frame < flying.second(); ++frame) flying.step();
    const float boost = flying.view.getSettings().speedBoost;
    CHECK_NEAR(flying.view.viewpoint().position, glm::vec3(speed * boost, 0.0f, -speed), 1e-3f);

    // Turning: the mouse moved to the right turns the view right.
    flying.step({200.0f, 0.0f});
    CHECK(flying.forward().x > 0.1f);

    flying.hand().setButton(MouseButton::Right, false);
    flying.step();
    CHECK(flying.frame.window.cursorMode() == CursorMode::Normal);
    int entities = 0;
    flying.frame.scene.forEachEntity([&](EntityId) { ++entities; });
    CHECK(entities == 0);
    CHECK(!flying.desk.state.sceneDirty);
}

TEST("Alt and a left drag orbit the view about what it is on, which stays in the middle of it") {
    Flying flying;
    openProject();
    const glm::vec3 focus(5.0f, 1.0f, -3.0f);
    flying.view.setViewpoint(at({5.0f, 1.0f, 9.0f}));
    flying.view.focusOn(focus, 12.0f);
    flying.step();

    flying.hand().onKeyEvent(Key::LeftAlt, true);
    flying.hand().setButton(MouseButton::Left, true);
    for (int frame = 0; frame < 20; ++frame) flying.step({25.0f, -8.0f});
    flying.hand().setButton(MouseButton::Left, false);
    flying.step();

    const glm::vec3 position = flying.view.viewpoint().position;
    CHECK(glm::length(position - glm::vec3(5.0f, 1.0f, 9.0f)) > 5.0f);
    CHECK_NEAR(glm::length(position - focus), 12.0f, 1e-3f);
    CHECK_NEAR(position + flying.forward() * 12.0f, focus, 1e-3f);
}

TEST("Frame Selected puts the whole selection in view, and a stood-down view refuses to move") {
    Flying flying;
    openProject();
    Desk& desk = flying.desk;
    std::vector<EntityId> cubes;
    for (const float x : {-10.0f, 14.0f}) {
        cubes.push_back(createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Cube));
        EditScope<Transform> pose(desk.scene(), desk.resources(), desk.state, cubes.back(), "Edit Transform");
        pose->position = {x, 2.0f, -30.0f};
    }
    flying.view.setViewpoint(at({0.0f, 0.0f, 0.0f}));

    ViewFraming::frameSelected(flying.frame.ctx, cubes, flying.view);
    const glm::vec3 eye   = flying.view.viewpoint().position;
    const float halfAngle = flying.view.fieldOfView() * 0.5f;
    for (const EntityId cube : cubes) {
        const glm::vec3 center = desk.scene().get<Transform>(cube).position;
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 point = center + glm::vec3(
                corner & 1 ? 0.5f : -0.5f,
                corner & 2 ? 0.5f : -0.5f,
                corner & 4 ? 0.5f : -0.5f
            );
            const float off = std::acos(glm::dot(glm::normalize(point - eye), flying.forward()));
            CHECK(off < halfAngle);
        }
    }
    CHECK_NEAR(eye + flying.forward() * flying.view.focusDistance(), glm::vec3(2.0f, 2.0f, -30.0f), 1e-3f);

    // In play the viewport is the game's: the view stands down and takes no move.
    flying.view.setActive(false);
    flying.view.focusOn(glm::vec3(100.0f), 5.0f);
    flying.hand().setButton(MouseButton::Right, true);
    flying.hand().onKeyEvent(Key::W, true);
    for (int frame = 0; frame < 10; ++frame) flying.step();
    CHECK(flying.view.viewpoint().position == eye);
    CHECK(flying.frame.window.cursorMode() == CursorMode::Normal);
}

TEST("looking through a scene camera shows it read-only: the view does not fly and the camera does not move") {
    Flying flying;
    openProject();
    Desk& desk = flying.desk;
    const EntityId camera = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Camera);
    const Transform pose = desk.scene().get<Transform>(camera);
    flying.view.setViewpoint(at({0.0f, 0.0f, 0.0f}));

    flying.view.lookThrough(camera);
    flying.hand().setButton(MouseButton::Right, true);
    flying.hand().onKeyEvent(Key::W, true);
    for (int frame = 0; frame < 30; ++frame) flying.step({10.0f, 0.0f});
    CHECK(flying.view.viewpoint().position == glm::vec3(0.0f));
    CHECK(desk.scene().get<Transform>(camera).position == pose.position);
    CHECK(desk.scene().get<Transform>(camera).rotation == pose.rotation);
    CHECK(flying.frame.ctx.hostView);
    CHECK(flying.frame.ctx.hostView->through == camera);

    // Its camera gone, the view is the editor's again.
    deleteEntity(desk.scene(), desk.state, camera);
    flying.step();
    CHECK(flying.view.lookingThrough() == EntityId{});
    flying.step();
    CHECK(flying.view.viewpoint().position != glm::vec3(0.0f));
}
