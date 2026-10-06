#define VKM_LOG_CATEGORY "IO"

#include "io/asset/asset_serializer.h"

#include <array>
#include <exception>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "logger.h"

#include "ecs/component/animation/animator.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "debug/engine_error_log.h"
#include "ecs/scene.h"
#include "ecs/component/core/missing_assets.h"
#include "resource/resource_manager.h"
#include "io/asset/asset_factory.h"
#include "io/asset/asset_library.h"
#include "io/asset/cooked_loader.h"
#include "io/json_vec.h"
#include "io/scene/component_serializer.h"
#include "io/scene/scene_serializer.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/script_component.h"
#include "core/reflect.h"
#include "resource/asset_source_kind.h"

namespace Vkm::Engine::AssetSerializer {

namespace {

/**
 * @brief Texture fields on MaterialAsset, paired with their stable JSON key.
 */
struct TexField {
    const char* key;
    TextureHandle MaterialAsset::* member;
};
// Sized from the macro, not a number beside it: a removed map would leave a {nullptr, nullptr} row the
// walks below take as a real slot.
#define VKM_MATERIAL_MAP_FIELD(key, member, slot, doc) {#key, &MaterialAsset::member},
constexpr std::array<TexField, MATERIAL_MAP_COUNT> MATERIAL_TEXTURE_FIELDS = {{
    VKM_MATERIAL_MAPS(VKM_MATERIAL_MAP_FIELD)
}};
#undef VKM_MATERIAL_MAP_FIELD

using ::Vkm::Engine::detail::vec3ToJson;
using ::Vkm::Engine::detail::vec4ToJson;
using ::Vkm::Engine::detail::jsonToVec3;
using ::Vkm::Engine::detail::jsonToVec4;

} // namespace

nlohmann::json materialToInline(const MaterialAsset& m, const ResourceManager& resources) {
    nlohmann::json src;
    src["kind"] = AssetSourceKind::INLINE;

    // Reflection drives scalar / vector / enum fields, so a new MaterialAsset field cannot fall out of the
    // round trip. Texture refs resolve by name, below.
    Reflect::forEachField(m, [&](std::string_view name, const auto& val) {
        using V = std::decay_t<decltype(val)>;
        if      constexpr (std::is_same_v<V, MaterialType>) src[std::string(name)] = Reflect::enumName(val);
        else if constexpr (std::is_same_v<V, glm::vec3>)    src[std::string(name)] = vec3ToJson(val);
        else if constexpr (std::is_same_v<V, glm::vec4>)    src[std::string(name)] = vec4ToJson(val);
        else                                                src[std::string(name)] = val;
    });

    nlohmann::json textures = nlohmann::json::object();
    for (const auto& f : MATERIAL_TEXTURE_FIELDS) {
        const TextureHandle& h = m.*f.member;
        if (!h) continue;
        const auto& tex = resources.get(h);
        // Warned here, not on every load: a hidden texture is not in the cooked manifest, so its name in a
        // public material's recipe can never resolve.
        if (tex.isHidden()) {
            LOG_WARNING(
                "Material texture slot '%s' refers to hidden asset '%s' - dropping ref",
                f.key,
                tex.name().c_str()
            );
            continue;
        }
        if (tex.name().empty()) {
            LOG_WARNING("Material texture slot '%s' has no name - dropping ref", f.key);
            continue;
        }
        textures[f.key] = tex.name();
    }
    if (!textures.empty()) src["textures"] = std::move(textures);
    return src;
}

void applyInline(const nlohmann::json& src, MaterialAsset& m, const ResourceManager& resources) {
    // Mirror of materialToInline. A missing key, or an enum name this build lacks, keeps the current value
    // rather than becoming enumerator zero.
    Reflect::forEachField(m, [&](std::string_view name, auto& val) {
        using V = std::decay_t<decltype(val)>;
        const std::string key(name);
        if constexpr (std::is_same_v<V, MaterialType>) {
            const std::string typeName = src.value(key, std::string{});
            if (!typeName.empty() && !Reflect::enumFromNameChecked(typeName, val)) {
                LOG_WARNING(
                    "Material '%s': no material type called '%s' in this build; leaving it as it was",
                    m.name().c_str(),
                    typeName.c_str()
                );
            }
        } else if constexpr (std::is_same_v<V, glm::vec3>) {
            val = jsonToVec3(src.value(key, nlohmann::json{}), val);
        } else if constexpr (std::is_same_v<V, glm::vec4>) {
            val = jsonToVec4(src.value(key, nlohmann::json{}), val);
        } else {
            val = src.value(key, val);
        }
    });

    if (src.contains("textures") && src["textures"].is_object()) {
        for (const auto& f : MATERIAL_TEXTURE_FIELDS) {
            if (!src["textures"].contains(f.key)) continue;
            const std::string texName = src["textures"][f.key].get<std::string>();
            if (texName.empty()) continue;
            const TextureHandle h = resources.findByName<TextureAsset>(texName);
            if (!h) {
                // Keep the slot rather than zeroing it: the name may be a typo or not loaded yet, and a
                // null slot draws as transparent black.
                LOG_WARNING(
                    "Material texture ref '%s' ('%s') unresolved; keeping previous slot value",
                    f.key,
                    texName.c_str()
                );
                continue;
            }
            m.*f.member = h;
        }
    }
}

namespace {

/**
 * @brief Emit one name-only asset reference into @p target.
 *
 * Skipped here, so every emitter keeps the rule: an unnamed asset has no key, and a hidden one is absent
 * from the cooked manifest, so its name would name nothing on load.
 *
 * @param target The section array the reference is appended to.
 * @param asset  The asset to name.
 */
void emitDescriptor(nlohmann::json& target, const Resource& asset) {
    if (asset.isHidden()) return;
    if (asset.name().empty()) {
        LOG_WARNING("Asset has no name; skipping in save");
        return;
    }
    target.push_back({{"name", asset.name()}});
}

/**
 * @brief Emit a name-only reference that was authored as a name, not held as a handle.
 *
 * With no handle to dedup by, and a name a component may already have listed, the section itself is
 * checked - a scan over lists a scene keeps short.
 *
 * @param target The section array the reference is appended to.
 * @param name   The authored asset name; skipped when the section already has it.
 */
void emitNamedRef(nlohmann::json& target, const std::string& name) {
    for (const auto& entry : target) {
        if (entry.value("name", std::string{}) == name) return;
    }
    target.push_back({{"name", name}});
}

/**
 * @brief Collects (kind, name) for every asset a behavior's authored fields name.
 *
 * Only assetField collects. beginStruct returns true: an AssetRef nested in a reflected struct is as
 * much a reference as one at the top level.
 */
class BehaviorAssetRefs : public BehaviorFieldVisitor {
    public:
        /**
         * @brief Collect into @p refs, after whatever it already holds.
         *
         * @param refs The list each reference is appended to.
         */
        explicit BehaviorAssetRefs(std::vector<std::pair<AssetType, std::string>>& refs)
            : m_refs(refs) {}
        ~BehaviorAssetRefs() override = default;

