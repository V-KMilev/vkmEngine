#include "system/script/behavior_registry.h"

#include "potion_runner.h"

// The entry a host resolves after loading this module. Both the editor and the
// runtime call it; the registry it writes into is the host's single instance
// (Windows: through the exe's import library, Linux: the rdynamic host at load),
// so registrations land where the engine reads them.
extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
void vkmRegisterBehaviors() {
    Engine::BehaviorRegistry::get().registerBehavior<Engine::PotionRunner>();
}
