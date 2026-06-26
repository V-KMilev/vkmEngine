#pragma once

namespace Engine {

class ResourceManager;

/**
 * @brief Bakes assets from their in-memory (recipe-derived) form into the on-disk asset database.
 *
 * Editor-only (lives in EngineCooker).
 */
namespace AssetCooker {

/**
 * @brief Cook every non-hidden loaded asset into the library and cooked cache, then write the manifest.
 *
 * Writes per-asset recipe files (the library) plus cooked binary blobs (the
 * cooked cache). Meshes/textures get a binary `.vkmc`; materials get their
 * canonical inline descriptor as the library file (no binary cook needed). An
 * asset whose cooked output is already current (recipe hash unchanged, cooked
 * file present) is skipped. Existing manifest records for assets not currently
 * loaded are preserved, so the manifest accumulates the whole project's asset
 * database. Call before saving a scene (which then references these assets by
 * name only).
 *
 * @param resources Resource manager whose loaded assets are cooked.
 */
void cookAllAssets(ResourceManager& resources);

} // namespace AssetCooker

} // namespace Engine