        BehaviorAssetRefs(const BehaviorAssetRefs& other) = delete;
        BehaviorAssetRefs& operator=(const BehaviorAssetRefs& other) = delete;

        BehaviorAssetRefs(BehaviorAssetRefs && other) = delete;
        BehaviorAssetRefs& operator=(BehaviorAssetRefs && other) = delete;

    public:
        void field(const char* name, float& value)       override {}
        void field(const char* name, int& value)         override {}
        void field(const char* name, bool& value)        override {}
        void field(const char* name, glm::vec2& value)   override {}
        void field(const char* name, glm::vec3& value)   override {}
        void field(const char* name, glm::vec4& value)   override {}
        void field(const char* name, glm::quat& value)   override {}
        void field(const char* name, std::string& value) override {}

        void enumField(const char* name, int& index, const char* const* names, std::size_t count) override {}

        void assetField(const char* name, std::string& assetName, AssetType type) override {
            if (assetName.empty()) return;
            m_refs.emplace_back(type, assetName);
        }

        bool beginStruct(const char* name) override { return true; }
        void endStruct() override {}

    private:
        std::vector<std::pair<AssetType, std::string>>& m_refs;
};

} // namespace

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
    // A row with no emitAssetRefs overload stops the build here.
    VKM_SCENE_COMPONENTS(VKM_SCENE_SKIP_P, VKM_SCENE_EMIT_R, VKM_SCENE_SKIP_P)

    // Without an assets-block entry the next load never asks the library for an unresolved name, so it
    // stays broken even once the library has it back.
    for (const MissingAssetRef& ref : SceneSerializer::unresolvedRefs(scene, resources, id)) {
        if (ref.type == AssetType::Count) continue;   // not a kind the library files
        refs.names.emplace_back(ref.type, ref.name);
    }
    if (const ScriptComponent* script = scene.tryGet<ScriptComponent>(id)) {
        // A behavior names assets in authored fields, not handles, so only visitFields can see them.
        BehaviorAssetRefs behaviorRefs(refs.names);
        for (const auto& behavior : script->behaviors) {
            if (!behavior) continue;
            // visitFields is non-const because a visitor may write; this one only reads, and a const
            // unique_ptr still hands out a mutable referent.
            behavior->visitFields(behaviorRefs);
        }
    }
}

