#include "game_behaviors.h"

// Entry point the editor resolves after dlopen-ing this module. It registers
// the game's behaviors into the engine's BehaviorRegistry, which resolves to the
// host exe's single instance (Windows: via the exe import lib; Linux: the
// rdynamic host at load) - so registrations land where the engine reads them.
//
// The runtime exe doesn't use this entry; it static-links this module and calls
// registerGameBehaviors() directly.
extern "C"
#if defined(_WIN32)
__declspec(dllexport)
#endif
void vkmRegisterBehaviors() {
    Engine::registerGameBehaviors();
}
