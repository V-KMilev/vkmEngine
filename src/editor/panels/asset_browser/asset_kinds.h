#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "io/scene/asset_refs.h"
#include "resource/asset_type.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "system/render/editor_render_hooks.h"
#include "ui/style/editor_icons.h"

namespace Vkm::Engine {

struct EditorContext;
class ResourceManager;
class Scene;

/**
 * @brief One asset as the grid sees it: no C++ type, only what a tile needs.
 */
struct AssetRow {
    StorageIndex key;
    std::string  name;            ///< A copy: a library row loaded mid-grid can move every asset's own.
    uint64_t     version = 0;
    bool         held    = true;  ///< Loaded; else only in the library, and `key` is null.
};

using RowSink = std::function<void(const AssetRow&)>;

/**
 * @brief Which asset slots of one kind are referenced; indexed, not hashed, as it is rebuilt every frame.
 */
struct UsedSlots {
    std::vector<uint8_t>& marks;

    void mark(uint32_t slot) {
        if (slot >= marks.size()) marks.resize(slot + 1, 0);
        marks[slot] = 1;
    }
    bool has(uint32_t slot) const { return slot < marks.size() && marks[slot] != 0; }
};

/**
 * @brief What a tile face needs to render a preview, gathered once per frame.
 */
struct TileContext {
    EditorContext& ec;
    MeshHandle     sphere;   ///< Shape material thumbnails are drawn on
    MaterialHandle neutral;  ///< Material mesh thumbnails are drawn under
    int            uploads;  ///< Texture uploads still allowed this frame
};

/**
 * @brief One asset kind as the browser sees it; rows, detail, used, named, remove and find are never null.
 */
struct AssetKind {
    /**
     * @brief Which import or create action a kind's primary verb raises.
     */
    enum class Verb { NewMaterial, NewTerrain, ImportModel, ImportTexture, ImportSound };

    using EntityRefs = SceneAssets::EntityAssetRefs;
    using RowsFn     = void (*)(const ResourceManager&, const RowSink&);
    using DetailFn   = void (*)(const ResourceManager&, StorageIndex, bool, char*, size_t);
    using ThumbFn    = GpuTextureId (*)(TileContext&, StorageIndex, uint64_t);
    using UsedFn     = void (*)(const EntityRefs&, const Scene&, const ResourceManager&, UsedSlots&);
    using RenameFn   = void (*)(EditorContext&, StorageIndex, const std::string&, const char*);
    using RemoveFn   = void (*)(ResourceManager&, StorageIndex);
    using FindFn     = StorageIndex (*)(const ResourceManager&, const std::string&);

    AssetType   type;
    const char* label;
    const char* empty;        ///< Shown when the kind holds nothing (or nothing matches)
    EditorIcon  icon;
    const char* verb;         ///< Primary action button label, with no kind noun in it
    const char* verbHint;     ///< Hover text: what the verb does, and to what
    Verb        verbAction;
    const char* assignLabel;  ///< Context-menu item text; null for a kind given to nothing
    const char* assignHint;   ///< Why the item is grayed, in the user's terms
    const char* noRename;     ///< Why renaming is refused; null when it is offered
    RowsFn      rows;
    DetailFn    detail;       ///< A short line (a tile's ~14 characters: the one distinguishing
                              ///< fact) or, verbose, the tooltip's.
    ThumbFn     thumb;        ///< Null: the kind has no picture, so the tile draws its glyph
    UsedFn      used;
    UsedFn      named;        ///< used's first half: what an entity's own references name.
    RenameFn    rename;       ///< Null: renaming would break a reference, see noRename
    RemoveFn    remove;
    FindFn      find;         ///< The loaded asset of a name, or a null key.
};

/// Every kind, one per AssetType, in the order the rail lists them.
extern const AssetKind ASSET_KINDS[static_cast<size_t>(AssetType::Count)];

/**
 * @brief The kind of @p type.
 *
 * @param type Any value: a drag's payload is bytes, and a kind is never stood in for another.
 * @return Its kind, or null for a type no kind is.
 */
const AssetKind* kindOf(AssetType type);

} // namespace Vkm::Engine
