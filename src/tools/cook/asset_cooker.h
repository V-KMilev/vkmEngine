#pragma once

namespace Engine {

class ResourceManager;

/**
 * @brief Bakes assets from their in-memory (recipe-derived) form into the
 *        on-disk asset database. Editor-only (lives in EngineCooker).
 */
namespace AssetCooker {

/**
 * @brief Cook every non-hidden asset currently in @p resources into the library
 *        (per-asset recipe files) + cooked cache (binary blobs), then write the
 *        manifest.
 *
 * Meshes/textures get a binary `.vkmc`; materials get their canonical inline
 * descriptor as the library file (no binary cook needed). An asset whose cooked
 * output is already current (recipe hash unchanged, cooked file present) is
 * skipped. Existing manifest records for assets not currently loaded are
 * preserved, so the manifest accumulates the whole project's asset database.
 * Call before saving a scene (which then references these assets by name only).
 */
void cookAllAssets(ResourceManager& resources);

} // namespace AssetCooker

} // namespace Engine
