#include "io/scene/asset_refs.h"

#include <cstddef>

#include "ecs/scene.h"
#include "io/scene/scene_components.h"
#include "io/scene/scene_serializer.h"
#include "ecs/component/scene/missing_assets.h"
#include "resource/resource_manager.h"
namespace Vkm::Engine::SceneAssets {

#define VKM_SCENE_SKIP_P(Type, Key)
#define VKM_SCENE_EMIT_R(Type, Key) \
    if (const Type* held = scene.tryGet<Type>(id)) ComponentSerializer::emitAssetRefs(*held, refs.handles);

void collectAssetRefs(
    const Scene& scene,
    EntityId id,
    const ResourceManager& resources,
    EntityAssetRefs& refs
) {
    // The components that name assets are the R rows of VKM_SCENE_COMPONENTS.
    VKM_SCENE_COMPONENTS(VKM_SCENE_SKIP_P, VKM_SCENE_EMIT_R, VKM_SCENE_SKIP_P)

    // Without an assets-block entry the next load never asks the library for an unresolved name, so it
    // stays broken even once the library has it back.
    for (const MissingAssetRef& ref : SceneSerializer::unresolvedRefs(scene, resources, id)) {
        if (ref.type == AssetType::Count) continue;   // not a kind the library files
        refs.names.emplace_back(ref.type, ref.name);
    }
    if (const ScriptComponent* script = scene.tryGet<ScriptComponent>(id)) {
        collectBehaviorAssetRefs(*script, resources, refs.names);
    }
}

#undef VKM_SCENE_SKIP_P
#undef VKM_SCENE_EMIT_R

void forEachMaterialOf(
    const EntityAssetRefs& refs,
    const ResourceManager& resources,
    const std::function<void(MaterialHandle)>& visit
) {
    for (const MaterialHandle& material : refs.handles.materials) visit(material);
    for (const auto& [type, name] : refs.names) {
        if (type != AssetType::Material) continue;
        if (const MaterialHandle named = resources.findByName<MaterialAsset>(name)) visit(named);
    }
    for (const TerrainHandle& terrain : refs.handles.terrains) {
        const TerrainAsset* held = resources.tryGet(terrain);
        if (held) held->forEachReference(visit, [](MeshHandle) {});
    }
}

} // namespace Vkm::Engine::SceneAssets
