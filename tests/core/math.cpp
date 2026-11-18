// The math a game builds on: the axes every helper agrees on, the seeded generator, curves, the
// view's frustum, a ray meeting a level plane and a moved box.

#include <algorithm>
#include <cmath>
#include <optional>

#include <glm/gtc/constants.hpp>

#include "vkm_physics/geometry/rotation.h"

#include "base/math/aabb.h"
#include "base/math/axes.h"
#include "base/math/curve.h"
#include "base/math/frustum.h"
#include "base/math/random.h"
#include "base/math/ray.h"
#include "base/math/rotation.h"
#include "ecs/component/render/camera.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
namespace Math = Vkm::Engine::Math;

namespace {

/// Where @p point lands on the screen of @p eye: x and y in -1..1 when in view, and whether it is
/// in front of the eye.
glm::vec3 onScreen(const CameraView& eye, const glm::vec3& point) {
    const glm::vec4 clip = eye.projection * eye.view * glm::vec4(point, 1.0f);
    return {clip.x / clip.w, clip.y / clip.w, clip.w};
}

} // namespace

TEST("forward is -Z, right is +X and up is +Y for every helper, and a camera looks down its forward") {
    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    CHECK_NEAR(Math::computeForward(identity), glm::vec3(0.0f, 0.0f, -1.0f), 1e-6f);
    CHECK_NEAR(Math::computeRight(identity), glm::vec3(1.0f, 0.0f, 0.0f), 1e-6f);
    CHECK_NEAR(Math::computeUp(identity), glm::vec3(0.0f, 1.0f, 0.0f), 1e-6f);
    CHECK_NEAR(Math::WORLD_FORWARD, glm::vec3(0.0f, 0.0f, -1.0f), 0.0f);
    CHECK_NEAR(Math::WORLD_RIGHT, glm::cross(Math::WORLD_FORWARD, Math::WORLD_UP), 0.0f);

    // A positive yaw turns left, a positive pitch looks up, and toYawPitch reads them back.
    const glm::quat left = Math::fromYawPitch(glm::half_pi<float>(), 0.0f);
    CHECK_NEAR(Math::computeForward(left), glm::vec3(-1.0f, 0.0f, 0.0f), 1e-6f);
    const glm::quat up = Math::fromYawPitch(0.0f, 0.5f);
    CHECK(Math::computeForward(up).y > 0.4f);
    for (const float yaw : {-2.5f, -0.3f, 0.0f, 1.1f, 3.0f}) {
        for (const float pitch : {-1.2f, 0.0f, 0.7f}) {
            float readYaw = 0.0f, readPitch = 0.0f;
            Math::toYawPitch(Math::computeForward(Math::fromYawPitch(yaw, pitch)), readYaw, readPitch);
            CHECK_NEAR(readYaw, yaw, 1e-4f);
            CHECK_NEAR(readPitch, pitch, 1e-4f);
        }
    }

    // lookRotation faces what it is given; the right of it stays level.
    const glm::vec3 toward  = glm::normalize(glm::vec3(3.0f, 1.0f, -2.0f));
    const glm::quat looking = Math::lookRotation(toward);
    CHECK_NEAR(Math::computeForward(looking), toward, 1e-5f);
    CHECK_NEAR(Math::computeRight(looking).y, 0.0f, 1e-5f);

    // A camera turned that way sees a point along its forward in the middle of the picture, and
    // one along its right on the picture's right.
    const CameraView eye  = viewThrough(Camera{}, glm::vec3(1.0f, 2.0f, 3.0f), looking, 16.0f / 9.0f);
    const glm::vec3 ahead = onScreen(eye, glm::vec3(1.0f, 2.0f, 3.0f) + 10.0f * toward);
    CHECK(ahead.z > 0.0f);
    CHECK_NEAR(glm::vec2(ahead), glm::vec2(0.0f), 1e-4f);
    const glm::vec3 aside = onScreen(
        eye,
        glm::vec3(1.0f, 2.0f, 3.0f) + 10.0f * toward + Math::computeRight(looking)
    );
    CHECK(aside.x > 0.05f);

    // rotationBetween is the spin that turns one into the other in a second, the short way.
    const glm::quat from   = Math::fromYawPitch(0.3f, 0.1f);
    const glm::quat to     = Math::fromYawPitch(-0.4f, 0.2f);
    const glm::vec3 spin   = Vkm::Physics::rotationBetween(from, to);
    const glm::quat turned = glm::angleAxis(glm::length(spin), glm::normalize(spin)) * from;
    CHECK(std::fabs(glm::dot(turned, to)) > 1.0f - 1e-6f);
    CHECK(glm::length(spin) < glm::pi<float>());
}

