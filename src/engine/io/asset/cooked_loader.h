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
 * @brief Load a cooked mesh or texture by its library name, decoded off the main thread.
 *
 * The runtime's load path: no importer or image decoder. Returns a loading stub at once and reads on the
 * ThreadPool; AsyncLoaderSystem finalises on the main thread.
 *
 * @param name      The asset's library name.
 * @param resources The graph the asset is added to.
 * @return The asset (the one resident under @p name, if any), or an invalid handle when the manifest has
 *         no row for @p name or no cooked file this build can read.
 */
MeshHandle    loadCookedMesh   (const std::string& name, ResourceManager& resources);
TextureHandle loadCookedTexture(const std::string& name, ResourceManager& resources);

/**
 * @brief Load a cooked skeleton, animation clip or sound by its library name, synchronously.
 *
 * A rig or clip is too small to earn an AsyncLoadQueue lane. A sound needs no decode (the cooked file is
 * the mixer's PCM), and a worker hop would open a window in which a scene's sounds exist but are silent.
 *
 * @param name      The asset's library name.
 * @param resources The graph the asset is added to.
 * @return The asset (the one resident under @p name, if any), or an invalid handle when the manifest has
 *         no row for @p name, no cooked file this build can read, or the file does not read.
 */
SkeletonHandle      loadCookedSkeleton     (const std::string& name, ResourceManager& resources);
AnimationClipHandle loadCookedAnimationClip(const std::string& name, ResourceManager& resources);
AudioClipHandle     loadCookedAudioClip    (const std::string& name, ResourceManager& resources);

} // namespace Vkm::Engine
