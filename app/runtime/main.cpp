#define VKM_LOG_CATEGORY "MAIN"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "logger.h"

#include "core/engine.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "net/wire/codecs.h"
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
 * Kept as text rather than an address, because the port to fall back on is the
 * project's and the project has not been read when the arguments are checked.
 */
struct NetArgs {
    std::string server;
};

/**
 * @brief Read --connect out of the command line.
 *
 * Not positional, so the project directory stays argv[1].
 *
 *   --connect host[:port]  join the game there
 *
 * There is no --host. Hosting is vkm_server, which is a host of its own rather
 * than a mode of this one - and the reason is not only that what a process is,
 * is which executable was run. A runtime that hosted would be playing a
 * character that is also the authority: never predicted, never corrected. The
 * configuration a developer runs most would then be exercising the client path
 * for only half of its players, which is the half of the engine most likely to
 * be wrong.
 *
 * An argument this does not recognise is refused rather than skipped. Skipping
 * is what makes a mistyped flag silent: `--conect 10.0.0.4` would open an
 * ordinary single-player game and say nothing about the game it did not join.
 *
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
        // Project root, working directory and log file, in the one order that
        // works (see tools/project_boot.h); argv[1] overrides the project beside
        // this executable, which is how one build runs several.
        if (!Vkm::Engine::bootHost(argc, argv, "log.log", "VKM-ENGINE")) return EXIT_FAILURE;

        // Before a window, a project or a gameplay module: a mistyped argument
        // is the first thing anyone gets wrong, and the cheapest moment to say
        // so is before any of the expensive, failable steps below.
        NetArgs net;
        if (!readNetArgs(argc, argv, net)) return EXIT_FAILURE;

        const std::filesystem::path root = Vkm::Engine::ProjectPaths::projectRoot();
        std::error_code ec;

        // Declared before the Engine so it outlives one: behaviors are
        // destroyed during Engine teardown and their code must still be mapped.
        Vkm::Engine::ScriptModule scriptModule;
        if (!Vkm::Engine::bootGameplayModule(scriptModule, "playing")) return EXIT_FAILURE;

        // Absent or unreadable leaves the defaults - a nameless project with no
        // entry scene - so a project.json that could not be read still plays, on
        // the world its module builds.
        Vkm::Engine::Project project;
        Vkm::Engine::loadProject(root, project);

        Vkm::Engine::Engine engine;

        const std::string title = project.name;
        auto sys = setupEngineApp(engine, AppConfig{
            title.c_str(),
            /*startPaused=*/false,
            /*logFps=*/true});

        // The backend is the host's choice, not the bootstrap's - see the
        // comment where setupEngineApp declines to make it.
        Vkm::GL::enableGLDebugLogging(false);
        sys.render.setBackend(std::make_unique<Vkm::Engine::GLBackend>());

        engine.getClock().setTickRate(project.tickRate);

        // Anything but the project's own world leaves this game nothing to play:
        // its entry scene did not load, or it names none and its module builds
        // none, and both leave the runtime on the engine's default scene.
        const Vkm::Engine::SceneBootResult boot = Vkm::Engine::bootProjectScene(
            project, scriptModule, engine.getScene(), engine.getResources());
        if (boot.source != Vkm::Engine::SceneBoot::Project) {
            LOG_ERROR("Project '%s' has no world of its own to play", project.name.c_str());
            return EXIT_FAILURE;
        }

        // Which world this is, so the two ends can agree they loaded the same
        // one. A slot is an entity's name on the wire, and two ends whose
        // scenes differ agree on every name and mean a different thing by each.
        engine.getNet().setWorld(Vkm::Engine::fingerprintScene(boot.path));

        // After the scene, because a spawner may reference what the world holds,
        // and after the module is loaded, because the entry lives in it.
        if (!net.server.empty()) {
            // Resolved here rather than with the arguments, so an address with
            // no port falls back to the project's. A name that does not resolve
            // is a network failure rather than a command-line one.
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
            if (!engine.getNet().connect(server)) return EXIT_FAILURE;
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
