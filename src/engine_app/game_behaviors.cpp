#include "game_behaviors.h"

#include "system/script/behavior_registry.h"

#include "behaviors/cube_spinner.h"

namespace Engine {

void registerGameBehaviors() {
    BehaviorRegistry::get().registerBehavior<CubeSpinner>();
}

} // namespace Engine
