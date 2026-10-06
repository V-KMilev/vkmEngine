#pragma once

// Shared by the vkm_engine_tests suites: the assertion, the frame a system test
// stands up, and helpers used across areas. One area's helpers live beside it
// (net/net_support.h, physics/physics_support.h).
//
// Everything is inline because several translation units include it; the failure
// list must be one variable, or a suite could fail and the process still exit zero.
//
// These suites need no GL context or window, so the binary runs on a machine with
// no GPU; anything needing a live context belongs in the render suite.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/clock.h"
#include "core/event/event_bus.h"
#include "core/host_chrome.h"
#include "core/system.h"
#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "io/project_paths.h"
#include "net/net_session.h"
#include "platform/input/input_map.h"
#include "platform/window/window_manager.h"
#include "platform/windows_api.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/resource_manager.h"
#include "system/render/render_settings.h"

#if !defined(_WIN32)
    #include <unistd.h>
#endif

using namespace Vkm::Engine;

/// The suite running now, so a failed check is reported under it.
inline const char* g_suite = "";

/// Every failed check, as "suite: check", repeated when the run ends.
inline std::vector<std::string> g_failed;

/**
 * @brief This run's own directory under the system temp directory, removed when the run ends.
 *
 * Named for the process, so two runs at once - two worktrees, CI beside a
 * developer - do not write the same paths and fail each other at random.
 *
 * @return The directory, the same one for every call in the run.
 */
inline const std::filesystem::path& runScratch() {
    struct Directory {
        std::filesystem::path path;

        Directory() {
#if defined(_WIN32)
            const unsigned long id = static_cast<unsigned long>(::GetCurrentProcessId());
#else
            const unsigned long id = static_cast<unsigned long>(::getpid());
#endif
            path = std::filesystem::temp_directory_path() / ("vkm_tests_" + std::to_string(id));
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            std::filesystem::create_directories(path, ec);
        }

        ~Directory() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
    static const Directory s_directory;
    return s_directory.path;
}

/**
 * @brief A project root of this scope's own, emptied on the way in and out.
 *
 * Project content resolves its path through ProjectPaths, so a suite that writes
 * any must say where; this keeps it out of the tree and out of the check that
 * reads every checked-in data file. Starts with an empty "prefabs" directory.
 */
class ScratchProject {
    public:
        explicit ScratchProject(const char* name)
            : m_root(runScratch() / name) {
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);
            std::filesystem::create_directories(m_root / "prefabs", ec);
            m_previous = ProjectPaths::projectRoot();
            ProjectPaths::setProjectRoot(m_root);
        }

        ~ScratchProject() {
            ProjectPaths::setProjectRoot(m_previous);
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);
        }

        ScratchProject(const ScratchProject& other) = delete;
        ScratchProject& operator=(const ScratchProject& other) = delete;

        ScratchProject(ScratchProject && other) = delete;
        ScratchProject& operator=(ScratchProject && other) = delete;

    public:
        const std::filesystem::path& root() const { return m_root; }

    private:
        std::filesystem::path m_root;
        std::filesystem::path m_previous;
};

/**
 * @brief The angle between two rotations, in degrees.
 *
 * Through the absolute dot product, because q and -q are the same rotation and
 * the wire encoding drops the sign; otherwise identical rotations read as 360.
 *
 * @param a One rotation; need not be normalized.
 * @param b The other.
 * @return The angle taking one to the other, 0 to 180 degrees.
 */
inline float rotationErrorDegrees(const glm::quat& a, const glm::quat& b) {
    const float dot = std::abs(glm::dot(glm::normalize(a), glm::normalize(b)));
    return glm::degrees(2.0f * std::acos(std::min(1.0f, dot)));
}

/**
 * @brief The type id of an anonymous-namespace type core_tests.cpp declares.
 *
 * ecs_tests.cpp declares one spelled the same; only a second translation unit
 * can hold the twin a type id must tell apart.
 *
 * @return typeId of core_tests.cpp's TypeIdProbe.
 */
TypeId typeIdOfCoreTestsProbe();

/**
 * @brief The type id of a class local to a lambda a namespace-scope const holds.
 *
 * The const has internal linkage, so its mangled name carries no anonymous
 * namespace, and ecs_tests.cpp declares a twin spelled the same.
 *
 * @return typeId of that local class.
 */
TypeId typeIdOfCoreTestsConstantsProbe();

inline void check(const char* what, bool ok) {
    if (!ok) g_failed.push_back(std::string(g_suite) + ": " + (what + std::strspn(what, " ")));
    std::printf("  %-62s %s\n", what, ok ? "ok" : "<-- FAILED");
}

// Loose on purpose: the checks ask which answer came back, not how precisely.
inline bool nearly(float a, float b) {
    return std::fabs(a - b) < 1e-3f;
}

// The services a FrameContext refers to, so a system test stands one up in a line
// and a new FrameContext field is one edit here rather than one per suite.
struct TestFrame {
    ResourceManager resources;
    Clock           clock;
    EventBus        events;
    WindowManager   window;   // never given a window: nothing here draws
    InputMap        input;
    NetSession      ownedNet;
    HostChrome      chrome;
    RenderSettings  render;
    FrameContext    ctx;

    explicit TestFrame(Scene& scene)
        : ctx{scene, resources, clock, events, window, input, ownedNet, chrome, render} {}

    // For the networking suites: the context refers to the test's own session.
    TestFrame(Scene& scene, NetSession& session)
        : ctx{scene, resources, clock, events, window, input, session, chrome, render} {}

    // For components naming assets the test made before the frame.
    TestFrame(Scene& scene, ResourceManager& assets)
        : ctx{scene, assets, clock, events, window, input, ownedNet, chrome, render} {}
};

inline bool sameDirection(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(a - b) < 1e-3f;
}

// A spine with two legs: the smallest rig where "did the joints hold" means anything.
inline SkeletonAsset makeTestRig() {
    SkeletonAsset rig;
    const char* names[] = {"hips", "spine", "chest", "legL", "footL", "legR", "footR"};
    const int32_t parents[] = {-1, 0, 1, 0, 3, 0, 5};
    const glm::vec3 offsets[] = {
        {0.0f, 1.0f, 0.0f},    // hips, a metre up
        {0.0f, 0.3f, 0.0f},    // spine
        {0.0f, 0.3f, 0.0f},    // chest
        {-0.15f, -0.45f, 0.0f}, // left leg
        {0.0f, -0.45f, 0.0f},   // left foot
        {0.15f, -0.45f, 0.0f},  // right leg
        {0.0f, -0.45f, 0.0f}    // right foot
    };

    for (int i = 0; i < 7; ++i) {
        rig.bones.push_back({names[i], parents[i]});
        Transform bind;
        bind.position = offsets[i];
        rig.bindPose.push_back(bind);
        rig.inverseBind.push_back(glm::mat4(1.0f));
    }
    return rig;
}

/**
 * @brief Count the children of @p entity off the hierarchy's linked list.
 *
 * @param scene  Scene holding the hierarchy.
 * @param entity Parent whose children are counted.
 * @return How many children it has; 0 when it has no Hierarchy.
 */
inline int childrenOf(const Scene& scene, EntityId entity) {
    const Hierarchy* parent = scene.tryGet<Hierarchy>(entity);
    int count = 0;
    for (EntityId at = parent ? parent->firstChild : EntityId{}; at; ) {
        const Hierarchy* child = scene.tryGet<Hierarchy>(at);
        if (!child) break;
        ++count;
        at = child->nextSibling;
    }
    return count;
}
