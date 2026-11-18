// What the renderer keeps between frames - held shadow tiles, baked light, probe and volume
// captures - kept only while it holds: a scene changed and walked through draws as one started
// changed.

#include <cmath>

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

namespace {

const char* const WALKED =
    "after a walk out and back, a fixed lamp moved and the probe moved, the frame is the one a fresh start draws";

/// The room as the walk leaves it: the lamp and the probe moved.
void moved(Scene& scene, const Room& room) {
    scene.get<Transform>(room.lamp).position  = {3.2f, 3.0f, -3.2f};
    scene.get<Transform>(room.probe).position = {0.5f, 2.0f, 0.5f};
}

} // namespace

TEST(WALKED) {
    Screen screen;
    Scene& scene    = screen.scene();
    const Room room = bakedRoom(scene, screen.resources());
    if (isFreshStart()) {
        moved(scene, room);
        keepFreshStart(screen.shot());
        return;
    }

    // Out through the doorway and round, so the sun's cascades and the held tiles move with the eye; the
    // lamp and the probe moved while it is away; back, and still until the bakes are done.
    Transform& eye = scene.get<Transform>(room.eye);
    const Transform start = eye;
    constexpr int WALK = 60, BACK = 120, SETTLED = 240;
    Picture walked;
    screen.play(SETTLED, [&](int frame) {
        if (frame <= WALK) {
            const float t = static_cast<float>(frame) / WALK;
            eye.position  = start.position + glm::vec3(-2.8f * std::sin(3.14159f * t), 0.0f, 10.0f * t);
            eye.rotation  = start.rotation * glm::angleAxis(6.28318f * t, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        if (frame == WALK / 2) moved(scene, room);
        if (frame == BACK) eye = start;
        if (frame == SETTLED) walked = screen.picture();
    }, 16);
    const RenderReadiness settled = screen.readiness();

    const Picture fresh = freshStart(WALKED);
    REQUIRE(fresh.width == WIDTH);
    CHECK(settled.ready());
    std::printf("  %.4f of the frame apart from a fresh start's\n", differing(walked, fresh));
    CHECK(differing(walked, fresh) < 0.002f);
}
