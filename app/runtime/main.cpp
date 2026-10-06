#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "logger.h"

#include "core/engine.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "net/net_session.h"
#include "project_boot.h"
#include "system/script/script_module.h"
#include "gl_backend.h"
#include "gl_debug.h"

#include "app/engine_app.h"

namespace {

/**
 * @brief What the command line asked for: a game to join, or none.
 *
 * Text, not an address: the fallback port is the project's, not yet read here.
 */
struct NetArgs {
    std::string server;
};

/**
 * @brief Read --connect out of the command line.
 *
 * `--connect host[:port]` joins a game. There is no --host: hosting is
 * vkm_server. An unrecognised argument is refused, not skipped.
 *
 * @param argc Argument count as main received it.
 * @param argv Arguments as main received them; flags start at firstFlagIndex.
 * @param args Filled when every argument was understood.
 * @return False when one was not, having said which.
 */
bool readNetArgs(int argc, char** argv, NetArgs& args) {
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
        } else if (arg == "--host") {
            LOG_ERROR("There is no --host: hosting a game is vkm_server, a host of its own");
            LOG_ERROR("To host and play, serve the project and join it:");
            LOG_ERROR("    vkm_server <project> &");
            LOG_ERROR("    vkm_runtime <project> --connect 127.0.0.1");
            return false;
        } else {
            LOG_ERROR("Unrecognised argument '%s'", arg.c_str());
            LOG_ERROR("Usage: vkm_runtime [project] [--connect host[:port]]");
            LOG_ERROR("To host a game, run vkm_server instead");
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    try {
        // First: everything below logs to its file and resolves paths against its
        // project - argv[1], or the one beside this executable.
        if (!Vkm::Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE")) return EXIT_FAILURE;

        // Before anything expensive or failable: a mistyped argument fails fast.
        NetArgs net;
        if (!readNetArgs(argc, argv, net)) return EXIT_FAILURE;

        const std::filesystem::path root = Vkm::Engine::ProjectPaths::projectRoot();

        // Declared before the Engine so it outlives it: behaviors are destroyed during
        // Engine teardown and need their code mapped.
        Vkm::Engine::ScriptModule scriptModule;
        if (!Vkm::Engine::bootGameplayModule(scriptModule, "playing")) return EXIT_FAILURE;

        // Absent or unreadable leaves the defaults (no entry scene), which still plays
        // the world its module builds.
        Vkm::Engine::Project project;
        Vkm::Engine::loadProject(root, project);

        Vkm::Engine::Engine engine;

        Vkm::App::AppConfig config;
        config.windowTitle = project.name.c_str();
        config.logFps      = true;
        auto sys = Vkm::App::setupEngineApp(engine, config);

        Vkm::GL::enableGLDebugLogging(false);
        sys.render.setBackend(std::make_unique<Vkm::Engine::GLBackend>(), engine.getWindow());
        sys.render.releaseUploadedPixels(true);

        // Applied before the world is built.
        engine.getRenderSettings() = project.render;

        for (const Vkm::Engine::SplashEntry& entry : project.splash) {
            const std::string image = Vkm::Engine::ProjectPaths::resolveProjectPath(entry.image).string();
            sys.splash.add(image, entry.seconds);
        }

        const Vkm::Engine::SceneBootResult boot = Vkm::Engine::bootProjectWorld(
            project,
            scriptModule,
            engine.getScene(),
            engine.getResources(),
            engine.getClock(),
            engine.getNet()
        );
        if (boot.source != Vkm::Engine::SceneBoot::Project) {
            LOG_ERROR("Project '%s' has no world of its own to play", project.name.c_str());
            return EXIT_FAILURE;
        }

        // After the scene, which a spawner may reference, and the module, which holds the entry.
        if (!net.server.empty()) {
            // Resolved here, not with the arguments, so a missing port falls back to the project's.
            const Vkm::Engine::NetAddress server =
                Vkm::Engine::NetAddress::parse(net.server, project.netPort);
            if (server.port == 0) {
                LOG_ERROR("'%s' is not an address this machine can reach", net.server.c_str());
                return EXIT_FAILURE;
            }
            if (!scriptModule.setupNetwork(engine.getNet())) {
                LOG_ERROR("This project has no vkmSetupNetwork entry, so it cannot join a game");
                return EXIT_FAILURE;
            }
            if (!engine.getNet().connect(server, project.tickRate)) return EXIT_FAILURE;
        }

        engine.run();

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        LOG_FATAL("Unknown exception");
        return EXIT_FAILURE;
    }

    LOG_INFO("Shutdown successfully!");
    return 0;
}
