#pragma once

#include <nlohmann/json_fwd.hpp>

#include "resource/asset_type.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief The io<->tools dispatch seam.
 *
 * tools wires these at startup; the runtime wires the cooked dispatch, the
 * editor wires the cooked+recipe dispatch. Each scene-load asset is recreated
 * by calling the matching function pointer, which switches internally on the
 * source `kind`.
 */
#define VKM_ASSET_FACTORY_FIELD(tag, type, name, dir) \
    Handle<type> (*create##tag)(const nlohmann::json&, ResourceManager&) = nullptr;
struct AssetFactory {
    VKM_ASSET_KINDS(VKM_ASSET_FACTORY_FIELD)
};
#undef VKM_ASSET_FACTORY_FIELD

AssetFactory& assetFactory();

} // namespace Vkm::Engine
