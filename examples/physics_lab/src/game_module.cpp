#include "system/script/behavior_registry.h"

#include "lab_walker.h"

// Entries a host resolves after loading this module.
//
// Reported back to the host at load. It refuses a module built against a
// different engine rather than letting a layout mismatch surface later as a
// crash somewhere unrelated. VKM_ENGINE_VERSION comes from the engine this
// module linked, so rebuilding against a new SDK is all it ever needs.
extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

// The only required entry: it populates the host's BehaviorRegistry so the
// scene can name this project's behaviors.
//
// There is no vkmBuildScene here, unlike the other examples. This project's
// world is authored and saved rather than generated at play time, so
// project.json's entryScene names it and the host loads it - which is the path
// a project made in the editor takes, and the one nothing else here exercised.
extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
void vkmRegisterBehaviors() {
    using Vkm::Engine::BehaviorRegistry;
    using Vkm::Engine::LabWalker;
    BehaviorRegistry::get().registerBehavior<LabWalker>();
}
