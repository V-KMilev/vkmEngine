#include "system/script/script_component.h"

namespace Vkm::Engine {

void reportUnregisteredBehavior(const char* typeName) {
    reportError(
        "Behavior",
        typeName,
        "added in code but never registered: it runs, but a saved scene cannot load it "
        "and a script reload drops it. Register it in vkmRegisterBehaviors."
    );
}

} // namespace Vkm::Engine
