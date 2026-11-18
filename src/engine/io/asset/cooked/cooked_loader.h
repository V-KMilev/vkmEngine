#pragma once

#include <string>

#include "resource/asset_type.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Load the cooked @p Asset called @p name in the library.
 *
 * The runtime's load path: no importer or image decoder. A mesh or texture is decoded off the main
 * thread: a loading stub comes back at once, read on the ThreadPool, and AsyncLoaderSystem finalizes
 * it on the main thread. The rest are read at once: a rig or clip is too small to earn an
 * AsyncLoadQueue lane, and a sound needs no decode (the cooked file is the mixer's PCM), where a
 * worker hop would open a window in which a scene's sounds exist but are silent.
 *
 * @tparam Asset    A kind of VKM_COOKED_ASSET_KINDS, each defined in cooked_loader.cpp.
 * @param name      The asset's library name.
 * @param resources The graph the asset is added to.
 * @return The asset (the one resident under @p name, if any), or an invalid handle when the manifest has
 *         no row for @p name, no cooked file this build can read, or a file read at once does not read.
 */
template<typename Asset>
Handle<Asset> loadCooked(const std::string& name, ResourceManager& resources);

#define VKM_DECLARE_LOAD_COOKED(tag, Asset, kindName, dir, cooked) \
    template<> Handle<Asset> loadCooked<Asset>(const std::string& name, ResourceManager& resources);
VKM_COOKED_ASSET_KINDS(VKM_DECLARE_LOAD_COOKED)
#undef VKM_DECLARE_LOAD_COOKED

} // namespace Vkm::Engine
