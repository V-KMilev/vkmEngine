#pragma once

#include <vector>

namespace Vkm::Engine {
    class NetSession;

    /**
     * @brief What registers each of a module's behaviors, one entry per VKM_BEHAVIOR block.
     */
    using ModuleBehaviors = std::vector<void (*)()>;
}

/**
 * @brief Marks a function as one of the entries a host resolves in a gameplay module.
 *
 * C linkage, so the name is not mangled; an explicit export on Windows, where MSVC
 * exports nothing from a DLL by default.
 */
#if defined(_WIN32)
    #define VKM_MODULE_ENTRY extern "C" __declspec(dllexport)
#else
    #define VKM_MODULE_ENTRY extern "C"
#endif

/**
 * @brief Marks a function as the module's own, seen by no other binary.
 *
 * A host that loads a rebuilt copy beside the running one keeps each copy's apart. A DLL exports
 * nothing unasked.
 */
#if defined(_WIN32)
    #define VKM_MODULE_LOCAL
#else
    #define VKM_MODULE_LOCAL __attribute__((visibility("hidden")))
#endif

/**
 * @brief The engine version this module was built against; required.
 *
 * The host refuses a mismatch rather than crash later on a layout difference.
 * VKM_GAME_MODULE, below, defines it.
 *
 * @return The version string the module was compiled with.
 */
VKM_MODULE_ENTRY const char* vkmModuleEngineVersion();

/**
 * @brief Register this project's behavior types in the host's BehaviorRegistry; required.
 *
 * VKM_GAME_MODULE defines it, registering every behavior a VKM_BEHAVIOR block lists.
 */
VKM_MODULE_ENTRY void vkmRegisterBehaviors();

/**
 * @brief Tell the session how players join and what they get; optional, for a networked game.
 *
 * @param session The session, before it hosts or connects.
 */
VKM_MODULE_ENTRY void vkmSetupNetwork(Vkm::Engine::NetSession& session);

/// This module's list, which VKM_GAME_MODULE defines: each loaded copy fills its own.
VKM_MODULE_LOCAL ::Vkm::Engine::ModuleBehaviors& vkmModuleBehaviors();

/**
 * @brief A gameplay module's entries: its engine version, and the registration of every behavior
 *        its VKM_BEHAVIOR blocks list. Written once a module, beside its other entries.
 */
#define VKM_GAME_MODULE()                                                               \
    VKM_MODULE_LOCAL ::Vkm::Engine::ModuleBehaviors& vkmModuleBehaviors() {            \
        static ::Vkm::Engine::ModuleBehaviors s_listed;                                \
        return s_listed;                                                               \
    }                                                                                  \
    VKM_MODULE_ENTRY const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; } \
    VKM_MODULE_ENTRY void vkmRegisterBehaviors() {                                     \
        for (void (*listed)() : vkmModuleBehaviors()) listed();                        \
    }
