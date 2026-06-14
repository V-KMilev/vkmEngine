#pragma once

namespace Engine {

/**
 * @brief Register every concrete gameplay Behavior with the BehaviorRegistry.
 *
 * Call once at startup before scene I/O (load / play-mode snapshot restore)
 * can recreate scripts by name. Mirrors registerBuiltinAssetFactories().
 */
void registerGameBehaviors();

} // namespace Engine
