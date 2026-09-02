#include "support.h"
#include "suites.h"

#include <cstring>

namespace {

struct Suite {
    const char* name;
    void (*run)();
};

// In dependency order, roughly: what everything is built from first, so a
// failure in the bit stream is read before the sixty failures it causes in the
// session that rides on it.
constexpr Suite SUITES[] = {
    {"core",        runCoreTests},
    {"ecs",         runEcsTests},
    {"physics",     runPhysicsTests},
    {"scene",       runSceneTests},
    {"culling",     runCullingTests},
    {"animation",   runAnimationTests},
    {"particle",    runParticleTests},
    {"wire",        runNetWireTests},
    {"transport",   runNetTransportTests},
    {"replication", runNetReplicationTests},
    {"prediction",  runNetPredictionTests},
    {"session",     runNetSessionTests},
};

void listSuites() {
    std::printf("Suites: ");
    for (const Suite& suite : SUITES) std::printf("%s ", suite.name);
    std::printf("\n\nRun all of them with no arguments, or name the ones you want:\n"
                "    vkm_engine_tests physics wire\n");
}

} // namespace

int main(int argc, char** argv) {
    // Engine code asserts and logs through vkmLog, so the logger has to exist
    // before a Scene does. ERROR level keeps expected noise out of the output,
    // and the temp directory keeps the file out of wherever ctest was run from.
    const std::filesystem::path logPath =
        std::filesystem::temp_directory_path() / "vkm_engine_tests.log";
    Vkm::Log::Logger::init(logPath.string(), "VKM_ENGINE-TESTS",
                           Vkm::Log::LogLevel::ERROR);

    if (argc > 1 && (std::strcmp(argv[1], "--list") == 0 || std::strcmp(argv[1], "-l") == 0)) {
        listSuites();
        return 0;
    }

    // A name that matches nothing is a failure, not an empty run: it is almost
    // always a typo, and a suite that silently runs nothing reports success.
    int ran = 0;
    for (int i = 1; i < argc; ++i) {
        bool matched = false;
        for (const Suite& suite : SUITES) {
            if (std::strcmp(argv[i], suite.name) != 0) continue;
            suite.run();
            ++ran;
            matched = true;
        }
        if (!matched) {
            std::printf("No suite called '%s'.\n", argv[i]);
            listSuites();
            return 2;
        }
    }

    if (ran == 0) {
        for (const Suite& suite : SUITES) suite.run();
    }

    std::printf(g_failures ? "\n%d FAILURE(S)\n" : "\nALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}
