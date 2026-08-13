#include "game_behaviors.h"

#include "system/script/behavior_registry.h"

#include "potion_runner.h"
#include "stress_arena.h"

namespace Engine {

void registerGameBehaviors() {
    BehaviorRegistry::get().registerBehavior<PotionRunner>();
    BehaviorRegistry::get().registerBehavior<StressArena>();
}

} // namespace Engine
