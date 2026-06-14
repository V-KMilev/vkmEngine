#include "game_behaviors.h"

#include "system/script/behavior_registry.h"

#include "behaviors/cube_spinner.h"
#include "behaviors/player_controller.h"

namespace Engine {

void registerGameBehaviors() {
    BehaviorRegistry::get().registerBehavior<CubeSpinner>();
    BehaviorRegistry::get().registerBehavior<PlayerController>();
}

} // namespace Engine