nlohmann::json saveAssetsForEntities(
    const Scene& scene,
    const std::vector<EntityId>& entities,
    const ResourceManager& resources
) {
    // One section per kind, indexed by AssetType, with the handles already in it.
    struct Section {
        nlohmann::json               entries = nlohmann::json::array();
        std::unordered_set<uint32_t> seen;
    };
    std::array<Section, static_cast<size_t>(AssetType::Count)> sections;

    // The asset the first time a handle is seen, and null every time after.
    const auto emit = [&](const auto& handle) {
        using Asset = typename std::decay_t<decltype(handle)>::resource_t;
        Section& section = sections[static_cast<size_t>(ASSET_TYPE<Asset>)];
        const Asset* already = nullptr;
        if (!handle || !section.seen.insert(handle.id()).second) return already;
        const Asset& asset = resources.get(handle);
        emitDescriptor(section.entries, asset);
        return &asset;
    };

    EntityAssetRefs collected;
    for (EntityId id : entities) collectAssetRefs(scene, id, resources, collected);
    const ComponentSerializer::AssetRefs& refs = collected.handles;

    for (const MeshHandle& h : refs.meshes)             emit(h);
    for (const MaterialHandle& h : refs.materials) {
        // Its textures too, so the loader recreates them first - unless hidden: a thumbnail material must
        // not drag its textures into the user's scene.
        const MaterialAsset* material = emit(h);
        if (!material || material->isHidden()) continue;
        for (const auto& f : MATERIAL_TEXTURE_FIELDS) emit(material->*f.member);
    }
    for (const SkeletonHandle& h : refs.skeletons)      emit(h);
    for (const AnimationClipHandle& h : refs.clips)     emit(h);
    for (const AudioClipHandle& h : refs.sounds)        emit(h);
    for (const TextureHandle& h : refs.textures)        emit(h);

    // Flat, by name: with no handle to walk into, a material named this way arrives without its textures.
    for (const auto& [type, name] : collected.names) {
        if (type == AssetType::Count) continue;
        emitNamedRef(sections[static_cast<size_t>(type)].entries, name);
    }

    // A section is keyed by its kind's library directory name, one list for both.
    nlohmann::json out;
#define VKM_ASSET_SECTION_OUT(tag, type, name, dir) \
    out[dir] = std::move(sections[static_cast<size_t>(AssetType::tag)].entries);
    VKM_ASSET_KINDS(VKM_ASSET_SECTION_OUT)
#undef VKM_ASSET_SECTION_OUT
    return out;
}

#undef VKM_SCENE_SKIP_P
#undef VKM_SCENE_EMIT_R

namespace {

/**
 * @brief Every live asset of one type, as the name-only descriptors a section holds.
 *
 * @tparam Asset Asset type to enumerate.
 * @param resources The graph to walk.
 * @return An array of descriptors, empty when the graph holds none.
 */
template<typename Asset>
nlohmann::json everyAssetOfType(const ResourceManager& resources) {
    nlohmann::json section = nlohmann::json::array();
    resources.forEachOfType<Asset>([&section](Handle<Asset>, const Asset& asset) {
        emitDescriptor(section, asset);
    });
    return section;
}

/**
 * @brief Remove every asset of one kind whose name the section does not carry.
 *
 * Handles are collected first: removal swap-and-pops the storage being walked. An absent section says
 * nothing about its kind, so nothing of that kind is dropped.
 *
 * @tparam Asset Resource type this section holds.
 * @param assetsJson The whole document.
 * @param key Section name within it.
 * @param resources Graph to prune.
 * @return How many were removed.
 */
template<typename Asset>
size_t dropSectionExtras(const nlohmann::json& assetsJson, const char* key, ResourceManager& resources) {
    const auto section = assetsJson.find(key);
    if (section == assetsJson.end() || !section->is_array()) return 0;

    std::unordered_set<std::string> keep;
    for (const nlohmann::json& entry : *section) {
        if (entry.is_object() && entry.contains("name") && entry["name"].is_string()) {
            keep.insert(entry["name"].get<std::string>());
        }
    }

    std::vector<Handle<Asset>> doomed;
    resources.template forEachOfType<Asset>([&](Handle<Asset> handle, const Asset& asset) {
        if (asset.isHidden() || keep.count(asset.name()) > 0) return;
        doomed.push_back(handle);
    });
    for (const Handle<Asset>& handle : doomed) resources.remove(handle);
    return doomed.size();
}

} // namespace