TEST("a seeded Rng repeats its stream, two seeds differ, and a draw stays in its range") {
    // PCG32's own reference stream for seed 42, stream 54 (O'Neill's pcg32-demo): the generator
    // the engine names, so a seed means the same on every build and machine.
    Math::Rng reference(42u, 54u);
    const uint32_t expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (const uint32_t value : expected) CHECK(reference.nextU32() == value);

    Math::Rng a(1234u), b(1234u), c(1235u);
    int same = 0, differ = 0;
    for (int i = 0; i < 100; ++i) {
        const uint32_t x = a.nextU32();
        same   += x == b.nextU32();
        differ += x != c.nextU32();
    }
    CHECK(same == 100);
    CHECK(differ > 95);

    // A re-seed starts the stream over.
    a.seed(1234u);
    Math::Rng fresh(1234u);
    const uint32_t again = a.nextU32();
    CHECK(again == fresh.nextU32());

    Math::Rng draws(7u);
    int outside  = 0;
    bool lowSeen = false, highSeen = false;
    for (int i = 0; i < 10000; ++i) {
        const float f = draws.nextFloat();
        const float g = draws.nextFloat(-2.0f, 3.0f);
        const int   n = draws.nextInt(-3, 4);
        outside  += f < 0.0f || f >= 1.0f || g < -2.0f || g >= 3.0f || n < -3 || n > 4;
        lowSeen  |= n == -3;
        highSeen |= n == 4;
    }
    CHECK(outside == 0);
    CHECK(lowSeen);
    CHECK(highSeen);
}

TEST("a curve and a gradient run straight between their keys and hold past their ends") {
    Math::Curve none;
    CHECK(none.at(0.5f) == 1.0f);

    Math::Curve one;
    one.keys = {{0.3f, 4.0f}};
    CHECK(one.at(0.0f) == 4.0f);
    CHECK(one.at(1.0f) == 4.0f);

    // Listed out of order: the keys are read by their times.
    Math::Curve curve;
    curve.keys = {{0.6f, 3.0f}, {0.2f, 1.0f}};
    CHECK_NEAR(curve.at(0.0f), 1.0f, 1e-6f);
    CHECK_NEAR(curve.at(0.2f), 1.0f, 1e-6f);
    CHECK_NEAR(curve.at(0.4f), 2.0f, 1e-6f);
    CHECK_NEAR(curve.at(0.6f), 3.0f, 1e-6f);
    CHECK_NEAR(curve.at(1.0f), 3.0f, 1e-6f);

    Math::Gradient gradient;
    CHECK_NEAR(gradient.at(0.5f), glm::vec4(1.0f), 0.0f);
    gradient.keys = {{0.0f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)}, {0.5f, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)}};
    CHECK_NEAR(gradient.at(0.25f), glm::vec4(0.5f, 0.0f, 0.5f, 0.5f), 1e-6f);
    CHECK_NEAR(gradient.at(0.9f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), 1e-6f);
}

