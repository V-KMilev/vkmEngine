#include "resource/resource_manager.h"

#include <atomic>

#include "resource/asset_type.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/font_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

namespace {

std::atomic<uint64_t> g_nextUid{0};

} // namespace

uint64_t ResourceManager::mintUid() {
    return ++g_nextUid;
}

ResourceManager::ResourceManager() {
    makeEngineSlots();
}

void ResourceManager::clear() {
    ResourceManager empty;
    swap(empty);
}

void ResourceManager::swap(ResourceManager& other) noexcept {
    m_slots.swap(other.m_slots);
    // Each manager keeps the fonts it baked; both have the slot from construction, so no allocation.
    m_slots.slot<FontAsset>().swap(other.m_slots.slot<FontAsset>());
    ++m_epoch;
    ++other.m_epoch;
    LOG_INFO_C("RESOURCE", "Swap committed");
}

void ResourceManager::makeEngineSlots() {
#define VKM_ASSET_SLOT(tag, type, name, dir) getSlot<type>();
    VKM_ASSET_KINDS(VKM_ASSET_SLOT)
#undef VKM_ASSET_SLOT
    // Not a library kind - fonts are baked, not cooked - but an engine type.
    getSlot<FontAsset>();
}

} // namespace Vkm::Engine
