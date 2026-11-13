#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "vkm_log/logger.h"

#include "core/engine.h"
#include "core/host_chrome.h"
#include "base/reflect/enum_names.h"
#include "io/project/project.h"
#include "io/project/roots.h"
#include "net/net_session.h"
#include "vkm_platform/process/discrete_gpu.h"
#include "font_baker.h"
#include "core/project_boot.h"
#include "io/project/host_boot.h"
#include "core/render_mode.h"
#include "system/script/module/script_module.h"

#include "app/backend_choice.h"
#include "app/engine_app.h"
#include "app/runtime/bench_system.h"

namespace {

/**
 * @brief What the command line asked for.
 *
 * Text, not an address: the fallback port is the project's, not yet read here.
 */
struct Args {
    std::string             server;            ///< A game to join, or empty.
    std::string             bench;             ///< A bench plan to run, or empty.
    bool                    windowed = false;  ///< A run vkm starts: kept in a window, whatever the player
                                               ///< chose.
    Vkm::Engine::RenderMode view{};            ///< Shown in place of the picture: a debug view, or Lit,
                                               ///< the picture itself.
};

/**
 * @brief Every debug view's name, as `--view` takes them.
 *
 * @return The names, comma-separated.
 */
std::string viewNames() {
    using Names = Vkm::Engine::Reflect::EnumNames<Vkm::Engine::RenderMode>;
    std::string names;
    for (size_t i = 0; i < Names::count; ++i) names += (i ? ", " : "") + std::string(Names::values[i]);
    return names;
}

/**
 * @brief Read the flags out of the command line.
 *
 * `--connect host[:port]` joins a game; hosting is vkm_server. `--bench <file>` times the views the
 * plan names and exits. `--windowed` keeps a development run in a window, so its frames compare
 * with the last run's. `--view <name>` shows a debug view (`RenderMode`) in place of the picture,
 * as the editor's viewport can. An unrecognized argument is refused, not skipped.
 *
 * @param argc Argument count as main received it.
 * @param argv Arguments as main received them; flags start at firstFlagIndex.
 * @param args Filled when every argument was understood.
 * @return False when one was not, having said which.
 */
bool readArgs(int argc, char** argv, Args& args) {
    for (int i = Vkm::Engine::firstFlagIndex(argc, argv); i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--connect") {
            if (i + 1 >= argc) {
                LOG_ERROR("--connect needs an address to join, like --connect 10.0.0.4");
                return false;
            }
            args.server = argv[++i];
            if (args.server.empty()) {
                LOG_ERROR("--connect needs an address to join, like --connect 10.0.0.4");
                return false;
            }
        } else if (arg == "--windowed") {
            args.windowed = true;
        } else if (arg == "--view") {
            if (i + 1 >= argc || !Vkm::Engine::Reflect::enumFromNameChecked(argv[i + 1], args.view)) {
                LOG_ERROR("--view needs a debug view to show, one of: %s", viewNames().c_str());
                return false;
            }
            ++i;
        } else if (arg == "--bench") {
            if (i + 1 >= argc) {
                LOG_ERROR("--bench needs a plan to run, like --bench bench.json");
                return false;
            }
            args.bench = argv[++i];
        } else {
            LOG_ERROR("Unrecognized argument '%s'", arg.c_str());
            LOG_ERROR("Usage: vkm_runtime [project] [--connect host[:port]] [--bench plan.json]");
            LOG_ERROR("       [--windowed] [--view <debug view>]");
            LOG_ERROR("To host a game, run vkm_server instead");
            return false;
        }
    }
    return true;
}

} // namespace

VKM_PLATFORM_PREFER_DISCRETE_GPU()

