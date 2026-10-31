#include "render/opengl/gpu.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>

#include <GL/glew.h>

#include "stb_image.h"

#include "vkm_opengl/buffer/shader_storage_buffer.h"
#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/shader/shader_preprocess.h"

#include "gl_backend.h"
#include "gl_shader_constants.h"
#include "frame/gl_frame_reader.h"
#include "vkm_platform/process/child_process.h"
#include "system/render/render_view.h"
#include "system/render/screenshot.h"

#include "harness/check.h"

namespace Vkm::Test {

/**
 * @brief GLBackend drawing into the off-screen surface.
 *
 * A host with no window reports a 0 x 0 surface, which the backend sizes its last pass by; this
 * gives the view the surface's size and is otherwise the backend itself.
 */
class OffscreenBackend : public RenderBackend {
    public:
        bool init(Platform::WindowManager& window) override {
            const bool ready = m_gl.init(window);
            setInfo(m_gl.info());
            return ready;
        }

        void render(const RenderView& view, const ResourceManager& resources) override {
            // The objects are a copy, and once the frame is drawn its lists name an index past every
            // array, as Visibility rewrites its own next frame: a backend reading them after render
            // returns faults there.
            m_objects             = *view.objects;
            m_sized               = view;
            m_sized.objects       = &m_objects;
            m_sized.surfaceWidth  = WIDTH;
            m_sized.surfaceHeight = HEIGHT;
            m_gl.render(m_sized, resources);
            std::fill(m_objects.scene.begin(), m_objects.scene.end(), PAST_EVERY_OBJECT);
            std::fill(m_objects.visible.begin(), m_objects.visible.end(), PAST_EVERY_OBJECT);
        }

        bool readFrame(const RenderView& view) override {
            m_sized               = view;
            m_sized.surfaceWidth  = WIDTH;
            m_sized.surfaceHeight = HEIGHT;
            return m_gl.readFrame(m_sized);
        }
        bool takeFrame(FramePixels& frame, bool wait) override { return m_gl.takeFrame(frame, wait); }

        uint32_t maxAnisotropy() const override { return m_gl.maxAnisotropy(); }
        bool holdsPixels(const TextureHandle& texture, uint64_t version) const override {
            return m_gl.holdsPixels(texture, version);
        }
        EditorRenderHooks* editorHooks() override { return m_gl.editorHooks(); }
        RenderReadiness readiness() const override { return m_gl.readiness(); }

        /// The backend itself, for what it shows only to tests.
        const GLBackend& gl() const { return m_gl; }

        /// The frame last drawn, as RenderSystem reads one for a screenshot; read by a reader of its
        /// own, so a screenshot's read in flight stays the screenshot's.
        Picture last() {
            Picture     picture;
            FramePixels frame;
            if (m_pictures.read(m_sized) && m_pictures.take(frame, true)) {
                picture.width  = frame.width;
                picture.height = frame.height;
                picture.rgb    = std::move(frame.rgb);
            }
            return picture;
        }

    private:
        static constexpr uint32_t PAST_EVERY_OBJECT = std::numeric_limits<uint32_t>::max();

