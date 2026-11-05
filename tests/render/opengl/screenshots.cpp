// A screenshot is the frame as the player saw it, written the way the file's name asks, and written
// without the frames waiting on it.

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <sys/stat.h>

#include "stb_image.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

TEST("a .png and a .rgb screenshot hold the frame's pixels, top row first") {
    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    addFloor(scene, shapes, material(screen.resources(), "test:floor", {0.5f, 0.5f, 0.5f}));
    // High in the frame, over a gray floor and under a blue sky: a frame upside down shows it low.
    place(
        scene,
        shapes.cube,
        material(screen.resources(), "test:red", {0.9f, 0.05f, 0.05f}),
        {0.0f, 3.2f, 0.0f}
    );
    addSun(scene);
    const EntityId eye = addEye(scene, {0.0f, 1.5f, 8.0f}, {0.0f, 1.5f, 0.0f});

    const std::filesystem::path png = scratch() / "frame.png";
    const std::filesystem::path rgb = scratch() / "frame.rgb";
    const std::filesystem::path jpg = scratch() / "frame.jpg";
    Graphics& graphics = screen.game.engine.getGraphics();
    Picture pngFrame, rgbFrame;
    // A request is taken by the next frame's draw.
    screen.play(PAST_THE_IDENT + 3, [&](int frame) {
        if (frame == PAST_THE_IDENT) graphics.saveScreenshot(png.string());
        if (frame == PAST_THE_IDENT + 1) {
            pngFrame = screen.picture();
            graphics.saveScreenshot(rgb.string());
        }
        if (frame == PAST_THE_IDENT + 2) {
            rgbFrame = screen.picture();
            graphics.saveScreenshot(jpg.string());
        }
    }, 16);

    REQUIRE(pngFrame.width == WIDTH);
    REQUIRE(pngFrame.height == HEIGHT);
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load(png.string().c_str(), &width, &height, &channels, 3);
    REQUIRE(decoded != nullptr);
    const std::vector<uint8_t> fromPng(decoded, decoded + static_cast<size_t>(width) * height * 3);
    stbi_image_free(decoded);
    CHECK(width == static_cast<int>(WIDTH));
    CHECK(height == static_cast<int>(HEIGHT));
    CHECK(fromPng == pngFrame.rgb);

    const std::string raw = readText(rgb);
    CHECK(raw.size() == rgbFrame.rgb.size());
    CHECK(std::vector<uint8_t>(raw.begin(), raw.end()) == rgbFrame.rgb);

    // Which way up: the red box above the middle, the sky over it, the floor along the bottom.
    const glm::vec3 box   = pngFrame.mean(Rect::around(pixelOf(scene, eye, {0.0f, 3.2f, 0.5f}), 4));
    const glm::vec3 sky   = pngFrame.mean({0, 0, WIDTH, 16});
    const glm::vec3 floor = pngFrame.mean({0, HEIGHT - 16, WIDTH, HEIGHT});
    CHECK(box.r > 2.0f * box.g);
    CHECK(sky.b > sky.r);
    CHECK(floor.b < 1.2f * floor.r);

    // Any other kind of file is refused, said, and not written.
    CHECK(!std::filesystem::exists(jpg));
    CHECK(countInLog("frame.jpg") == 1);
}

TEST("the frames go on while a screenshot cannot be written yet, and it holds the frame asked for") {
    // A pipe nobody reads: opening it to write waits for a reader, so a frame writing its own
    // screenshot stops at the request until the reader below gives up on the frames.
    const std::filesystem::path pipe = scratch() / "frame.rgb";
    REQUIRE(mkfifo(pipe.c_str(), 0600) == 0);

    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    addFloor(scene, shapes, material(screen.resources(), "test:floor", {0.5f, 0.5f, 0.5f}));
    addSun(scene);
    addEye(scene, {0.0f, 1.5f, 8.0f}, {0.0f, 1.5f, 0.0f});

    constexpr int FRAMES_ON = 10;
    std::mutex              mutex;
    std::condition_variable onFrame;
    bool wentOn = false;
    bool heard  = false;  // Whether the frames went on before the reader gave up.
    std::string             written;
    std::thread reader([&] {
        {
            std::unique_lock<std::mutex> lock(mutex);
            heard = onFrame.wait_for(lock, std::chrono::seconds(20), [&] { return wentOn; });
        }
        written = readText(pipe);
    });

    Graphics& graphics = screen.game.engine.getGraphics();
    Picture asked;
    screen.play(PAST_THE_IDENT + FRAMES_ON + 1, [&](int frame) {
        if (frame == PAST_THE_IDENT) graphics.saveScreenshot(pipe.string());
        if (frame == PAST_THE_IDENT + 1) asked = screen.picture();
        if (frame == PAST_THE_IDENT + 1 + FRAMES_ON) {
            const std::lock_guard<std::mutex> lock(mutex);
            wentOn = true;
            onFrame.notify_one();
        }
    }, 16);
    reader.join();

    CHECK(heard);
    REQUIRE(asked.width == WIDTH);
    CHECK(std::vector<uint8_t>(written.begin(), written.end()) == asked.rgb);
}
