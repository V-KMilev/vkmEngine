#define VKM_LOG_CATEGORY "SERVER"

#include <cstdint>
#include <cstdlib>
#include <string>

#include "logger.h"

#include "core/engine.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "net/net_session.h"
#include "platform/net/net_address.h"
#include "project_boot.h"
#include "system/script/script_module.h"
#include "app/engine_app.h"

namespace {

/**
 * @brief Read --port out of the command line.
 *
 * An unrecognised argument is refused, not skipped.
 *
 * @param argc Argument count as main received it.
 * @param argv Arguments as main received them; flags start at firstFlagIndex.
 * @param port Set only when --port was given; leave it zero to detect that.
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
            if (!Vkm::Engine::NetAddress::parsePort(argv[++i], port)) {
                LOG_ERROR("'%s' is not a port to serve on", argv[i]);
                return false;
            }
        } else {
            LOG_ERROR("Unrecognised argument '%s'", arg.c_str());
            LOG_ERROR("Usage: vkm_server [project] [--port N]");
            return false;
        }
    }
    return true;
}

} // namespace

// The host that referees: no window, no render backend, nobody playing here.
// Why it is an executable, not a flag: docs/guides/engine.md 1.
int main(int argc, char** argv) {
    try {
        if (!Vkm::Engine::bootHost(argc, argv, "server.log", "VKM-SERVER")) return EXIT_FAILURE;

        // Before anything expensive or failable. Zero unless --port was given.
        uint16_t port = 0;
        if (!readPort(argc, argv, port)) return EXIT_FAILURE;

        Vkm::Engine::ScriptModule scriptModule;
        if (!Vkm::Engine::bootGameplayModule(scriptModule, "serving")) return EXIT_FAILURE;

        Vkm::Engine::Project project;
        Vkm::Engine::loadProject(Vkm::Engine::ProjectPaths::projectRoot(), project);

        Vkm::Engine::Engine engine;

        Vkm::App::AppConfig config;
        config.windowTitle       = project.name.c_str();
        config.headless          = true;
        config.headlessFrameRate = project.tickRate;
        Vkm::App::setupEngineApp(engine, config);

        const Vkm::Engine::SceneBootResult boot = Vkm::Engine::bootProjectWorld(
            project,
            scriptModule,
            engine.getScene(),
            engine.getResources(),
            engine.getClock(),
            engine.getNet()
        );
        if (boot.source != Vkm::Engine::SceneBoot::Project) {
            LOG_ERROR("Project '%s' has no world of its own to serve", project.name.c_str());
            return EXIT_FAILURE;
        }

        if (!scriptModule.setupNetwork(engine.getNet())) {
            LOG_ERROR(
                "Project '%s' has no vkmSetupNetwork entry, so it cannot take players",
                project.name.c_str()
            );
            return EXIT_FAILURE;
        }

        // The command line's port wins over the project's.
        const uint16_t listenPort = port != 0 ? port : project.netPort;
        if (!engine.getNet().host(listenPort, project.maxPlayers, project.tickRate)) {
            return EXIT_FAILURE;
        }

        LOG_INFO(
            "Serving '%s' at %u Hz for up to %u player(s)",
            project.name.c_str(),
            project.tickRate,
            project.maxPlayers
        );
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
