// The module's entries (system/script/module/module_entry.h): its engine version and its behaviors,
// each registered by its own VKM_BEHAVIOR block. A game played over a wire adds vkmSetupNetwork
// here; without it, `vkm serve` refuses to start (docs/reference/networking.md).
#include "system/script/behavior_api.h"

VKM_GAME_MODULE()