nlohmann::json saveAllAssets(const ResourceManager& resources) {
    nlohmann::json out;
    // No dependency order to keep: these are names, and loadAssets reads sections in the order it needs.
#define VKM_ASSET_SECTION_ALL(tag, type, name, dir) out[dir] = everyAssetOfType<type>(resources);
    VKM_ASSET_KINDS(VKM_ASSET_SECTION_ALL)
#undef VKM_ASSET_SECTION_ALL
    return out;
}

size_t dropAssetsNotIn(const nlohmann::json& assetsJson, ResourceManager& resources) {
    if (!assetsJson.is_object()) return 0;

    // The kinds saveAllAssets writes, from the same list, so what is pruned is what could be recorded.
    size_t dropped = 0;
#define VKM_ASSET_SECTION_DROP(tag, type, name, dir) \
    dropped += dropSectionExtras<type>(assetsJson, dir, resources);
    VKM_ASSET_KINDS(VKM_ASSET_SECTION_DROP)
#undef VKM_ASSET_SECTION_DROP

    if (dropped > 0) LOG_INFO("Dropped %zu asset(s) the session created", dropped);
    return dropped;
}

nlohmann::json saveAssetsForScene(const Scene& scene, const ResourceManager& resources) {
    std::vector<EntityId> entities;
    entities.reserve(scene.entityCount());
    scene.forEachEntity([&](EntityId id) { entities.push_back(id); });
    return saveAssetsForEntities(scene, entities, resources);
}

