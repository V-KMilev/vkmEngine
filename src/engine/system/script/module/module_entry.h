#pragma once

namespace Vkm::Engine {
    class NetSession;
    class ResourceManager;
    class Scene;
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
 * @brief The engine version this module was built against; required.
 *
 * The host refuses a mismatch rather than crash later on a layout difference. Define
 * it as `{ return VKM_ENGINE_VERSION; }`.
 *
 * @return The version string the module was compiled with.
 */
VKM_MODULE_ENTRY const char* vkmModuleEngineVersion();

/**
 * @brief Register this project's behavior types in the host's BehaviorRegistry; required.
 */
VKM_MODULE_ENTRY void vkmRegisterBehaviors();

/**
 * @brief Build the project's starting world in code; optional.
 *
 * Asked only when `project.json` has no `entryScene`; see bootProjectWorld.
 *
 * @param scene Scene to populate; empty when this is called.
 * @param resources Asset graph the built world's meshes and materials go into.
 */
VKM_MODULE_ENTRY void vkmBuildScene(Vkm::Engine::Scene& scene, Vkm::Engine::ResourceManager& resources);

/**
 * @brief Tell the session how players join and what they get; optional, for a networked game.
 *
 * @param session The session, before it hosts or connects.
 */
VKM_MODULE_ENTRY void vkmSetupNetwork(Vkm::Engine::NetSession& session);
