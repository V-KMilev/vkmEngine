#include "game_behaviors.h"

#include "system/script/behavior_registry.h"

#include "cube_spinner.h"
#include "player_controller.h"

namespace Engine {

void registerGameBehaviors() {
    BehaviorRegistry::get().registerBehavior<CubeSpinner>();
    BehaviorRegistry::get().registerBehavior<PlayerController>();
}

} // namespace Engine
