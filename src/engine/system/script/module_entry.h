#pragma once

namespace Vkm::Engine {
    class NetSession;
    class Scene;
}

/**
 * @brief Marks a function as one of the entries a host resolves in a gameplay module.
 *
 * A gameplay module is a shared library, and an entry has to leave it under its
 * own name: C linkage so the name is not mangled, and on Windows an explicit
 * export because MSVC exports nothing from a DLL by default. Both halves were
 * written out at every entry - four per module - and a project that wrote only
 * the first built a library whose symbols the host could not find, on one
 * platform, at run time.
 */
#if defined(_WIN32)
    #define VKM_MODULE_ENTRY extern "C" __declspec(dllexport)
#else
    #define VKM_MODULE_ENTRY extern "C"
#endif

/**
 * @brief The engine version this module was built against. Required.
 *
 * Reported to the host at load, which refuses a module built against a
 * different engine rather than letting a layout mismatch surface later as a
 * crash somewhere unrelated. Define it as `{ return VKM_ENGINE_VERSION; }`;
 * that macro comes from the engine this module linked, so rebuilding against a
 * new SDK is all it ever needs.
 */
VKM_MODULE_ENTRY const char* vkmModuleEngineVersion();

/**
 * @brief Register this project's behavior types. Required.
 *
 * Populates the host's BehaviorRegistry, which is how a scene file naming a
 * behavior finds the type to build.
 */
VKM_MODULE_ENTRY void vkmRegisterBehaviors();

/**
 * @brief Build the project's starting world in code. Optional.
 *
 * For a project whose world is generated rather than authored, so
 * `project.json`'s `entryScene` has nothing to point at. A host that does not
 * find this symbol simply loads the entry scene.
 *
 * @param scene The scene to populate; empty when this is called.
 */
VKM_MODULE_ENTRY void vkmBuildScene(Vkm::Engine::Scene& scene);

/**
 * @brief Tell the session how players join and what they get. Optional.
 *
 * A game played over a wire needs it; one that is never served does not, and a
 * host that does not find this symbol runs offline.
 *
 * @param session The session, before it hosts or connects.
 */
VKM_MODULE_ENTRY void vkmSetupNetwork(Vkm::Engine::NetSession& session);
