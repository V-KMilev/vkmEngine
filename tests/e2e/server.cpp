// vkm_server run as a user runs it, joined by a player in this process as vkm_runtime --connect joins:
// it serves with no display, seats a player, refuses one whose game is not its own, and stops in order.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#if !defined(_WIN32)
    #include <csignal>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include "vkm_network/address.h"
#include "vkm_network/udp_socket.h"

#include "core/project_boot.h"
#include "io/asset/asset_library.h"
#include "io/project/project.h"
#include "net/net_session.h"
#include "net/wire/codecs.h"
#include "vkm_platform/process/child_process.h"
#include "system/script/module/script_module.h"

#include "e2e/game_project.h"
#include "e2e/hosts.h"
#include "fixture/fixture.h"
#include "harness/check.h"
#include "harness/scratch.h"
#include "harness/world.h"

using namespace Vkm::Test;
using namespace Vkm::Network;
using namespace Vkm::Platform;

namespace {

/// A port nothing on this machine holds now.
uint16_t freePort() {
    UdpSocket probe;
    REQUIRE(probe.open(0));
    return probe.localAddress().port;
}

/**
 * @brief vkm_server serving the open project, started as a user starts one.
 */
struct Server {
    ChildProcess process;
    uint16_t     port    = 0;
    std::string  output;  ///< What it printed so far.
    bool         serving = false;

    explicit Server(const std::filesystem::path& root) {
        noDisplay();
        // A port freed for it may be taken before it binds: then another.
        for (int attempt = 0; attempt < 5 && !serving; ++attempt) {
            port = freePort();
            output.clear();
            REQUIRE(process.start(hostPath("vkm_server"), {root.string(), "--port", std::to_string(port)}));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (process.running() && std::chrono::steady_clock::now() < deadline && !says("Serving '")) {
                output += process.takeOutput();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            output += process.takeOutput();
            serving = says("Serving '");
            if (!serving) process.stop();
        }
        if (!serving) std::printf("%s", output.c_str());
    }

    bool says(const std::string& text) const { return output.find(text) != std::string::npos; }

    /// Send it SIGTERM, as a service manager stops one, and wait for it.
    void terminate() {
        process.stop();
        output += process.takeOutput();
    }
};

/// A project a server can serve: the fixture's module and a cooked scene of one crate.
std::filesystem::path servedProject() {
    const std::filesystem::path root = openProject("game");
    writeProjectFile("scenes/main.json");
    writeMeshRecipe("game:crate", "cube");
    authorScene("scenes/main.json", {{"Crate", "game:crate", {0.0f, 0.5f, 0.0f}, 1.0f}});
    installModule("game_v1");
    const Ran cook = runHost("vkm_cook", {root.string()});
    REQUIRE(cook.exit == 0);
    return root;
}

/**
 * @brief Play @p client against a server on its own clock, a tick a frame at about its rate, until
 *        @p done says so or a minute is up.
 */
void playAgainst(Game& client, const std::function<bool()>& done) {
    client.run(60 * 60, [&](int) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (done()) client.engine.getWindow().requestClose();
    });
}

} // namespace

TEST("vkm_server serves a project with no display, seats a player who joins, and on SIGTERM says goodbye and stops") {
    const std::filesystem::path root = servedProject();
    Server server(root);
    REQUIRE(server.serving);

    // A player's end, as vkm_runtime --connect stands one up.
    ScriptModule module;
    REQUIRE(bootGameplayModule(module, "playing"));
    Project project;
    loadProject(root, project);
    Game client;
    REQUIRE(bootProjectWorld(
        project,
        client.scene(),
        client.resources(),
        client.clock(),
        client.engine.getInput(),
        client.engine.getNet()
    ).source == SceneBoot::Project);
    NetSession& net = client.engine.getNet();
    REQUIRE(module.setupNetwork(net));
    REQUIRE(client.join(Address{0x7F000001u, server.port}, project.tickRate));

    // Seated once its character stands where the server put it; then the server is stopped. Read in
    // play: the client's own end closes the session when it stops.
    glm::vec3   seatedAt(0.0f);
    bool stopped = false;
    bool dropped = false;
    std::string why;
    playAgainst(client, [&] {
        const Transform* mine = client.scene().tryGet<Transform>(net.localEntity());
        if (!stopped && net.isPlaying() && mine && mine->position.x > 1.0f) {
            seatedAt = mine->position;
            stopped  = true;
            server.terminate();
        }
        dropped = stopped && net.isDisconnected();
        why     = net.lastError();
        return dropped;
    });

    REQUIRE(stopped);
    // Player 1 stands 3 m along X (tests/fixture/game.cpp).
    CHECK_NEAR(seatedAt, glm::vec3(3.0f, 0.0f, 0.0f), 1e-3f);
    CHECK(countIn(server.output, "Player 1 joined") == 1);
    CHECK(dropped);
    CHECK(why == "the server closed the game");
    CHECK(countIn(server.output, "Stopped serving") == 1);
}

