#define VKM_LOG_CATEGORY "SERVER"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "logger.h"

#include "core/engine.h"
#include "core/engine_config.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "net/wire/codecs.h"
#include "net/net_session.h"
#include "project_boot.h"
#include "system/script/script_module.h"
#include "app/engine_app.h"

namespace {

/**
 * @brief Read --port out of the command line.
 *
 * Not positional, so the project directory stays argv[1] the way it is for
 * every other host. An argument this does not recognise is refused rather than
 * skipped, for the reason a flag was the wrong place to put an identity in the
 * first place: skipping is what makes a mistake silent.
 *
 * @param port Set only when --port was given, so a caller can tell "not given"
 *             from a port by leaving it zero.
 * @return False when an argument was not understood, having said which.
 */
bool readPort(int argc, char** argv, uint16_t& port) {
    for (int i = Vkm::Engine::firstFlagIndex(argc, argv); i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port") {
            if (i + 1 >= argc) {
                LOG_ERROR("--port needs a number to serve on");
                return false;
            }
            const int value = std::atoi(argv[++i]);
            if (value <= 0 || value > 65535) {
                LOG_ERROR("'%s' is not a port to serve on", argv[i]);
                return false;
            }
            port = static_cast<uint16_t>(value);
        } else {
            LOG_ERROR("Unrecognised argument '%s'", arg.c_str());
            LOG_ERROR("Usage: vkm_server [project] [--port N]");
            return false;
        }
    }
    return true;
}

} // namespace

// The host that referees: no window, no render backend, and nobody playing on
// this machine. Why hosting is an executable rather than a flag is
// docs/guides/engine.md 1.
int main(int argc, char** argv) {
    try {
        if (!Vkm::Engine::bootHost(argc, argv, "server.log", "VKM-SERVER")) return EXIT_FAILURE;

        // Before anything expensive or failable, as the runtime does. Left zero
        // when no --port was given, which is how the project's own answer wins.
        uint16_t port = 0;
        if (!readPort(argc, argv, port)) return EXIT_FAILURE;

        // A server reads meshes like the runtime does: mesh colliders are built
        // from their triangles, so the simulation needs them whether or not
        // anything draws them.
        Vkm::Engine::ScriptModule scriptModule;
        if (!Vkm::Engine::bootGameplayModule(scriptModule, "serving")) return EXIT_FAILURE;

        Vkm::Engine::Project project;
        Vkm::Engine::loadProject(Vkm::Engine::ProjectPaths::projectRoot(), project);

        Vkm::Engine::Engine engine;

        // No window, no backend, and the same system stack as a runtime. An
        // authority that simulated differently from the clients it corrects
        // would not be an authority.
        setupEngineApp(engine, AppConfig{
            project.name.c_str(),
            /*startPaused=*/false,
            /*logFps=*/false,
            /*headless=*/true,
            /*headlessFrameRate=*/Vkm::Engine::Config::clampTickRate(project.tickRate)});

        engine.getClock().setTickRate(project.tickRate);

        const Vkm::Engine::SceneBootResult boot = Vkm::Engine::bootProjectScene(
            project, scriptModule, engine.getScene(), engine.getResources());
        if (boot.source != Vkm::Engine::SceneBoot::Project) {
            LOG_ERROR("Project '%s' has no world of its own to serve", project.name.c_str());
            return EXIT_FAILURE;
        }

        // Which world this is, so the two ends can agree they loaded the same
        // one. A slot is an entity's name on the wire, and two ends whose
        // scenes differ agree on every name and mean a different thing by each.
        engine.getNet().setWorld(Vkm::Engine::fingerprintScene(boot.path));

        // A project that takes players says what one is. Without that entry
        // there is nothing to serve, unlike the runtime where its absence just
        // means a single-player game.
        if (!scriptModule.setupNetwork(engine.getNet())) {
            LOG_ERROR("Project '%s' has no vkmSetupNetwork entry, so it cannot take players",
                      project.name.c_str());
            return EXIT_FAILURE;
        }

        // How many players the game is for is the project's answer; where this
        // particular run listens is the command line's, when it gave one.
        if (!engine.getNet().host(port != 0 ? port : project.netPort,
                                  project.maxPlayers)) {
            return EXIT_FAILURE;
        }

        LOG_INFO("Serving '%s' at %u Hz for up to %u player(s)",
                 project.name.c_str(), project.tickRate, project.maxPlayers);
        engine.run();

    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        LOG_FATAL("Unknown exception");
        return EXIT_FAILURE;
    }

    LOG_INFO("Stopped serving");
    return 0;
}
