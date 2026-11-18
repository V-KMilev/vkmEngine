// The world on the screen: the ray under a window point, and where a world point lands on a canvas.

#include <glm/gtx/norm.hpp>

#include "core/frame/screen_view.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/ui/ui_canvas.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

TEST("ScreenView gives the ray through a window point and where a world point lands on a canvas") {
    Frame frame;
    // The game's view is a 200 by 100 rect, 40 in and 20 down the window.
    frame.chrome.viewport = {40, 20, 200, 100};
    CHECK(!ScreenView::of(frame.ctx).valid());

    // An eye 10 m up +Z, looking down -Z at the origin.
    const EntityId eye = frame.scene.createEntity();
    Transform pose;
    pose.position = {0.0f, 0.0f, 10.0f};
    frame.scene.add(eye, pose);
    frame.scene.add(eye, Camera{});
    const ScreenView view = ScreenView::of(frame.ctx);
    REQUIRE(view.valid());

    const auto middle = view.rayThrough({140.0f, 70.0f});
    REQUIRE(middle.has_value());
    CHECK_NEAR(middle->direction, glm::vec3(0.0f, 0.0f, -1.0f), 1e-4f);
    CHECK(view.rayThrough({140.0f, 30.0f})->direction.y > 0.1f);
    CHECK(view.rayThrough({220.0f, 70.0f})->direction.x > 0.1f);
    CHECK(!view.rayThrough({20.0f, 70.0f}).has_value());
    CHECK(!view.rayThrough({140.0f, 130.0f}).has_value());

    // A canvas laid out 50 high: two screen pixels a canvas pixel.
    UICanvas canvas;
    canvas.referenceHeight = 50.0f;
    CHECK_NEAR(*view.toCanvas({0.0f, 0.0f, 0.0f}, canvas), glm::vec2(50.0f, 25.0f), 1e-3f);
    CHECK(view.toCanvas({0.0f, 1.0f, 0.0f}, canvas)->y < 24.0f);
    CHECK(view.toCanvas({1.0f, 0.0f, 0.0f}, canvas)->x > 51.0f);
    CHECK(!view.toCanvas({0.0f, 0.0f, 20.0f}, canvas).has_value());

    // Back again: the ray through where a point lands passes through the point.
    const glm::vec3 lamp(2.0f, 1.5f, -3.0f);
    const glm::vec2 placed = *view.toCanvas(lamp, canvas) * 2.0f + glm::vec2(40.0f, 20.0f);
    const auto ray = view.rayThrough(placed);
    REQUIRE(ray.has_value());
    const glm::vec3 toLamp = lamp - ray->origin;
    CHECK(glm::length(toLamp - ray->direction * glm::dot(toLamp, ray->direction)) < 1e-3f);
}