int main(int argc, char** argv) {
    const int status = Vkm::Engine::runHost([&] {
        // First: everything below logs to its file and resolves paths against its
        // project - argv[1], or the one beside this executable.
        Vkm::Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE");

        // Before anything expensive or failable: a mistyped argument fails fast.
        Args args;
        if (!readArgs(argc, argv, args)) return EXIT_FAILURE;

        Vkm::App::BenchPlan benchPlan;
        if (!args.bench.empty() && !Vkm::App::loadBenchPlan(args.bench, benchPlan)) return EXIT_FAILURE;

        const std::filesystem::path root = Vkm::Engine::Roots::projectRoot();

        // Declared before the Engine so it outlives it: behaviors are destroyed during
        // Engine teardown and need their code mapped.
        Vkm::Engine::ScriptModule scriptModule;
        if (!Vkm::Engine::bootGameplayModule(scriptModule, "playing")) return EXIT_FAILURE;

        // Absent or unreadable leaves the defaults: no entry scene, so no world to play, which
        // the boot below reports.
        Vkm::Engine::Project project;
        Vkm::Engine::loadProject(root, project);

        Vkm::Engine::Engine engine;

        Vkm::App::AppConfig config;
        config.windowTitle = project.name.c_str();
        auto sys = Vkm::App::setupEngineApp(engine, config, Vkm::App::Backend::windowSurface());
        Vkm::Engine::ensureDefaultUIFont(engine.getResources(), engine.getWindow().displayHeight());

        // Last of its stage, so its camera wins over the game's.
        constexpr Vkm::Engine::SystemStage BENCH_STAGE = Vkm::Engine::SystemStage::Simulation;
        const Vkm::App::BenchSystem* bench = args.bench.empty()
            ? nullptr
            : &engine.addSystem<Vkm::App::BenchSystem>(BENCH_STAGE, std::move(benchPlan));

        // The host's display, as the editor's viewport sets it.
        if (args.view != Vkm::Engine::RenderMode::Lit) {
            Vkm::Engine::HostChrome::Display shown;
            shown.mode = args.view;
            engine.getChrome().display = shown;
        }

        sys.render.setBackend(std::make_unique<Vkm::App::Backend>(), engine.getWindow());
        sys.render.releaseUploadedPixels(true);

        // Applied before the world is built.
        Vkm::Engine::Graphics& graphics = engine.getGraphics();
        graphics.setProject(project.render);
        // The player's own picture; a bench measures the tiers it names.
        if (args.bench.empty()) {
            graphics.keepChoicesIn(Vkm::Engine::Roots::playerRoot(project.name) / "graphics.json");
            // A player's game fills the screen unless they chose a window. Asked before the first frame,
            // so the desktop's work on the switch lands under the splash and the loading cover, not in play.
            if (!args.windowed) {
                const auto mode = graphics.chosenWindowMode().value_or(Vkm::Platform::WindowMode::Fullscreen);
                engine.getWindow().updateMode(mode);
            }
        }
        const float fontPixels = Vkm::Engine::uiFontPixels(engine.getWindow().displayHeight());
        Vkm::Engine::bakeProjectFonts(engine.getResources(), project.fonts, fontPixels);

        for (const Vkm::Engine::SplashEntry& entry : project.splash) {
            const std::string image = Vkm::Engine::Roots::resolveProjectPath(entry.image).string();
            sys.splash.add(image, entry.seconds);
        }
        sys.loading->setScreen(project.loading);

        if (!Vkm::Engine::bootPlayedWorld(project, engine, sys.behaviors, "play")) return EXIT_FAILURE;
        // The player's own controls, which a rebinding writes back.
        engine.getInput().keepBindingsIn(Vkm::Engine::Roots::playerRoot(project.name) / "bindings.json");

        // After the scene, which a spawner may reference, and the module, which holds the entry.
        // Set up whether or not the command line joins a game, so the game can join one from
        // its own menu (NetSession::connect) with the schema its server expects.
        const bool networked = scriptModule.setupNetwork(engine.getNet());
        if (!args.server.empty()) {
            // Resolved here, not with the arguments, so a missing port falls back to the project's.
            const Vkm::Network::Address server =
                Vkm::Network::Address::parse(args.server, project.netPort);
            if (server.port == 0) {
                LOG_ERROR("'%s' is not an address this machine can reach", args.server.c_str());
                return EXIT_FAILURE;
            }
            if (!networked) {
                LOG_ERROR("This project has no vkmSetupNetwork entry, so it cannot join a game");
                return EXIT_FAILURE;
            }
            const Vkm::Engine::InputMap& input = engine.getInput();
            const bool joining = engine.getNet().connect(
                server,
                project.tickRate,
                input.actionCount(),
                input.actionFingerprint()
            );
            if (!joining) return EXIT_FAILURE;
        }

        engine.run();

        if (bench && !bench->passed()) return EXIT_FAILURE;

        return EXIT_SUCCESS;
    });
    // After the engine's teardown, which ends the run.
    if (status == EXIT_SUCCESS) LOG_INFO("Shutdown successfully!");
    return status;
}
