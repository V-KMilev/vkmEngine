// Runs one test, named on the command line, in this process; ctest starts one process a test
// (discover.cmake), so a crash, a hang or a process-wide singleton is one test's alone.
//
//     vkm_engine_tests --list
//     vkm_engine_tests "physics: a stack of boxes stands"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
    #include <malloc.h>
#endif

#include "vkm_log/logger.h"

#include "vkm_platform/process/this_process.h"
#include "vkm_platform/threading/thread_pool.h"
#include "system/script/behavior.h"

#include "harness/check.h"
#include "harness/scratch.h"

// A sanitizer brings an operator new and delete of its own, to watch every block: the counting
// replacements stay out of its way, and allocations() skips.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    #define VKM_TEST_SANITIZED
#elif defined(__has_feature)
    #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
        #define VKM_TEST_SANITIZED
    #endif
#endif

// The tests' own behaviors register as a module's do: VKM_BEHAVIOR lists them, this registers them.
VKM_GAME_MODULE()

namespace Vkm::Test {

namespace {

struct Entry {
    std::string area;
    std::string name;
    Body        body    = nullptr;
    bool        infoLog = false;  ///< TEST_WITH_INFO_LOG: its log keeps Info lines too.
};

std::vector<Entry>& entries() {
    static std::vector<Entry> s_entries;
    return s_entries;
}

int g_failures = 0;

std::filesystem::path g_scratch;

/// The folder under tests/ that holds @p file: the area its tests are listed under.
std::string areaOf(const char* file) {
    const std::string path = std::filesystem::path(file).generic_string();
    const size_t at = path.rfind("/tests/");
    if (at == std::string::npos) return "tests";
    const size_t start = at + 7;
    return path.substr(start, path.find('/', start) - start);
}

/// @p name as a folder name: what a file system takes, nothing else.
std::string folderFor(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (plain) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    return out.substr(0, 80);
}

void setEnvironment(const char* key, const std::filesystem::path& value) {
#if defined(_WIN32)
    _putenv_s(key, value.string().c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

int run(const Entry& entry) {
    const std::filesystem::path tree = buildDir();
    if (tree.empty()) {
        std::printf("The build tree cannot be found: '%s' has nowhere to run.\n", entry.name.c_str());
        return 2;
    }
    g_scratch = tree / "test_scratch" / entry.area / folderFor(entry.name);
    // A test that runs itself again in a process of its own (tests/render/opengl/gpu.h, freshStart) names
    // that run, whose directory sits beside its own.
    if (const char* again = std::getenv("VKM_TEST_AGAIN")) g_scratch += std::string("_") + again;
    std::error_code ec;
    std::filesystem::remove_all(g_scratch, ec);
    if (!ec) std::filesystem::create_directories(g_scratch / "home", ec);
    if (ec) {
        std::printf(
            "'%s' has no empty folder at %s: %s.\n",
            entry.name.c_str(),
            g_scratch.string().c_str(),
            ec.message().c_str()
        );
        return 2;
    }

    // vkm's pinned tools stay where it keeps them for the user (tools/vkmcli/toolchain.py, tools_cache):
    // on Windows under the LOCALAPPDATA the next lines move, so a test that runs vkm finds them, never
    // fetching them again into its scratch.
    const char* local = std::getenv("LOCALAPPDATA");
    if (local && !std::getenv("VKM_TOOLS_DIR")) {
        setEnvironment("VKM_TOOLS_DIR", std::filesystem::path(local) / "vkm" / "tools");
    }

    // A player's files and a host's log go where the test can see them, never the user's own.
    setEnvironment("XDG_CONFIG_HOME", g_scratch / "home");
    setEnvironment("XDG_STATE_HOME", g_scratch / "home");
    setEnvironment("APPDATA", g_scratch / "home");
    setEnvironment("LOCALAPPDATA", g_scratch / "home");

    // Warnings and up, so a test can read what a refusal said (logText); Info too for one that asks.
    const Log::Level least = entry.infoLog ? Log::Level::Info : Log::Level::Warning;
    Log::Logger::init((g_scratch / "test.log").string(), "VKM-TESTS", least);
    vkmRegisterBehaviors();

    std::printf("%s: %s\n", entry.area.c_str(), entry.name.c_str());
    try {
        entry.body();
    } catch (const Stop&) {
    } catch (const Skip& skip) {
        std::printf("SKIPPED: %s\n", skip.reason.c_str());
        // A check that failed before the skip still fails the test.
        if (g_failures == 0) return 77;
    } catch (const std::exception& e) {
        fail("", 0, std::string("threw ") + e.what());
    }
    std::printf(g_failures == 0 ? "ok\n" : "FAILED\n");
    return g_failures == 0 ? 0 : 1;
}

} // namespace

Registration::Registration(const char* name, const char* file, Body body, bool infoLog) {
    entries().push_back({areaOf(file), name, body, infoLog});
}

void fail(const char* file, int line, const std::string& what) {
    ++g_failures;
    const std::string where = file[0] ? std::filesystem::path(
        file
    ).filename().string() + ":" + std::to_string(line)
                                      : std::string("test");
    std::printf("  %s: %s\n", where.c_str(), what.c_str());
}

const std::filesystem::path& scratch() { return g_scratch; }

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary) << text;
}

std::filesystem::path buildDir() {
    return Platform::executableDir().parent_path();
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

std::string logText() { return readText(g_scratch / "test.log"); }

int countInLog(const std::string& text) {
    const std::string log = logText();
    int count = 0;
    for (size_t at = log.find(text); at != std::string::npos; at = log.find(text, at + text.size())) ++count;
    return count;
}

#if !defined(VKM_TEST_SANITIZED)

namespace {

std::atomic<uint64_t> g_allocations{0};

} // namespace

#endif

uint64_t allocations() {
#if defined(VKM_TEST_SANITIZED)
    skip("a sanitizer's own operator new is not counted");
#elif defined(_WIN32)
    skip("a DLL's own operator new, as the engine's, is not counted on Windows");
#else
    return g_allocations.load(std::memory_order_relaxed);
#endif
}

} // namespace Vkm::Test

#if !defined(VKM_TEST_SANITIZED)

namespace {

/// A block of @p size bytes, counted for allocations(); null when there is no memory for it.
void* counted(std::size_t size) {
    Vkm::Test::g_allocations.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(size ? size : 1);
}

/// A block of @p size bytes on an @p align boundary, counted; null when there is no memory for it.
void* countedAligned(std::size_t size, std::align_val_t align) {
    Vkm::Test::g_allocations.fetch_add(1, std::memory_order_relaxed);
    const std::size_t alignment = static_cast<std::size_t>(align);
#if defined(_WIN32)
    return _aligned_malloc(size ? size : 1, alignment);
#else
    // aligned_alloc takes a whole number of alignments.
    return std::aligned_alloc(alignment, (size / alignment + 1) * alignment);
#endif
}

/// Returns a block countedAligned gave.
void releaseAligned(void* block) {
#if defined(_WIN32)
    _aligned_free(block);
#else
    std::free(block);
#endif
}

} // namespace

// GCC 11 and up, inlining a delete here or in main, sees std::free take a block from operator new;
// these replacements are the pair that makes it right, so the warning is off to the end of the file.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11
    #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

// Every form, counted for allocations(), the engine's libraries' calls too where the platform resolves
// them here: not inside a Windows DLL, whose calls stay with its own runtime.
void* operator new(std::size_t size) {
    if (void* block = counted(size)) return block;
    throw std::bad_alloc();
}
void* operator new(std::size_t size, std::align_val_t align) {
    if (void* block = countedAligned(size, align)) return block;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void* operator new[](std::size_t size, std::align_val_t align) { return operator new(size, align); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    return countedAligned(size, align);
}
void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    return countedAligned(size, align);
}
void operator delete(void* block) noexcept { std::free(block); }
void operator delete[](void* block) noexcept { std::free(block); }
void operator delete(void* block, std::size_t) noexcept { std::free(block); }
void operator delete[](void* block, std::size_t) noexcept { std::free(block); }
void operator delete(void* block, const std::nothrow_t&) noexcept { std::free(block); }
void operator delete[](void* block, const std::nothrow_t&) noexcept { std::free(block); }
void operator delete(void* block, std::align_val_t) noexcept { releaseAligned(block); }
void operator delete[](void* block, std::align_val_t) noexcept { releaseAligned(block); }
void operator delete(void* block, std::size_t, std::align_val_t) noexcept { releaseAligned(block); }
void operator delete[](void* block, std::size_t, std::align_val_t) noexcept { releaseAligned(block); }
void operator delete(void* block, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned(block); }
void operator delete[](void* block, std::align_val_t, const std::nothrow_t&) noexcept {
    releaseAligned(block);
}

#endif

int main(int argc, char** argv) {
    using namespace Vkm::Test;

    // Unbuffered, as ctest reads it through a pipe: a crash keeps what was printed before it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::map<std::string, const Entry*> byName;
    for (const Entry& entry : entries()) {
        const std::string full = entry.area + ": " + entry.name;
        // ctest's lists split on ';'.
        if (full.find(';') != std::string::npos) {
            std::printf("A test's name holds a ';': '%s'.\n", full.c_str());
            return 2;
        }
        if (!byName.emplace(full, &entry).second) {
            std::printf("Two tests are called '%s'.\n", full.c_str());
            return 2;
        }
    }

    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
        for (const auto& [full, entry] : byName) std::printf("%s\n", full.c_str());
        return 0;
    }
    if (argc != 2) {
        std::printf("Usage: %s --list | \"<area>: <test>\"\n", argv[0]);
        return 2;
    }
    const auto found = byName.find(argv[1]);
    if (found == byName.end()) {
        std::printf("No test called '%s'; --list names them.\n", argv[1]);
        return 2;
    }

    const int status = run(*found->second);
    // Before the statics go: see ThreadPool::shutdown.
    Vkm::Platform::ThreadPool::get().shutdown();
    if (status == 0) {
        std::error_code ec;
        std::filesystem::remove_all(g_scratch, ec);
    }
    std::fflush(stdout);
    return status;
}
