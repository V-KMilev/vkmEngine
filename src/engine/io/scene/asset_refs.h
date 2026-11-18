#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/reflect/reflect.h"
#include "ecs/entity.h"
#include "io/json/reflect_json.h"
#include "resource/asset_type.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

class ResourceManager;
class Scene;
struct ScriptComponent;

namespace ComponentSerializer {

    /**
     * @brief Every asset a set of components references, one list per kind.
     *
     * The scene's `assets` block is built from it; a reference the block does not
     * name resolves to nothing on the next load. One list per kind because the
     * ResourceManager resolves a handle by its type: each kind's is named for its
     * library directory (`meshes`, `materials` and the rest, from VKM_ASSET_KINDS).
     * Empty handles are not recorded.
     */
    struct AssetRefs {
#define VKM_ASSET_REF_LIST(tag, Asset, name, dir, cooked) std::vector<Handle<Asset>> dir;
        VKM_ASSET_KINDS(VKM_ASSET_REF_LIST)
#undef VKM_ASSET_REF_LIST

#define VKM_ASSET_REF_LIST_OF(tag, Asset, name, dir, cooked) \
        static std::vector<Handle<Asset>>& of(AssetRefs& refs, Handle<Asset>) { return refs.dir; }
        VKM_ASSET_KINDS(VKM_ASSET_REF_LIST_OF)
#undef VKM_ASSET_REF_LIST_OF
    };

    /**
     * @brief Record every asset handle among @p value's reflected fields, nested structs and
     *        lists included, into @p refs: what a save names.
     *
     * @param value A reflected value.
     * @param refs  Receives the non-empty handles, each in its kind's list.
     */
    template <typename V>
    void emitAssetRefs(const V& value, AssetRefs& refs) {
        if constexpr (ReflectJson::IsHandle<V>::value) {
            if (value) AssetRefs::of(refs, value).push_back(value);
        } else if constexpr (ReflectJson::IsVector<V>::value) {
            for (const auto& item : value) emitAssetRefs(item, refs);
        } else if constexpr (Reflect::IS_REFLECTED<V>) {
            const auto each = [&](std::string_view, const auto& field) { emitAssetRefs(field, refs); };
            Reflect::forEachField(value, each);
        }
    }

} // namespace ComponentSerializer

namespace SceneAssets {

    /**
     * @brief Every asset reference a set of entities holds, as an assets block would name it.
     *
     * Handles from components, plus names with no handle: an AssetRef a behavior authors, an asset a
     * behavior held as text names, and a name the last load could not resolve, kept so a save still
     * names it. An asset's own references (a material's textures, a mesh's or a clip's rig) are not here.
     */
    struct EntityAssetRefs {
        ComponentSerializer::AssetRefs                 handles;
        std::vector<std::pair<AssetType, std::string>> names;
    };

    /**
     * @brief Append every asset reference @p id holds to @p refs.
     *
     * The one walk over an entity's references: a field that names an asset is seen here or nowhere.
     *
     * @param scene Scene holding the entity.
     * @param id Entity to walk; alive.
     * @param resources The asset graph the entity's handles index into.
     * @param refs Where the references are appended; duplicates are kept.
     */
    void collectAssetRefs(
        const Scene& scene,
        EntityId id,
        const ResourceManager& resources,
        EntityAssetRefs& refs
    );

    /**
     * @brief Append the (kind, name) of every asset @p script's behaviors name in their authored fields.
     *
     * A behavior names assets in fields, not handles, so only its visitFields sees them. Defined by
     * the script system (system/script/scene_script.cpp), the one that can read a behavior. One no
     * module registered is text, which says no field's kind: each string in it naming an asset
     * @p resources holds counts, as every kind that holds one by that name.
     *
     * @param script    The entity's behaviors.
     * @param resources The assets a behavior held as text may name.
     * @param names     Where each name is appended.
     */
    void collectBehaviorAssetRefs(
        const ScriptComponent& script,
        const ResourceManager& resources,
        std::vector<std::pair<AssetType, std::string>>& names
    );

    /**
     * @brief Each material the entities @p refs were walked from draw with: those they name, by
     *        handle or by name, and those the terrains they name hold.
     *
     * @param refs      collectAssetRefs's answer.
     * @param resources Resolves a name, and a terrain to its materials.
     * @param visit     Called with each, a repeat included.
     */
    void forEachMaterialOf(
        const EntityAssetRefs& refs,
        const ResourceManager& resources,
        const std::function<void(MaterialHandle)>& visit
    );

} // namespace SceneAssets

} // namespace Vkm::Engine
