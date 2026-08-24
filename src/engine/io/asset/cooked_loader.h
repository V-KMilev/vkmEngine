#pragma once

#include <string>

#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Load cooked assets by their library name (the runtime load path).
 *
 * Every loader here resolves the cooked file through AssetLibrary and reads it
 * with no recipe factory, no Assimp and no stb. All are idempotent: a name
 * already resident returns its existing handle, and a name the manifest never
 * listed returns an invalid one.
 */

/**
 * @brief Request a cooked mesh / texture, decoded off the main thread.
 *
 * Returns immediately with a loading stub and reads the binary on the
 * ThreadPool; finalisation happens on the main thread via AsyncLoaderSystem,
 * exactly like the editor's stb/Assimp loaders.
 */
MeshHandle    requestCookedMeshAsync   (const std::string& name, ResourceManager& resources);
TextureHandle requestCookedTextureAsync(const std::string& name, ResourceManager& resources);

/**
 * @brief Load a cooked skeleton / animation clip / sound, synchronously.
 *
 * A rig is a few tens of kilobytes and a clip little more, which is well under
 * what earns a completion type, an AsyncLoadQueue lane and a drain in
 * AsyncLoaderSystem - the machinery meshes and textures pay for because their
 * decode is measured in milliseconds. The handle comes back fully loaded.
 *
 * A sound is here for a different reason: it is bigger, but there is nothing to
 * decode. The cooked file is already the PCM the mixer wants, so the read is a
 * copy and a worker hop would buy a copy's worth of latency at the price of a
 * completion lane and a window in which a scene's sounds exist but are silent.
 */
SkeletonHandle      loadCookedSkeleton     (const std::string& name, ResourceManager& resources);
AnimationClipHandle loadCookedAnimationClip(const std::string& name, ResourceManager& resources);
AudioClipHandle     loadCookedAudioClip    (const std::string& name, ResourceManager& resources);

} // namespace Vkm::Engine