TEST("a server refuses a player whose game replicates other types, and both ends say what they hold") {
    const std::filesystem::path root = servedProject();
    Server server(root);
    REQUIRE(server.serving);

    // A build without the game's module: its world reads, its behaviors held as text, and it replicates
    // the engine's types alone.
    AssetLibrary::get().load(AssetLibrary::Truth::Manifest);
    registerEngineNetTypes();
    Project project;
    loadProject(root, project);
    Game client;
    REQUIRE(bootProjectWorld(
        project,
        client.scene(),
        client.resources(),
        client.clock(),
        client.engine.getInput(),
        client.engine.getNet()
    ).source == SceneBoot::Project);
    NetSession& net = client.engine.getNet();
    REQUIRE(client.join(Address{0x7F000001u, server.port}, project.tickRate));

    bool seated  = false;
    bool refused = false;
    playAgainst(client, [&] {
        seated  = net.isPlaying();
        refused = net.isDisconnected();
        return seated || refused;
    });
    server.terminate();

    CHECK(!seated);
    CHECK(refused);
    CHECK(net.lastError() == "a different build or world");
    CHECK(countInLog("This end replicates: ") == 1);
    CHECK(countInLog("Health") == 0);
    CHECK(countIn(server.output, "Refusing a join: a different build or project. This end replicates: ") == 1);
    CHECK(countIn(server.output, ", Health") >= 2);
    CHECK(countIn(server.output, "Stopped serving") == 1);
}

TEST("two servers on one project each keep a log of their own") {
    const std::filesystem::path root = servedProject();
    Server first(root);
    REQUIRE(first.serving);
    Server second(root);
    REQUIRE(second.serving);
    const std::string firstLog  = readText(root / "logs" / "server.log");
    const std::string secondLog = readText(root / "logs" / "server-2.log");
    first.terminate();
    second.terminate();

    // Each host's banner once, in a file of its own.
    CHECK(countIn(firstLog, "Build Information") == 1);
    CHECK(countIn(secondLog, "Build Information") == 1);
}

TEST("a server stopped while it boots ends at its first frame, though it was started with SIGINT ignored") {
#if defined(_WIN32)
    skip("a SIGINT its parent ignored is POSIX's");
#else
    const std::filesystem::path root = servedProject();
    const std::string program = hostPath("vkm_server").string();
    const std::string project = root.string();
    const std::string port    = std::to_string(freePort());
    noDisplay();

    // Started as a background job is, SIGINT ignored, which exec keeps; its module takes seconds to
    // load (tests/fixture/game.cpp), so the stop comes while it boots. Nothing but what a forked child
    // of a threaded process may call runs before the exec.
    setenv("VKM_FIXTURE_LOAD_SECONDS", "3", 1);
    const pid_t host = fork();
    if (host == 0) {
        std::signal(SIGINT, SIG_IGN);
        const int quiet = open("/dev/null", O_WRONLY);
        dup2(quiet, STDOUT_FILENO);
        dup2(quiet, STDERR_FILENO);
        execl(
            program.c_str(),
            program.c_str(),
            project.c_str(),
            "--port",
            port.c_str(),
            static_cast<char*>(nullptr)
        );
        _exit(127);
    }
    unsetenv("VKM_FIXTURE_LOAD_SECONDS");
    REQUIRE(host > 0);

    // Its log opens with its first lines, before the module loads.
    const std::filesystem::path log = root / "logs" / "server.log";
    const auto booting = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (countIn(readText(log), "Build Information") == 0 && std::chrono::steady_clock::now() < booting) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    kill(host, SIGINT);

    int status = 0;
    bool ended = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!ended && std::chrono::steady_clock::now() < deadline) {
        ended = waitpid(host, &status, WNOHANG) == host;
        if (!ended) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!ended) {
        kill(host, SIGKILL);
        waitpid(host, &status, 0);
    }
    CHECK(ended);
    CHECK((WIFEXITED(status) && WEXITSTATUS(status) == 0));
    CHECK(countIn(readText(log), "Stopped serving") == 1);
#endif
}
