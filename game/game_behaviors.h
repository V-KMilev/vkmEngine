#pragma once

namespace Engine {

/**
 * @brief Register every concrete gameplay Behavior with the BehaviorRegistry.
 *
 * Called by the module entry (vkmRegisterBehaviors) when the editor loads the
 * game module, and directly by the runtime (which static-links this code).
 * Mirrors registerCookedAssetFactories().
 */
void registerGameBehaviors();

} // namespace Engine
