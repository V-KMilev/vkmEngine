#include "support.h"

#include <cstring>

#include "platform/threading/thread_pool.h"

#include "suites.h"

namespace {

struct Suite {
    const char* name;
    void (*run)();
};

// Expanded from the list in suites.h, so a declared suite cannot be left out here.
#define VKM_SUITE_ROW(name, entry, subject) {name, entry},
constexpr Suite SUITES[] = {
    VKM_TEST_SUITES(VKM_SUITE_ROW)
};
#undef VKM_SUITE_ROW

// The heading comes from the table, so a failure is found under the suite it ran in.
void run(const Suite& suite) {
    std::printf("\n=== %s ===\n", suite.name);
    g_suite = suite.name;
    suite.run();
}

void listSuites() {
    std::printf("Suites: ");
    for (const Suite& suite : SUITES) std::printf("%s ", suite.name);
    std::printf(
        "\n\nRun all of them with no arguments, or name the ones you want:\n"
        "    vkm_engine_tests solver wire\n"
    );
}

int runSuites(int argc, char** argv) {
    // Engine code asserts and logs through vkmLog, so the logger must exist before a Scene.
    // FATAL only: the suites provoke errors on purpose, and a check, not the log, says what failed.
    // The temp directory keeps the file out of the cwd.
    const std::filesystem::path logPath =
        std::filesystem::temp_directory_path() / "vkm_engine_tests.log";
    Vkm::Log::Logger::init(logPath.string(), "VKM_ENGINE-TESTS", Vkm::Log::LogLevel::FATAL);

    if (argc > 1 && (std::strcmp(argv[1], "--list") == 0 || std::strcmp(argv[1], "-l") == 0)) {
        listSuites();
        return 0;
    }

    // A name that matches nothing fails: it is almost always a typo, and an empty run reports success.
    int ran = 0;
    for (int i = 1; i < argc; ++i) {
        bool matched = false;
        for (const Suite& suite : SUITES) {
            if (std::strcmp(argv[i], suite.name) != 0) continue;
            run(suite);
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
        for (const Suite& suite : SUITES) run(suite);
    }

    // Repeated at the end, where ctest still shows them: it cuts the middle of a long output.
    if (!g_failed.empty()) std::printf("\nFailed:\n");
    for (const std::string& failed : g_failed) std::printf("  %s\n", failed.c_str());
    std::printf(g_failed.empty() ? "\nALL OK\n" : "\n%zu FAILURE(S)\n", g_failed.size());
    return g_failed.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const int status = runSuites(argc, argv);
    // See ThreadPool::shutdown.
    Vkm::Engine::ThreadPool::get().shutdown();
    return status;
}
