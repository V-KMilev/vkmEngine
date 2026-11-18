#include "harness/world.h"

#include <system_error>
#include <utility>

#include "io/project/roots.h"

namespace Vkm::Test {

namespace {

/**
 * @brief The last system of every frame: asks the next frame's ticks, lets the test look, and ends the run.
 */
class Driver : public System {
    public:
        Driver(int frames, int steps, std::function<void(int)> each)
            : m_frames(frames)
            , m_steps(steps)
            , m_each(std::move(each))
        {}

        void update(FrameContext& ctx) override {
            ++m_frame;
            if (m_each) m_each(m_frame);
            if (m_frame >= m_frames) ctx.window.requestClose();
            else ctx.clock.requestStep(m_steps);
        }

    private:
        int                      m_frames = 0;
        int                      m_steps  = 0;
        int                      m_frame  = 0;
        std::function<void(int)> m_each;
};

App::AppConfig headless() {
    App::AppConfig config;
    config.windowTitle = "vkm_engine_tests";
    return config;
}

} // namespace

Game::Game(uint32_t tickRate)
    : systems(App::setupEngineApp(engine, headless(), nullptr))
{
    engine.getClock().setTickRate(tickRate);
    engine.getWindow().setFramerate(0);
}

Game::~Game() = default;

void Game::run(int frames, const std::function<void(int frame)>& each, int steps) {
    engine.addSystem<Driver>(SystemStage::Editor, frames, steps, each);
    engine.getClock().setPaused(true);
    engine.getClock().requestStep(steps);
    engine.run();
}

void Frame::tick(System& system, int ticks) {
    clock.setPaused(true);
    for (int i = 0; i < ticks; ++i) {
        clock.requestStep(1);
        clock.beginFrame();
        while (clock.consumeFixedStep()) {
            events.flush();
            system.fixedUpdate(ctx);
        }
    }
    events.flush();
}

std::filesystem::path openProject(const std::string& name) {
    const std::filesystem::path root = scratch() / name;
    std::error_code ec;
    std::filesystem::create_directories(root / "scenes", ec);
    std::filesystem::create_directories(root / "prefabs", ec);
    Roots::setProjectRoot(root);
    return root;
}

std::filesystem::path engineDir() { return Roots::engineRoot(); }

} // namespace Vkm::Test