TEST("a box is in a camera's view, out of it or across its edge as its corners say") {
    Camera lens;
    lens.zNear = 0.5f;
    lens.zFar  = 100.0f;
    const CameraView eye     = viewThrough(lens, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 1.0f);
    const Math::Frustum view = Math::extractFrustum(eye.projection * eye.view);
    const auto box = [](const glm::vec3& center, float half) {
        return Math::AABB{center - glm::vec3(half), center + glm::vec3(half)};
    };

    CHECK(Math::frustumIntersectsAABB(view, box({0.0f, 0.0f, -10.0f}, 1.0f)));     // ahead
    CHECK(!Math::frustumIntersectsAABB(view, box({0.0f, 0.0f, 10.0f}, 1.0f)));     // behind
    CHECK(!Math::frustumIntersectsAABB(view, box({0.0f, 0.0f, -200.0f}, 1.0f)));   // past the far plane
    CHECK(!Math::frustumIntersectsAABB(view, box({0.0f, 0.0f, -0.1f}, 0.05f)));    // nearer than near
    CHECK(!Math::frustumIntersectsAABB(view, box({30.0f, 0.0f, -10.0f}, 1.0f)));   // off to the right
    CHECK(!Math::frustumIntersectsAABB(view, box({0.0f, -30.0f, -10.0f}, 1.0f)));  // below
    // Its edge at 70 degrees: a box straddling it at 10 m counts, one just past it does not.
    const float edge = 10.0f * std::tan(lens.fovY * 0.5f);
    CHECK(Math::frustumIntersectsAABB(view, box({edge, 0.0f, -10.0f}, 0.5f)));
    CHECK(!Math::frustumIntersectsAABB(view, box({edge + 1.5f, 0.0f, -10.0f}, 0.5f)));
    CHECK(Math::frustumIntersectsAABB(view, box({-edge - 0.4f, 0.0f, -10.0f}, 0.5f)));
}

TEST("a ray meets a level plane ahead of it, and not one behind it or that it runs along") {
    const Math::Ray down{{1.0f, 5.0f, 2.0f}, glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f))};
    const std::optional<glm::vec3> floor = Math::rayHitsLevel(down, 1.0f);
    REQUIRE(floor.has_value());
    CHECK_NEAR(*floor, glm::vec3(5.0f, 1.0f, 2.0f), 1e-5f);
    CHECK(!Math::rayHitsLevel(down, 6.0f));
    CHECK(!Math::rayHitsLevel(Math::Ray{{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, 0.0f));
}

TEST("a moved box holds every corner of the box it moved, and no more") {
    Math::Rng rng(99u);
    const Math::AABB local{{-1.0f, -0.5f, -2.0f}, {3.0f, 0.25f, 1.0f}};
    float worst = 0.0f;
    int unlike  = 0;
    for (int i = 0; i < 200; ++i) {
        // One draw a statement, so a failing case is the same case under every compiler.
        const float ax = rng.nextFloat(-1, 1);
        const float ay = rng.nextFloat(-1, 1);
        const float az = rng.nextFloat(-1, 1);
        const glm::vec3 axis = glm::normalize(glm::vec3(ax, ay, az) + 0.01f);
        const float sx = rng.nextFloat(-3, 3);
        const float sy = rng.nextFloat(0.1f, 2);
        const float sz = rng.nextFloat(-1, 4);
        const glm::vec3 scale(sx, sy, sz);
        const float ox = rng.nextFloat(-50, 50);
        const float oy = rng.nextFloat(-50, 50);
        const float oz = rng.nextFloat(-50, 50);
        const glm::vec3 offset(ox, oy, oz);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), offset);
        model = model * glm::mat4_cast(glm::angleAxis(rng.nextFloat(-6, 6), axis));
        model = glm::scale(model, scale);

        Math::AABB corners = Math::AABB::empty();
        for (int c = 0; c < 8; ++c) {
            const glm::vec3 corner(
                (c & 1) ? local.max.x : local.min.x,
                (c & 2) ? local.max.y : local.min.y,
                (c & 4) ? local.max.z : local.min.z
            );
            corners.expand(glm::vec3(model * glm::vec4(corner, 1.0f)));
        }
        const Math::AABB moved = Math::transform(model, local);
        const glm::vec3 apart  = glm::max(
            glm::abs(moved.min - corners.min),
            glm::abs(moved.max - corners.max)
        );
        worst = std::max({worst, apart.x, apart.y, apart.z});

        // A pose held as rotation and position moves a box as its matrix does, to the bit.
        const Math::AABB posed = Math::transform(glm::mat3(model), glm::vec3(model[3]), local);
        unlike += posed.min != moved.min || posed.max != moved.max;
    }
    CHECK(worst < 1e-3f);
    CHECK(unlike == 0);
}
