#pragma once

namespace Engine {

/**
 * @brief Register every built-in mesh generator + material loader with the
 *        engine's AssetFactories registry.
 *
 * Call this once at startup, before any scene load. The Engine namespace
 * (engine/io/) holds the registry; this function (in tools/) is where the
 * concrete generators are wired up — keeping the engine layer free of
 * direct dependencies on tools/.
 */
void registerBuiltinAssetFactories();

} // namespace Engine