        GLBackend     m_gl;
        GLFrameReader m_pictures;
        RenderView    m_sized;
        RenderObjects m_objects;
};

namespace {

// Frames the renderer stays ready for before a test's frames begin: a bake that ends starts no other,
// and the ident's images are drawn, so its clock runs.
constexpr int SETTLE_FRAMES = 4;

// Frames a renderer may take to become ready: its programs compile and its bakes run a share a frame.
constexpr int READY_LIMIT = 100000;

bool asked(const char* variable) {
    const char* value = std::getenv(variable);
    return value && std::string(value) == "1";
}

} // namespace

Rect Rect::around(const glm::vec2& center, int radius) {
    const auto clampTo = [](float v, uint32_t most) {
        return static_cast<uint32_t>(std::clamp(v, 0.0f, static_cast<float>(most)));
    };
    const float r = static_cast<float>(radius);
    return {clampTo(center.x - r, WIDTH), clampTo(center.y - r, HEIGHT), clampTo(center.x + r + 1.0f, WIDTH),
            clampTo(center.y + r + 1.0f, HEIGHT)};
}

glm::vec3 Picture::at(uint32_t x, uint32_t y) const {
    const size_t i = (static_cast<size_t>(y) * width + x) * 3;
    return {rgb[i], rgb[i + 1], rgb[i + 2]};
}

glm::vec3 Picture::mean(const Rect& rect) const {
    glm::dvec3 sum(0.0);
    size_t count = 0;
    for (uint32_t y = rect.y0; y < std::min(rect.y1, height); ++y) {
        for (uint32_t x = rect.x0; x < std::min(rect.x1, width); ++x) {
            sum += glm::dvec3(at(x, y));
            ++count;
        }
    }
    return count > 0 ? glm::vec3(sum / static_cast<double>(count)) : glm::vec3(0.0f);
}

float differing(const Picture& a, const Picture& b, int levels, const Rect& rect) {
    if (a.width != b.width || a.height != b.height || a.rgb.size() != b.rgb.size()) return 1.0f;
    size_t apart = 0;
    size_t count = 0;
    for (uint32_t y = rect.y0; y < std::min(rect.y1, a.height); ++y) {
        for (uint32_t x = rect.x0; x < std::min(rect.x1, a.width); ++x) {
            const glm::vec3 d = glm::abs(a.at(x, y) - b.at(x, y));
            apart += std::max({d.r, d.g, d.b}) > static_cast<float>(levels);
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(apart) / static_cast<float>(count) : 0.0f;
}

float meanApart(const Picture& a, const Picture& b, const Rect& rect) {
    if (a.width != b.width || a.height != b.height) return 255.0f;
    double sum   = 0.0;
    size_t count = 0;
    for (uint32_t y = rect.y0; y < std::min(rect.y1, a.height); ++y) {
        for (uint32_t x = rect.x0; x < std::min(rect.x1, a.width); ++x) {
            const glm::vec3 d = glm::abs(a.at(x, y) - b.at(x, y));
            sum += (d.r + d.g + d.b) / 3.0;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
}

float lit(const Picture& picture) {
    size_t shown = 0;
    for (size_t i = 0; i < picture.rgb.size(); i += 3) {
        shown += (picture.rgb[i] | picture.rgb[i + 1] | picture.rgb[i + 2]) > 8;
    }
    return picture.rgb.empty() ? 0.0f : static_cast<float>(shown) / static_cast<float>(
        picture.rgb.size() / 3
    );
}

Device::Device() {
    const bool surface = m_context.available() && m_context.attachSurface(WIDTH, HEIGHT);
    if (surface) return;
    const std::string why = m_context.available(

    ) ? "the GPU offers no off-screen surface" : m_context.reason();
    if (!asked("VKM_GPU_REQUIRED")) skip(why);
    fail(__FILE__, __LINE__, "VKM_GPU_REQUIRED=1, and " + why);
    throw Stop{};
}

Probes::Probes() {
    // As GLBackend puts it before every program it builds.
    Vkm::OpenGL::setShaderPrelude(shaderConstants());
}

Probes::~Probes() = default;

std::vector<glm::vec4> Probes::run(
    const std::string& name,
    const std::vector<glm::vec4>& rows,
    const std::function<void(const Vkm::OpenGL::ComputeShader&)>& set
) const {
    constexpr size_t GROUP = 64;
    REQUIRE(!rows.empty());
    REQUIRE(rows.size() % GROUP == 0u);
    const Vkm::OpenGL::ComputeShader probe("tests/render/opengl/probes/" + name);
    const auto bytes = static_cast<uint32_t>(rows.size() * sizeof(glm::vec4));
    const Vkm::OpenGL::ShaderStorageBuffer buffer(rows.data(), bytes);
    buffer.bindBase(0);
    probe.bind();
    if (set) set(probe);
    probe.dispatch(static_cast<uint32_t>(rows.size() / GROUP));
    Vkm::OpenGL::memoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    std::vector<glm::vec4> read(rows.size());
    buffer.read(0, bytes, read.data());
    return read;
}

Screen::Screen(uint32_t tickRate)
    : game(tickRate)
{
    auto backend = std::make_unique<OffscreenBackend>();
    m_backend = backend.get();
    game.systems.render.setBackend(std::move(backend), game.engine.getWindow());
    // The scene's rect, as a host's chrome gives it; the window it would sit in has no size.
    chrome().viewport = {0, 0, WIDTH, HEIGHT};
}

Screen::~Screen() = default;

RenderBackend& Screen::backend() { return *m_backend; }

const GLBackend& Screen::gl() const { return m_backend->gl(); }

RenderReadiness Screen::readiness() const { return m_backend->readiness(); }

Picture Screen::picture() const {
    // Into the test's own directory, which a failure keeps: what it saw, to look at.
    Picture taken = m_backend->last();
    writeScreenshotFile(
        (scratch() / ("frame_" + std::to_string(++m_taken) + ".png")).string(),
        taken.width,
        taken.height,
        taken.rgb
    );
    return taken;
}

void Screen::play(int frames, const std::function<void(int frame)>& each, int steps) {
    int ready  = 0;
    int played = 0;
    game.run(READY_LIMIT + frames, [&](int) {
        Clock& clock = game.clock();
        if (ready < SETTLE_FRAMES) {
            const RenderReadiness now = m_backend->readiness();
            ready = now.ready() && !now.splashPending ? ready + 1 : 0;
            if (ready == SETTLE_FRAMES) clock.requestStep(steps);
            return;
        }
        ++played;
        if (each) each(played);
        if (played >= frames) game.engine.getWindow().requestClose();
        else clock.requestStep(steps);
    }, 0);
    REQUIRE(played == frames);
}

Picture Screen::shot(const std::function<void(int frame)>& before) {
    Picture last;
    play(PAST_THE_IDENT, [&](int frame) {
        if (frame < PAST_THE_IDENT && before) before(frame);
        if (frame == PAST_THE_IDENT) last = picture();
    }, 16);
    return last;
}

std::vector<Picture> Screen::shots(const std::vector<std::function<void()>>& changes, int settle) {
    std::vector<Picture> taken;
    const int frames = PAST_THE_IDENT + static_cast<int>(changes.size()) * settle;
    play(frames, [&](int frame) {
        const int after = frame - PAST_THE_IDENT;
        if (after < 0 || after % settle != 0) return;
        taken.push_back(picture());
        const size_t next = static_cast<size_t>(after / settle);
        if (next < changes.size()) changes[next]();
    }, 16);
    return taken;
}

bool isFreshStart() {
    const char* again = std::getenv("VKM_TEST_AGAIN");
    return again && std::string(again) == "fresh";
}

void keepFreshStart(const Picture& frame) {
    const char* path = std::getenv("VKM_FRESH_PICTURE");
    REQUIRE(path != nullptr);
    REQUIRE(writeScreenshotFile(path, frame.width, frame.height, frame.rgb));
}

Picture freshStart(const std::string& test) {
    const std::filesystem::path kept = scratch() / "fresh.rgb";
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    REQUIRE(!ec);
    setenv("VKM_TEST_AGAIN", "fresh", 1);
    setenv("VKM_FRESH_PICTURE", kept.c_str(), 1);
    Platform::ChildProcess run;
    const bool started = run.start(self, {"render: " + test}, std::filesystem::current_path(ec));
    unsetenv("VKM_TEST_AGAIN");
    unsetenv("VKM_FRESH_PICTURE");
    REQUIRE(started);
    std::string said;
    while (run.running()) {
        said += run.takeOutput();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    said += run.takeOutput();

    Picture frame;
    const std::string rgb = readText(kept);
    if (run.exitCode() != 0 || rgb.size() != static_cast<size_t>(WIDTH) * HEIGHT * 3) {
        fail(__FILE__, __LINE__, "the fresh start kept no frame; it said:\n" + said);
        return frame;
    }
    frame.width  = WIDTH;
    frame.height = HEIGHT;
    frame.rgb.assign(rgb.begin(), rgb.end());
    return frame;
}

void matchesReference(const Picture& seen, const std::string& name, const Device& device) {
    REQUIRE(seen.width == WIDTH);
    std::error_code ec;
    const std::filesystem::path pictures = engineDir() / "tests" / "render" / "opengl" / "pictures";
    const std::filesystem::path file     = pictures / (name + ".png");
    if (asked("VKM_UPDATE_PICTURES")) {
        std::filesystem::create_directories(pictures, ec);
        CHECK(writeScreenshotFile(file.string(), seen.width, seen.height, seen.rgb));
        writeText(pictures / "renderer.txt", device.renderer() + "\n");
        return;
    }

    std::string made = readText(pictures / "renderer.txt");
    while (!made.empty() && (made.back() == '\n' || made.back() == '\r')) made.pop_back();
    if (made != device.renderer()) {
        skip("the reference pictures were made on " + made + ", this GPU is " + device.renderer());
    }

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(file.string().c_str(), &width, &height, &channels, 3);
    REQUIRE(pixels != nullptr);
    REQUIRE(width == static_cast<int>(WIDTH));
    REQUIRE(height == static_cast<int>(HEIGHT));
    Picture reference;
    reference.width  = static_cast<uint32_t>(width);
    reference.height = static_cast<uint32_t>(height);
    reference.rgb.assign(pixels, pixels + static_cast<size_t>(width) * height * 3);
    stbi_image_free(pixels);

    const float apart = differing(seen, reference);
    CHECK(apart <= 0.001f);
    if (apart <= 0.001f) return;

    // What it drew, and where it differs: white where a channel is past the tolerance.
    const std::filesystem::path out = buildDir() / "test_pictures";
    std::filesystem::create_directories(out, ec);
    Picture where = seen;
    for (size_t i = 0; i < where.rgb.size(); i += 3) {
        bool off = false;
        for (size_t c = 0; c < 3; ++c) off = off || std::abs(
            int(seen.rgb[i + c]) - int(reference.rgb[i + c])
        ) > 8;
        for (size_t c = 0; c < 3; ++c) where.rgb[i + c] = off ? 255 : seen.rgb[i + c] / 4;
    }
    writeScreenshotFile((out / (name + ".png")).string(), seen.width, seen.height, seen.rgb);
    writeScreenshotFile((out / (name + "_diff.png")).string(), seen.width, seen.height, where.rgb);
}

} // namespace Vkm::Test