namespace {

// The kind's cooked load; a material has none.
template<typename Asset>
Handle<Asset> loadCooked(const std::string& name, ResourceManager& rm) {
    if constexpr (std::is_same_v<Asset, MeshAsset>)               return loadCookedMesh(name, rm);
    else if constexpr (std::is_same_v<Asset, TextureAsset>)       return loadCookedTexture(name, rm);
    else if constexpr (std::is_same_v<Asset, SkeletonAsset>)      return loadCookedSkeleton(name, rm);
    else if constexpr (std::is_same_v<Asset, AnimationClipAsset>) return loadCookedAnimationClip(name, rm);
    else if constexpr (std::is_same_v<Asset, AudioClipAsset>)     return loadCookedAudioClip(name, rm);
    else static_assert(Reflect::DEPENDENT_FALSE<Asset>, "loadCooked: this kind has no cooked load");
}

/**
 * @brief Build one asset from the library: its cooked file when this build can read one, else its recipe.
 *
 * The recipe is the source of truth: after a cooker version bump it is read and the next cook re-bakes. A
 * material has no cooked file; its recipe is its runtime form.
 *
 * @tparam Asset    Resource type to build.
 * @param name      The asset's library name.
 * @param resources The asset graph to build into.
 * @return The asset, or an invalid handle, having logged, when neither source serves it.
 */
template<typename Asset>
Handle<Asset> loadFromLibrary(const std::string& name, ResourceManager& resources) {
    constexpr AssetType TYPE = ASSET_TYPE<Asset>;
    nlohmann::json recipe;
    if constexpr (std::is_same_v<Asset, MaterialAsset>) {
        if (!AssetLibrary::readRecipe(TYPE, name, recipe)) return {};
        MaterialAsset material;
        applyInline(recipe, material, resources);
        return resources.add(std::move(material));
    } else {
        if (const Handle<Asset> cooked = loadCooked<Asset>(name, resources)) return cooked;

        const RecipeImport<Asset> import = recipeImport<Asset>();
        const char* what = Reflect::enumName(TYPE);
        if (!import) {
            LOG_ERROR(
                "%s '%s': no cooked file this build can read, and this host imports no recipes",
                what,
                name.c_str()
            );
            return {};
        }
        LOG_INFO("%s '%s': no cooked file this build can read; importing its recipe", what, name.c_str());
        if (!AssetLibrary::readRecipe(TYPE, name, recipe)) return {};
        return import(recipe, resources);
    }
}

/**
 * @brief Recreate one asset kind's section from its JSON array.
 *
 * Each reference is built by loadFromLibrary and renamed to the recorded name. Under
 * LoadMode::Reload a held material is rebuilt into its own slot (moved onto the live one, committed for
 * re-upload, the shell dropped), so every handle keeps naming it.
 *
 * @tparam Asset     Resource type the section holds.
 * @param assetsJson The whole assets block.
 * @param sectionKey The section's key, its kind's library directory name.
 * @param resources  The asset graph to build into.
 * @param mode       What to do about a name the graph already holds.
 * @return {created, already present}.
 */
template<typename Asset>
std::pair<size_t, size_t> loadAssetSection(
    const nlohmann::json& assetsJson,
    const char* sectionKey,
    ResourceManager& resources,
    LoadMode mode
) {
    constexpr AssetType TYPE = ASSET_TYPE<Asset>;
    const char* what = Reflect::enumName(TYPE);
    size_t created = 0, skipped = 0;
    auto it = assetsJson.find(sectionKey);
    if (it == assetsJson.end() || !it->is_array() || it->empty()) return {created, skipped};

    for (const auto& entry : *it) {
        const std::string name = entry.value("name", std::string{});
        if (name.empty()) {
            LOG_WARNING("%s entry missing 'name' - skipping", what);
            continue;
        }
        // Reload rebuilds a material alone; LoadMode::Reload says why.
        const Handle<Asset> live = resources.findByName<Asset>(name);
        const bool rebuild = mode == LoadMode::Reload && std::is_same_v<Asset, MaterialAsset>;
        if (live && !rebuild) {
            ++skipped;
            continue;
        }

        // A hand-edited recipe can hold a wrong-typed value that throws: that costs this asset, named, not
        // the scene; its references stay unresolved, as a missing one's do.
        const size_t held = resources.countOfType<Asset>();
        Handle<Asset> h;
        try {
            h = loadFromLibrary<Asset>(name, resources);
        } catch (const std::exception& e) {
            reportError(
                "Asset",
                std::string(what) + " '" + name + "'",
                "its recipe " + AssetLibrary::recipePath(TYPE, name).string()
                    + " does not read (" + e.what() + "); left unresolved"
            );
            continue;
        }
        if (!h) {
            LOG_WARNING("%s '%s' could not be recreated - skipping", what, name.c_str());
            continue;
        }
        // An importer dedupes by its own identity (a texture by its path), so it can answer with an asset
        // held under another name; renaming or moving that takes it from everything resolving it.
        if (h != live && resources.countOfType<Asset>() == held
            && resources.get(h).name() != name) {
            LOG_WARNING(
                "%s '%s': its recipe resolves to '%s', which is an asset of its own; "
                    "left unresolved (two library entries import the same source)",
                what,
                name.c_str(),
                resources.get(h).name().c_str()
            );
            continue;
        }
        if (!live) {
            resources.rename(h, name);
            ++created;
            continue;
        }
        // A load answering with the live asset rebuilt nothing; removing it would delete the live one.
        if (h == live) {
            ++skipped;
            continue;
        }

        // The rebuilt contents go into the live slot so its handle keeps naming it; the shell comes back
        // holding the old contents and is removed like any other asset.
        resources.swapValue(live, resources.edit(h));
        resources.remove(h);
        ++skipped;
    }
    return {created, skipped};
}

// One kind's section: its key and type come from its row of VKM_ASSET_KINDS.
std::pair<size_t, size_t> loadSection(
    AssetType type,
    const nlohmann::json& assetsJson,
    ResourceManager& resources,
    LoadMode mode
) {
    switch (type) {
#define VKM_LOAD_SECTION(tag, Asset, kindName, dir) \
        case AssetType::tag:                         \
            return loadAssetSection<Asset>(assetsJson, dir, resources, mode);
        VKM_ASSET_KINDS(VKM_LOAD_SECTION)
#undef VKM_LOAD_SECTION
        case AssetType::Count: break;
    }
    return {0, 0};
}

} // namespace

bool loadAssets(const nlohmann::json& assetsJson, ResourceManager& resources, LoadMode mode) {
    if (!assetsJson.is_object()) {
        LOG_WARNING("Assets block is not an object - skipping");
        return false;
    }

    size_t      created = 0;
    size_t      skipped = 0;
    std::string tally;
    for (const AssetType type : ASSET_DEPENDENCY_ORDER) {
        const auto [made, kept] = loadSection(type, assetsJson, resources, mode);
        created += made;
        skipped += kept;
        if (made > 0) {
            tally += (tally.empty() ? "" : ", ") + std::to_string(made) + " " + Reflect::enumName(type);
        }
    }

    // Silent when nothing is new: a prefab's assets block is loaded again every time the prefab is read.
    if (created > 0) {
        LOG_INFO("Assets created: %s; %zu already loaded", tally.c_str(), skipped);
    }
    return true;
}

} // namespace Vkm::Engine::AssetSerializer
