#include "panels/asset_browser_panel.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>

#include "ecs/component/animation/animator.h"
#include "ecs/component/audio/audio_source.h"
#include "system/render/editor_render_hooks.h"
#include "core/system.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/terrain.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"
#include "input/editor_keybinds.h"
#include "input/view_placement.h"
#include "overlays/viewport_projection.h"
#include "system/visibility/visibility.h"
#include "ui/asset_drag.h"
#include "ui/editor_icons.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"
#include "editor_actions.h"
#include "command/component_edit.h"
#include "command/editor_commands.h"
#include "panels/material_editor_panel.h"
#include "asset_editors/asset_editors.h"
#include "session/file_import.h"
#include "session/material_preview_session.h"
#include "system/audio/audio_system.h"
#include "system/loading/loading_system.h"
#include "system/render/render_system.h"
#include "import/import_formats.h"
#include "io/asset/asset_library.h"
#include "io/asset/asset_serializer.h"
#include "io/project_paths.h"
#include "ui/audition_transport.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief One asset as the grid sees it: no C++ type, only what a tile needs.
 *
 * The handle keeps its generation, so a recycled slot is not mistaken for the old asset.
 */
struct AssetRow {
    StorageIndex key;
    std::string  name;  ///< A copy: a library row loaded mid-grid can move every asset's own.
    uint64_t     version = 0;
    bool         held    = true;  ///< Loaded; else only in the library, and `key` is null.
};

using RowSink = std::function<void(const AssetRow&)>;

using EntityRefs = AssetSerializer::EntityAssetRefs;

/**
 * @brief Which asset slots of one kind something holds a reference to.
 *
 * Indexed by slot, not hashed: rebuilt every frame, and slots are small and dense.
 */
struct UsedSlots {
    std::vector<uint8_t>& marks;

    void mark(uint32_t slot) {
        if (slot >= marks.size()) marks.resize(slot + 1, 0);
        marks[slot] = 1;
    }
    bool has(uint32_t slot) const { return slot < marks.size() && marks[slot] != 0; }
};

// Making an undrawn texture's GPU mirror is a full upload; capped so 4K maps fill in over
// several frames.
constexpr int TEXTURE_UPLOADS_PER_FRAME = 3;

/// What a tile face needs to render a preview, gathered once per frame.
struct TileContext {
    EditorContext& ec;
    MeshHandle     sphere;   ///< Shape material thumbnails are drawn on
    MaterialHandle neutral;  ///< Material mesh thumbnails are drawn under
    int            uploads;  ///< Texture uploads still allowed this frame
};

// Which import or create action a kind's primary verb raises.
enum class Verb { NewMaterial, NewTerrain, ImportModel, ImportTexture, ImportSound };

using RowsFn   = void (*)(const ResourceManager&, const RowSink&);
using DetailFn = void (*)(const ResourceManager&, StorageIndex, bool, char*, size_t);
using ThumbFn  = GpuTextureId (*)(TileContext&, StorageIndex, uint64_t);
using TargetFn = bool (*)(const Scene&, EntityId);
using AssignFn = void (*)(Scene&, ResourceManager&, EditorState&, EntityId, StorageIndex);
using UsedFn   = void (*)(const EntityRefs&, const Scene&, const ResourceManager&, UsedSlots&);
using RenameFn = void (*)(EditorContext&, StorageIndex, const std::string&, const char*);
using RemoveFn = void (*)(ResourceManager&, StorageIndex);
using FindFn   = StorageIndex (*)(const ResourceManager&, const std::string&);

/**
 * @brief Everything the browser needs to know about one asset kind.
 *
 * rows, detail, used, named, remove and find are never null.
 */
struct AssetKind {
    AssetType     type;
    const char*   label;
    const char*   empty;        ///< Shown when the kind holds nothing (or nothing matches)
    EditorIcon    icon;
    const ImVec4* accent;
    const char*   verb;         ///< Primary action button label, with no kind noun in it
    const char*   verbHint;     ///< Hover text: what the verb does, and to what
    Verb          verbAction;
    const char*   assignLabel;  ///< Context-menu item text; null when the kind has no target
    const char*   assignHint;   ///< Why the item is greyed, in the user's terms
    const char*   noRename;     ///< Why renaming is refused; null when it is offered
    RowsFn        rows;
    DetailFn      detail;
    ThumbFn       thumb;        ///< Null: the kind has no picture, so the tile draws its glyph
    TargetFn      target;
    AssignFn      assign;
    UsedFn        used;
    UsedFn        named;        ///< used's first half: what an entity's own references name.
    RenameFn      rename;       ///< Null: renaming would break a reference, see noRename
    RemoveFn      remove;
    FindFn        find;         ///< The loaded asset of a name, or a null key.
};

template<typename Asset>
struct KindOps {
    static void rows(const ResourceManager& resources, const RowSink& sink) {
        resources.forEachOfType<Asset>([&](Handle<Asset> h, const Asset& a) {
            if (a.isHidden()) return;  // editor helpers / preview primitives are not user-facing
            sink(AssetRow{h.key, a.name(), a.version()});
        });
    }

    static void rename(EditorContext& ec, StorageIndex key, const std::string& from, const char* to) {
        EditorActions::renameAsset(
            ec.frame.resources,
            ec.state,
            Handle<Asset>{key},
            from,
            to,
            "Rename Asset"
        );
    }

    static void remove(ResourceManager& resources, StorageIndex key) {
        resources.remove(Handle<Asset>{key});
    }

    static StorageIndex find(const ResourceManager& resources, const std::string& name) {
        return resources.findByName<Asset>(name).key;
    }
};

// Detail lines: a short form (a tile's ~14 characters: the one distinguishing fact) and a
// verbose one for the tooltip.

void materialDetail(const ResourceManager& resources, StorageIndex key, bool verbose, char* out, size_t n) {
    const MaterialAsset& m = resources.get(MaterialHandle{key});
    const char* type = "opaque";
    switch (m.type) {
        case MaterialType::Transparent: type = "transparent"; break;
        case MaterialType::Unlit:       type = "unlit";       break;
        case MaterialType::AlphaMask:   type = "alpha mask";  break;
        default: break;
    }
    if (verbose) {
        snprintf(
            out,
            n,
            "%s . metal %.2f . rough %.2f",
            type,
            static_cast<double>(m.metallic),
            static_cast<double>(m.roughness)
        );
    } else if (m.type != MaterialType::Opaque) {
        // A thumbnail cannot show transparency.
        snprintf(out, n, "%s", type);
    } else {
        snprintf(out, n, "rough %.2f", static_cast<double>(m.roughness));
    }
}

void meshDetail(const ResourceManager& resources, StorageIndex key, bool verbose, char* out, size_t n) {
    const MeshAsset& mesh = resources.get(MeshHandle{key});
    const size_t tris = mesh.indices.size() / 3;
    // Explains an odd thumbnail: the preview draws bind-pose vertices with no rig.
    const char* skin = mesh.skin.empty() ? "" : " . skinned";
    if (verbose) {
        snprintf(out, n, "%zu tris . %zu verts%s", tris, mesh.vertices.size(), skin);
    } else {
        snprintf(out, n, "%zu tris%s", tris, skin);
    }
}

void terrainDetail(const ResourceManager& resources, StorageIndex key, bool verbose, char* out, size_t n) {
    const TerrainAsset& terrain = resources.get(TerrainHandle{key});
    if (verbose) {
        snprintf(
            out,
            n,
            "%u x %u vertices . %.0f m . %zu plant layer(s)",
            terrain.resolution,
            terrain.resolution,
            static_cast<double>(terrain.size),
            terrain.foliage.size()
        );
    } else {
        snprintf(out, n, "%.0f m", static_cast<double>(terrain.size));
    }
}

void soundDetail(const ResourceManager& resources, StorageIndex key, bool verbose, char* out, size_t n) {
    const AudioClipAsset& clip = resources.get(AudioClipHandle{key});
    char layout[16];
    if      (clip.channels == 1) snprintf(layout, sizeof(layout), "mono");
    else if (clip.channels == 2) snprintf(layout, sizeof(layout), "stereo");
    else                         snprintf(layout, sizeof(layout), "%u ch", clip.channels);

    if (verbose) {
        // A positioned source loses half a wide clip's field; said where a clip is picked.
        const char* mono = clip.channels > 1
            ? "\nEach channel sticks to one ear - positioning wants mono"
            : "";
        snprintf(
            out,
            n,
            "%.2fs . %s %u Hz . %.1f MB%s",
            static_cast<double>(clip.duration()),
            layout,
            clip.sampleRate,
            static_cast<double>(clip.sampleCount() * sizeof(int16_t)) / (1024.0 * 1024.0),
            mono
        );
    } else {
        snprintf(out, n, "%.2fs . %s", static_cast<double>(clip.duration()), layout);
    }
}

void textureDetail(const ResourceManager& resources, StorageIndex key, bool verbose, char* out, size_t n) {
    const TextureAsset& tex = resources.get(TextureHandle{key});
    if (tex.loading) {
        // An async import has no dimensions for a few frames; "0x0" would read as broken.
        snprintf(out, n, "decoding...");
        return;
    }
    if (!verbose) {
        snprintf(out, n, "%ux%u", tex.params.width, tex.params.height);
        return;
    }
    // From the layout, not the bytes: cooked textures hold mips, compressed ones blocks.
    snprintf(
        out,
        n,
        "%ux%u . %u ch . %s%s . %u level%s . %.1f MB",
        tex.params.width,
        tex.params.height,
        channelCount(tex.params.format),
        Reflect::enumName(tex.usage()),
        isCompressedFormat(tex.params.internalFormat) ? " . block-compressed" : "",
        tex.params.mipLevels,
        tex.params.mipLevels == 1 ? "" : "s",
        static_cast<double>(tex.pixelData.size()) / (1024.0 * 1024.0)
    );
}

void skeletonDetail(const ResourceManager& resources, StorageIndex key, bool verbose, char* out, size_t n) {
    const SkeletonAsset& rig = resources.get(SkeletonHandle{key});
    if (rig.bones.empty()) {
        snprintf(out, n, "no bones");
        return;
    }
    if (verbose) {
        snprintf(out, n, "%zu bones . root '%s'", rig.bones.size(), rig.bones.front().name.c_str());
    } else {
        snprintf(out, n, "%zu bones", rig.bones.size());
    }
}

void clipDetail(const ResourceManager& resources, StorageIndex key, bool verbose, char* out, size_t n) {
    const AnimationClipAsset& clip = resources.get(AnimationClipHandle{key});

    // By the rig resolved, not the name: SkeletalAnimationSystem::resolveClip refuses a clip
    // whose rig is absent, which otherwise looks like one that works.
    const SkeletonHandle rig = resources.findByName<SkeletonAsset>(clip.skeleton);

    size_t channels = 0;
    for (const ClipBone& bone : clip.bones) {
        channels += (bone.position.count != 0) + (bone.rotation.count != 0) + (bone.scale.count != 0);
    }

    const double seconds = static_cast<double>(clip.duration);
    if (verbose) {
        char markers[32] = {};
        if (!clip.markers.empty()) {
            snprintf(markers, sizeof(markers), " . %zu markers", clip.markers.size());
        }
        if (rig) {
            snprintf(
                out,
                n,
                "%.2fs . %zu channels . rig '%s'%s",
                seconds,
                channels,
                clip.skeleton.c_str(),
                markers
            );
        } else {
            snprintf(
                out,
                n,
                "%.2fs . %zu channels . rig '%s' is not in this project%s",
                seconds,
                channels,
                clip.skeleton.c_str(),
                markers
            );
        }
    } else if (!rig) {
        snprintf(out, n, "%.2fs . no rig", seconds);
    } else {
        snprintf(out, n, "%.2fs . %zu ch", seconds, channels);
    }
}

GpuTextureId materialThumb(TileContext& tc, StorageIndex key, uint64_t version) {
    PreviewRequest req;
    req.key      = AssetEditors::thumbnailKey(AssetType::Material, MaterialHandle{key}.id());
    req.draws    = {{tc.sphere, MaterialHandle{key}}};
    req.yawDeg   = 30.0f;
    req.pitchDeg = 18.0f;
    req.distance = 2.6f;
    const bool live = false;
    return tc.ec.materialPreviews.texture(tc.ec.frame.resources, req, version, live);
}

GpuTextureId meshThumb(TileContext& tc, StorageIndex key, uint64_t version) {
    PreviewRequest req;
    req.key      = AssetEditors::thumbnailKey(AssetType::Mesh, MeshHandle{key}.id());
    req.draws    = {{MeshHandle{key}, tc.neutral}};
    req.yawDeg   = 25.0f;
    req.pitchDeg = 15.0f;
    req.distance = 2.6f;
    const bool live = false;
    return tc.ec.materialPreviews.texture(tc.ec.frame.resources, req, version, live);
}

GpuTextureId textureThumb(TileContext& tc, StorageIndex key, uint64_t) {
    // The tile samples the renderer's mirror; nothing is copied. A re-upload keeps its id,
    // so the version is unused.
    EditorRenderHooks& hooks = tc.ec.hooks;
    const TextureHandle handle{key};
    if (const GpuTextureId resident = hooks.textureId(handle)) return resident;

    // Not resident, and never will be on its own; see EditorRenderHooks::textureId.
    if (tc.uploads <= 0) return 0;
    --tc.uploads;
    return hooks.ensureTexture(handle, tc.ec.frame.resources);
}

// Assignment, through an EditScope: an undo step, and a prefab override on an instance.

bool meshTarget(const Scene& scene, EntityId id) { return scene.has<Mesh>(id); }

void assignMaterial(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id,
    StorageIndex key
) {
    EditScope<Mesh> mesh(scene, resources, state, id, "Assign Material");
    mesh->material = MaterialHandle{key};
}

void assignMesh(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id, StorageIndex key) {
    EditScope<Mesh> mesh(scene, resources, state, id, "Assign Mesh");
    mesh->mesh = MeshHandle{key};
}

// One Animator carries both the rig and its clip.
bool animatorTarget(const Scene& scene, EntityId id) { return scene.has<Animator>(id); }

void assignSkeleton(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id,
    StorageIndex key
) {
    EditScope<Animator> animator(scene, resources, state, id, "Assign Skeleton");
    animator->skeleton = SkeletonHandle{key};
}

void assignClip(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id, StorageIndex key) {
    EditScope<Animator> animator(scene, resources, state, id, "Assign Clip");
    animator->clip = AnimationClipHandle{key};
    // Cut, not crossFadeTo: the editor does not advance the clock a blend runs on.
    animator->time = 0.0f;
}

bool audioTarget(const Scene& scene, EntityId id) { return scene.has<AudioSource>(id); }

bool terrainTarget(const Scene& scene, EntityId id) { return scene.has<Terrain>(id); }

void assignTerrain(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id,
    StorageIndex key
) {
    EditScope<Terrain> terrain(scene, resources, state, id, "Assign Terrain");
    terrain->terrain = TerrainHandle{key};
}

void assignSound(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id,
    StorageIndex key
) {
    EditScope<AudioSource> source(scene, resources, state, id, "Assign Sound");
    source->clip = AudioClipHandle{key};
}

// Usage walks. Entity references come from AssetSerializer::collectAssetRefs, the walk a
// scene save uses; textures and rigs also walk the resources (see docs/reference/editor.md).

// Expanded from the declaring list, so a new map cannot hide its texture from deletion checks.
constexpr TextureHandle MaterialAsset::* MATERIAL_TEXTURE_SLOTS[] = {
#define VKM_BROWSER_TEXTURE_SLOT(key, member, slot, doc) &MaterialAsset::member,
    VKM_MATERIAL_MAPS(VKM_BROWSER_TEXTURE_SLOT)
#undef VKM_BROWSER_TEXTURE_SLOT
};

// Names entities hold without a handle (a behavior field, an unresolved load), resolved back.
template<typename Asset>
void markNamed(const EntityRefs& refs, const ResourceManager& resources, UsedSlots& used) {
    for (const auto& [type, name] : refs.names) {
        if (type != ASSET_TYPE<Asset>) continue;
        if (const Handle<Asset> h = resources.findByName<Asset>(name)) used.mark(h.id());
    }
}

// Every reference entities hold to one kind: its handles, then its names.
template<typename Asset>
void markReferenced(
    const std::vector<Handle<Asset>>& handles,
    const EntityRefs& refs,
    const ResourceManager& resources,
    UsedSlots& used
) {
    for (const Handle<Asset>& h : handles) used.mark(h.id());
    markNamed<Asset>(refs, resources, used);
}

// What one entity's references name of each kind, the first half of each walk below.

void materialsNamed(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    const auto mark = [&](MaterialHandle material) { used.mark(material.id()); };
    AssetSerializer::forEachMaterialOf(refs, resources, mark);
}

void meshesNamed(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.meshes, refs, resources, used);
}

void terrainsNamed(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.terrains, refs, resources, used);
}

void texturesNamed(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.textures, refs, resources, used);
}

void skeletonsNamed(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.skeletons, refs, resources, used);
}

void clipsNamed(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.clips, refs, resources, used);
}

void soundsNamed(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.sounds, refs, resources, used);
}

void materialsInUse(
    const EntityRefs& refs,
    const Scene& scene,
    const ResourceManager& resources,
    UsedSlots& used
) {
    materialsNamed(refs, scene, resources, used);
    const auto mark = [&](MaterialHandle material) { used.mark(material.id()); };
    // A terrain no entity places still names its materials in its file.
    resources.forEachOfType<TerrainAsset>([&](TerrainHandle, const TerrainAsset& terrain) {
        AssetSerializer::forEachMaterialOf(terrain, mark);
    });
}

void meshesInUse(
    const EntityRefs& refs,
    const Scene& scene,
    const ResourceManager& resources,
    UsedSlots& used
) {
    meshesNamed(refs, scene, resources, used);
    resources.forEachOfType<TerrainAsset>([&](TerrainHandle, const TerrainAsset& terrain) {
        for (const TerrainFoliage& plant : terrain.foliage) {
            for (const TerrainPlantPart& part : plant.parts) {
                if (part.mesh) used.mark(part.mesh.id());
            }
        }
    });
}

void terrainsInUse(
    const EntityRefs& refs,
    const Scene& scene,
    const ResourceManager& resources,
    UsedSlots& used
) {
    terrainsNamed(refs, scene, resources, used);
}

void texturesInUse(
    const EntityRefs& refs,
    const Scene& scene,
    const ResourceManager& resources,
    UsedSlots& used
) {
    texturesNamed(refs, scene, resources, used);
    // Every material, not only drawn ones: an unplaced material still owns its textures.
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle, const MaterialAsset& m) {
        for (TextureHandle MaterialAsset::* slot : MATERIAL_TEXTURE_SLOTS) {
            if (m.*slot) used.mark((m.*slot).id());
        }
    });
}

void skeletonsInUse(
    const EntityRefs& refs,
    const Scene& scene,
    const ResourceManager& resources,
    UsedSlots& used
) {
    skeletonsNamed(refs, scene, resources, used);
    // Meshes and clips name their rig as a string, invisible to a handle walk.
    const auto claimByName = [&](const std::string& name) {
        if (const SkeletonHandle rig = resources.findByName<SkeletonAsset>(name)) {
            used.mark(rig.id());
        }
    };
    resources.forEachOfType<MeshAsset>([&](MeshHandle, const MeshAsset& m) {
        if (!m.skin.empty()) claimByName(m.skeleton);
    });
    resources.forEachOfType<AnimationClipAsset>([&](AnimationClipHandle, const AnimationClipAsset& c) {
        claimByName(c.skeleton);
    });
}

void clipsInUse(
    const EntityRefs& refs,
    const Scene& scene,
    const ResourceManager& resources,
    UsedSlots& used
) {
    clipsNamed(refs, scene, resources, used);
    // A crossfade's outgoing clip is unsaved session state but still sampled.
    scene.forEach<Animator>([&](EntityId, const Animator& a) {
        if (a.fadeFrom) used.mark(a.fadeFrom.id());
    });
}

void soundsInUse(
    const EntityRefs& refs,
    const Scene& scene,
    const ResourceManager& resources,
    UsedSlots& used
) {
    soundsNamed(refs, scene, resources, used);
}

// A kind is a builder below plus its place in KINDS; an omitted field is null (no picture,
// assign or rename). The rail runs in KINDS order, each in its accent.
AssetKind materialKind() {
    AssetKind kind{};
    kind.type        = AssetType::Material;
    kind.label       = "Materials";
    kind.empty       = "No materials yet. New makes a blank one.";
    kind.icon        = EditorIcon::Material;
    kind.accent      = &EditorStyle::Accent::MAT_BASE;
    kind.verb        = "New";
    kind.verbHint    = "Create a blank material and open it in a window of its own";
    kind.verbAction  = Verb::NewMaterial;
    kind.assignLabel = "Assign to selected entity";
    kind.assignHint  = "(select a mesh entity to assign)";
    kind.rows        = &KindOps<MaterialAsset>::rows;
    kind.detail      = &materialDetail;
    kind.thumb       = &materialThumb;
    kind.target      = &meshTarget;
    kind.assign      = &assignMaterial;
    kind.used        = &materialsInUse;
    kind.named       = &materialsNamed;
    kind.rename      = &KindOps<MaterialAsset>::rename;
    kind.remove      = &KindOps<MaterialAsset>::remove;
    kind.find        = &KindOps<MaterialAsset>::find;
    return kind;
}

AssetKind textureKind() {
    AssetKind kind{};
    kind.type       = AssetType::Texture;
    kind.label      = "Textures";
    kind.empty      = "No textures yet. Import brings an image in.";
    kind.icon       = EditorIcon::Texture;
    kind.accent     = &EditorStyle::Accent::MAT_TEXTURE;
    kind.verb       = "Import...";
    kind.verbHint   = "Import an image as color - PNG, JPG, TGA or BMP";
    kind.verbAction = Verb::ImportTexture;
    // No assign item: which material slot is not an entity's question.
    kind.rows       = &KindOps<TextureAsset>::rows;
    kind.detail     = &textureDetail;
    kind.thumb      = &textureThumb;
    kind.used       = &texturesInUse;
    kind.named      = &texturesNamed;
    kind.rename     = &KindOps<TextureAsset>::rename;
    kind.remove     = &KindOps<TextureAsset>::remove;
    kind.find       = &KindOps<TextureAsset>::find;
    return kind;
}

AssetKind meshKind() {
    AssetKind kind{};
    kind.type        = AssetType::Mesh;
    kind.label       = "Meshes";
    kind.empty       = "No meshes yet. Import brings some in.";
    kind.icon        = EditorIcon::Mesh;
    kind.accent      = &EditorStyle::Accent::MESH;
    kind.verb        = "Import...";
    kind.verbHint    = "Import a model - glTF, GLB, OBJ or FBX";
    kind.verbAction  = Verb::ImportModel;
    kind.assignLabel = "Assign to selected entity";
    kind.assignHint  = "(select a mesh entity to assign)";
    kind.rows        = &KindOps<MeshAsset>::rows;
    kind.detail      = &meshDetail;
    kind.thumb       = &meshThumb;
    kind.target      = &meshTarget;
    kind.assign      = &assignMesh;
    kind.used        = &meshesInUse;
    kind.named       = &meshesNamed;
    kind.rename      = &KindOps<MeshAsset>::rename;
    kind.remove      = &KindOps<MeshAsset>::remove;
    kind.find        = &KindOps<MeshAsset>::find;
    return kind;
}

AssetKind skeletonKind() {
    AssetKind kind{};
    kind.type        = AssetType::Skeleton;
    kind.label       = "Skeletons";
    kind.empty       = "No rigs yet. Import a rigged model to get one.";
    kind.icon        = EditorIcon::Skeleton;
    kind.accent      = &EditorStyle::Accent::TRANSFORM;
    kind.verb        = "Import...";
    kind.verbHint    = "Import a rigged model - glTF, GLB or FBX";
    kind.verbAction  = Verb::ImportModel;
    kind.assignLabel = "Assign to selected Animator";
    kind.assignHint  = "(select an entity with an Animator)";
    // SkeletalAnimationSystem::resolveClip refuses a clip whose rig name stops matching.
    kind.noRename    = "(a mesh and a clip name their rig - a rename would unbind them)";
    kind.rows        = &KindOps<SkeletonAsset>::rows;
    kind.detail      = &skeletonDetail;
    kind.target      = &animatorTarget;
    kind.assign      = &assignSkeleton;
    kind.used        = &skeletonsInUse;
    kind.named       = &skeletonsNamed;
    kind.remove      = &KindOps<SkeletonAsset>::remove;
    kind.find        = &KindOps<SkeletonAsset>::find;
    return kind;
}

AssetKind clipKind() {
    AssetKind kind{};
    kind.type        = AssetType::AnimationClip;
    kind.label       = "Clips";
    kind.empty       = "No animation clips yet. Import an animated model to get some.";
    kind.icon        = EditorIcon::Anim;
    kind.accent      = &EditorStyle::Accent::ANIM;
    kind.verb        = "Import...";
    kind.verbHint    = "Import an animated model - glTF, GLB or FBX";
    kind.verbAction  = Verb::ImportModel;
    kind.assignLabel = "Assign to selected Animator";
    kind.assignHint  = "(select an entity with an Animator)";
    kind.rows        = &KindOps<AnimationClipAsset>::rows;
    kind.detail      = &clipDetail;
    kind.target      = &animatorTarget;
    kind.assign      = &assignClip;
    kind.used        = &clipsInUse;
    kind.named       = &clipsNamed;
    kind.rename      = &KindOps<AnimationClipAsset>::rename;
    kind.remove      = &KindOps<AnimationClipAsset>::remove;
    kind.find        = &KindOps<AnimationClipAsset>::find;
    return kind;
}

AssetKind soundKind() {
    AssetKind kind{};
    kind.type        = AssetType::AudioClip;
    kind.label       = "Sounds";
    kind.empty       = "No sounds yet. Import decodes a wav / mp3 / flac.";
    kind.icon        = EditorIcon::Audio;
    kind.accent      = &EditorStyle::Accent::AUDIO;
    kind.verb        = "Import...";
    kind.verbHint    = "Import a sound - WAV, MP3 or FLAC";
    kind.verbAction  = Verb::ImportSound;
    kind.assignLabel = "Assign to selected Audio Source";
    kind.assignHint  = "(select an entity with an Audio Source)";
    kind.rows        = &KindOps<AudioClipAsset>::rows;
    kind.detail      = &soundDetail;
    kind.target      = &audioTarget;
    kind.assign      = &assignSound;
    kind.used        = &soundsInUse;
    kind.named       = &soundsNamed;
    kind.rename      = &KindOps<AudioClipAsset>::rename;
    kind.remove      = &KindOps<AudioClipAsset>::remove;
    kind.find        = &KindOps<AudioClipAsset>::find;
    return kind;
}

AssetKind terrainKind() {
    AssetKind kind{};
    kind.type        = AssetType::Terrain;
    kind.label       = "Terrains";
    kind.empty       = "No terrains yet. New makes a flat one to sculpt.";
    kind.icon        = EditorIcon::Terrain;
    kind.accent      = &EditorStyle::Accent::TERRAIN;
    kind.verb        = "New";
    kind.verbHint    = "Create a terrain entity on a new terrain and open the Terrain window";
    kind.verbAction  = Verb::NewTerrain;
    kind.assignLabel = "Assign to selected entity";
    kind.assignHint  = "(select a terrain entity to assign)";
    // Its name is the path of the file it is saved to.
    kind.noRename    = "(a terrain's name is the path of its .terrain file)";
    kind.rows        = &KindOps<TerrainAsset>::rows;
    kind.detail      = &terrainDetail;
    kind.target      = &terrainTarget;
    kind.assign      = &assignTerrain;
    kind.used        = &terrainsInUse;
    kind.named       = &terrainsNamed;
    kind.remove      = &KindOps<TerrainAsset>::remove;
    kind.find        = &KindOps<TerrainAsset>::find;
    return kind;
}

const AssetKind KINDS[] = {
    materialKind(),
    textureKind(),
    meshKind(),
    skeletonKind(),
    clipKind(),
    soundKind(),
    terrainKind()
};

static_assert(std::size(KINDS) == static_cast<size_t>(AssetType::Count), "as many kinds as asset types");

// Null for a type no kind is: a drag's payload is bytes, and a kind is never stood in for another.
const AssetKind* kindOf(AssetType type) {
    for (const AssetKind& k : KINDS) {
        if (k.type == type) return &k;
    }
    return nullptr;
}

/**
 * @brief The rows of @p kind matching @p filter: what is loaded, then the library's names that are
 *        not, all by name.
 *
 * @param kind      The kind.
 * @param resources What is loaded.
 * @param library   The library's names of the kind.
 * @param filter    The search.
 * @param out       Filled.
 */
void listRows(
    const AssetKind& kind,
    const ResourceManager& resources,
    const std::vector<std::string>& library,
    const char* filter,
    std::vector<AssetRow>& out
) {
    out.clear();
    kind.rows(resources, [&](const AssetRow& row) {
        if (matchesFilter(row.name.c_str(), filter)) out.push_back(row);
    });
    for (const std::string& name : library) {
        if (!matchesFilter(name.c_str(), filter) || kind.find(resources, name)) continue;
        out.push_back(AssetRow{{}, name, 0, false});
    }
    std::sort(out.begin(), out.end(), [](const AssetRow& a, const AssetRow& b) { return a.name < b.name; });
}

/**
 * @brief What a tile calls an asset: its file's stem (assetFileStem) with what follows it,
 *        "ArmChair_1k:mat0" of "assets/models/ArmChair/ArmChair_1k.gltf:mat0"; a name that names
 *        no file, whole.
 *
 * A model's assets share its folders, which an elided full name would show alone.
 *
 * @param name The asset's name.
 * @return Its caption; the tooltip keeps the whole name.
 */
std::string tileCaption(const std::string& name) {
    const std::string stem = assetFileStem(name);
    if (stem.empty()) return name;
    const size_t colon = name.find(':');
    return colon == std::string::npos ? stem : stem + name.substr(colon);
}

/**
 * @brief Load a library row's asset, saying so when it cannot be.
 *
 * @param ec   The editor.
 * @param kind Its kind.
 * @param name Its name.
 * @return Its key, or a null one.
 */
StorageIndex loadRow(EditorContext& ec, const AssetKind& kind, const std::string& name) {
    if (!AssetSerializer::loadNamed(kind.type, name, ec.frame.resources)) {
        ec.state.pushToast(ToastKind::Error, "Could not load " + name);
        return {};
    }
    return kind.find(ec.frame.resources, name);
}

/// How far what @p drag makes reaches below its origin: a mesh stands on its base.
float dropBase(const ResourceManager& resources, const AssetDrag& drag) {
    return drag.type == AssetType::Mesh ? ViewPlacement::meshBase(resources, MeshHandle{drag.key}) : 0.0f;
}

} // namespace

void AssetBrowserPanel::acceptViewportDrop(EditorContext& ec) {
    const ImVec2 min = ec.viewportPos;
    const ImRect rect(min, ImVec2(min.x + ec.viewportSize.x, min.y + ec.viewportSize.y));
    if (!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##AssetDrop"))) return;

    // Peeked first, so a drag that would neither assign nor make anything here is not taken.
    FrameContext&                  ctx    = ec.frame;
    const std::optional<AssetDrag> peeked = peekAssetDrag();
    if (peeked && ctx.visibility && ctx.visibility->hasCamera) {
        const AssetDrag  drag        = *peeked;
        const glm::mat4& invViewProj = ctx.visibility->camera.invViewProj;
        const Math::Ray  ray         = viewportRay(invViewProj, ImGui::GetMousePos(), min, ec.viewportSize);

        EntityId hit{};
        float    t = 0.0f;
        ViewPlacement::firstSurface(ctx.scene, ctx.resources, ray, t, {}, &hit);
        // What the ray meets is a drawn surface; the entity a kind assigns to may hold it, as an
        // imported character's Animator holds its skinned meshes. A mesh dropped on a mesh is a
        // new thing set on it, never the floor turned into a crate.
        const AssetKind* kind = kindOf(drag.type);
        EntityId         target{};
        if (hit && drag.type != AssetType::Mesh && kind && kind->assign) {
            HierarchyOperations::forSelfAndAncestors(ctx.scene, hit, [&](EntityId at) {
                if (kind->target(ctx.scene, at)) target = at;
                return !target;
            });
        }
        if (target) {
            if (ImGui::AcceptDragDropPayload(ASSET_DRAG)) {
                kind->assign(ctx.scene, ctx.resources, ec.state, target, drag.key);
                ec.state.selectEntity(target);
            }
        } else if (EditorActions::makesEntity(drag.type) && ImGui::AcceptDragDropPayload(ASSET_DRAG)) {
            const float     base  = dropBase(ctx.resources, drag);
            const float     ahead = ec.state.viewAim.focus;
            const glm::vec3 at    = ViewPlacement::placeAlong(ctx.scene, ctx.resources, ray, ahead, base);
            EditorActions::createFromAsset(ctx.scene, ctx.resources, ec.state, drag.type, drag.key, at);
        }
    }
    ImGui::EndDragDropTarget();
}

void AssetBrowserPanel::acceptAssetOn(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId entity
) {
    // Peeked first, so a drag of a kind that does not assign here is not taken (nor the row lit).
    const std::optional<AssetDrag> drag = peekAssetDrag();
    if (!drag) return;
    const AssetKind* kind = kindOf(drag->type);
    if (!kind || !kind->assign || !kind->target(scene, entity)) return;
    if (ImGui::AcceptDragDropPayload(ASSET_DRAG)) {
        kind->assign(scene, resources, state, entity, drag->key);
        state.selectEntity(entity);
    }
}

void AssetBrowserPanel::acceptAssetCreate(Scene& scene, ResourceManager& resources, EditorState& state) {
    // Peeked first: only a kind an entity is made of is taken, so a material is not swallowed.
    const std::optional<AssetDrag> drag = peekAssetDrag();
    if (!drag || !EditorActions::makesEntity(drag->type) || !ImGui::AcceptDragDropPayload(ASSET_DRAG)) return;
    const float     base = dropBase(resources, *drag);
    const glm::vec3 at   = ViewPlacement::placeInView(scene, resources, state.viewAim, base);
    EditorActions::createFromAsset(scene, resources, state, drag->type, drag->key, at);
}

void AssetBrowserPanel::openRename(AssetType kind, StorageIndex key, const std::string& name) {
    snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", name.c_str());
    m_rename = AssetTarget{kind, key, name, true};
}

const std::vector<std::string>& AssetBrowserPanel::libraryNames(AssetType type) {
    const size_t   at  = static_cast<size_t>(type);
    const uint64_t now = AssetLibrary::get().revision() + 1;
    if (m_libraryAt[at] != now) {
        m_library[at]   = AssetLibrary::get().namesOf(type);
        m_libraryAt[at] = now;
    }
    return m_library[at];
}

void AssetBrowserPanel::ensureAssets(ResourceManager& resources) {
    m_sphere = materialPreviewSphere(resources);

    m_neutral = neutralPreviewMaterial(resources);
}

void AssetBrowserPanel::draw(EditorContext& ec) {
    ensureAssets(ec.frame.resources);

    drawRail(ec);
    ImGui::SameLine();

    ImGui::BeginGroup();
    drawToolbar(ec);
    ImGui::Separator();
    if (ImGui::BeginChild("##abGrid")) drawGrid(ec);
    ImGui::EndChild();
    ImGui::EndGroup();

    // At panel scope: OpenPopup hashes against the calling window, so the child's would not match.
    serviceTextureImport(ec);
    serviceSoundImport(ec);
    drawRenameModal(ec);
    drawDeleteModal(ec);
}

void AssetBrowserPanel::drawRail(EditorContext& ec) {
    const float width = EditorStyle::px(150.0f);
    ImGui::BeginChild("##abRail", ImVec2(width, 0), ImGuiChildFlags_Borders);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float stripe = EditorStyle::px(3.0f);

    // Through the grid's filter, so searching narrows the rail too; counted again only once the
    // assets, the library or the search change.
    const RailKey key{ec.frame.resources.revision(), AssetLibrary::get().revision(), m_filter};
    if (key != m_railKey) {
        std::vector<AssetRow> rows;
        for (const AssetKind& kind : KINDS) {
            listRows(kind, ec.frame.resources, libraryNames(kind.type), m_filter, rows);
            m_railCounts[static_cast<size_t>(kind.type)] = rows.size();
        }
        m_railKey = key;
    }
    for (const AssetKind& kind : KINDS) {
        const size_t count = m_railCounts[static_cast<size_t>(kind.type)];

        ImGui::PushID(static_cast<int>(kind.type));

        // The kind's own hue, not the editor accent.
        ImVec4 tint = *kind.accent;
        tint.w = 0.20f;
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tint);
        tint.w = 0.32f;
        ImGui::PushStyleColor(ImGuiCol_Header, tint);
        tint.w = 0.44f;
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, tint);

        ImGui::Indent(stripe + EditorStyle::px(4.0f));
        const bool selected = m_kind == kind.type;
        if (entitySelectable("kind", selected, kind.icon, kind.label)) m_kind = kind.type;
        ImGui::Unindent(stripe + EditorStyle::px(4.0f));
        ImGui::PopStyleColor(3);

        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();

        const ImU32 accent = ImGui::GetColorU32(*kind.accent);
        dl->AddRectFilled(
            ImVec2(mn.x, mn.y + 1.0f),
            ImVec2(mn.x + stripe, mx.y - 1.0f),
            selected ? accent : (accent & 0x60FFFFFF)
        );

        char buf[16];
        snprintf(buf, sizeof(buf), "%zu", count);
        const float textW = ImGui::CalcTextSize(buf).x;
        const ImVec2 countPos(
            mx.x - textW - EditorStyle::px(6.0f),
            mn.y + (mx.y - mn.y - ImGui::GetTextLineHeight()) * 0.5f
        );
        dl->AddText(countPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), buf);
        ImGui::PopID();
    }

    ImGui::EndChild();
}

void AssetBrowserPanel::drawToolbar(EditorContext& ec) {
    const AssetKind* found = kindOf(m_kind);
    if (!found) return;
    const AssetKind& kind = *found;

    // The kind's primary action, always first; see docs/reference/editor.md, "Import".
    if (ImGui::Button(kind.verb)) {
        switch (kind.verbAction) {
            case Verb::NewMaterial:
                if (MaterialHandle h = EditorActions::createNewMaterial(ec.frame.resources, ec.state)) {
                    ec.state.openMaterial(h);
                }
                break;
            case Verb::NewTerrain: {
                const EntityId made = EditorActions::createEntity(
                    ec.frame.scene,
                    ec.frame.resources,
                    ec.state,
                    EditorActions::EntityKind::Terrain
                );
                ec.state.selectEntity(made);
                break;
            }
            case Verb::ImportModel:   ec.state.requestModelImport = true; break;
            case Verb::ImportTexture: openTextureImport(); break;
            case Verb::ImportSound:   openSoundImport();   break;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kind.verbHint);

    ImGui::SameLine();
    searchField("##assetFilter", m_filter, sizeof(m_filter), EditorStyle::px(200.0f));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(EditorStyle::px(130.0f));
    sliderFloat("##cell", &m_cell, 64.0f, 200.0f, "%.0f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tile size");

    // Two ways a clip is inaudible though nothing is wrong; a muted mix even looks like it plays.
    if (m_kind == AssetType::AudioClip) {
        AudioDevice& device = ec.audioSystem.device();
        if (!device.isOpen()) {
            ImGui::SameLine();
            ImGui::TextColored(EditorStyle::WARNING, "No audio device.");
        } else if (device.masterVolume() <= 0.0f) {
            ImGui::SameLine();
            ImGui::TextColored(EditorStyle::WARNING, "Listener volume is 0.");
        }
    }
}

void AssetBrowserPanel::drawGrid(EditorContext& ec) {
    const AssetKind* found = kindOf(m_kind);
    if (!found) return;
    const AssetKind& kind      = *found;
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;
    Scene&           scene     = ec.frame.scene;

    const EntityId sel = state.selectedEntity;
    const bool canAssign = kind.assign && sel && scene.isAlive(sel) && kind.target(scene, sel);

    // Snapshotted for the clipper: an off-screen tile bakes no thumbnail.
    std::vector<AssetRow> rows;
    listRows(kind, resources, libraryNames(kind.type), m_filter, rows);
    if (rows.empty()) {
        ImGui::TextDisabled("%s", m_filter[0] ? "Nothing matches the search." : kind.empty);
        return;
    }

    // The project's loading screen uses what it shows, though nothing in the scene does: deleted
    // while it is built, its handle would die under it.
    const Scene* screen = ec.loadingSystem.builtScreen();
    const std::array<uint64_t, 4> stamp = {
        scene.epoch(),
        resources.revision(),
        state.commands.revision(),
        reinterpret_cast<uintptr_t>(screen)
    };
    if (m_refsKey != stamp || ec.input.playing) {
        m_refs = EntityRefs{};
        scene.forEachEntity([&](EntityId id) {
            AssetSerializer::collectAssetRefs(scene, id, resources, m_refs);
        });
        if (screen) {
            screen->forEachEntity([&](EntityId id) {
                AssetSerializer::collectAssetRefs(*screen, id, resources, m_refs);
            });
        }
        m_refsKey = stamp;
    }
    std::fill(m_used.begin(), m_used.end(), uint8_t{0});
    UsedSlots used{m_used};
    kind.used(m_refs, scene, resources, used);

    TileContext tc{ec, m_sphere, m_neutral, TEXTURE_UPLOADS_PER_FRAME};
    AudioDevice& device = ec.audioSystem.device();

    const float face = EditorStyle::px(m_cell);
    const float step = face + ImGui::GetStyle().ItemSpacing.x;
    const int   cols = (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / step));

    // A library row's asset, loaded as it is first used; one that fails is not tried again.
    const auto take = [&](const AssetRow& row) -> StorageIndex {
        if (row.held) return row.key;
        if (row.name == m_unloadable) return {};
        const StorageIndex key = loadRow(ec, kind, row.name);
        if (!key) m_unloadable = row.name;
        return key;
    };

    // A loaded asset's own menu items: renaming and deleting it.
    const auto heldItems = [&](const AssetRow& row, bool orphan) {
        ImGui::Separator();
        ImGui::BeginDisabled(!kind.rename);
        if (ImGui::MenuItem("Rename...")) openRename(kind.type, row.key, row.name);
        ImGui::EndDisabled();
        if (!kind.rename) ImGui::TextDisabled("%s", kind.noRename);
        // Not during a session: delete discards the undo history, the set-aside one too.
        ImGui::BeginDisabled(!orphan || ec.input.playing);
        if (ImGui::MenuItem("Delete...")) {
            m_delete = AssetTarget{kind.type, row.key, row.name, true};
        }
        ImGui::EndDisabled();
        if (ec.input.playing) {
            ImGui::TextDisabled("(stop the session first)");
        } else if (!orphan) {
            ImGui::TextDisabled("(in use - clear the references first)");
        }
        // Who it is in use by: the entities naming it, or, when none does, the loading screen or
        // another asset holding it.
        if (!orphan && ImGui::MenuItem("Select Users")) {
            std::vector<uint8_t>  marks;
            std::vector<EntityId> users;
            const auto names = [&](const Scene& in, EntityId id) {
                EntityRefs refs;
                AssetSerializer::collectAssetRefs(in, id, resources, refs);
                std::fill(marks.begin(), marks.end(), uint8_t{0});
                UsedSlots named{marks};
                kind.named(refs, in, resources, named);
                return named.has(row.key.index);
            };
            scene.forEachEntity([&](EntityId id) {
                if (names(scene, id)) users.push_back(id);
            });
            bool onScreen = false;
            if (users.empty() && screen) {
                screen->forEachEntity([&](EntityId id) { onScreen = onScreen || names(*screen, id); });
            }
            // The selection stands when no entity uses it.
            if (users.empty()) {
                const char* why = onScreen
                    ? " is used by the project's loading screen, not by an entity"
                    : " is held by another asset, not by an entity";
                state.pushToast(ToastKind::Info, row.name + why);
            } else {
                state.deselect();
                for (const EntityId id : users) state.addToSelection(id);
            }
        }
    };

    // Into its own window; a terrain into the Terrain window, on the entity placing it.
    const bool opens = AssetEditors::opens(kind.type) || kind.type == AssetType::Terrain;
    const auto open  = [&](StorageIndex key) {
        if (kind.type != AssetType::Terrain) {
            state.openAssetEditor(kind.type, key);
            return;
        }
        EntityId placing{};
        scene.forEach<Terrain>([&](EntityId id, const Terrain& terrain) {
            if (!placing && terrain.terrain == TerrainHandle{key}) placing = id;
        });
        // None yet: one is placed, as dropping it in the viewport would.
        if (!placing) placing = EditorActions::createFromAsset(scene, resources, state, kind.type, key, {});
        if (!placing) return;
        state.selectEntity(placing);
        state.openTerrain(placing);
    };

    const auto drawTile = [&](const AssetRow& row) {
        const uint32_t     id     = row.key.index;
        const GpuTextureId tex    = row.held && kind.thumb ? kind.thumb(tc, row.key, row.version) : 0u;
        const bool         orphan = row.held && !used.has(id);
        const char*        name   = row.name.empty() ? "(unnamed)" : row.name.c_str();

        // By name, so a row keeps its id as it is loaded mid-drag.
        if (row.name.empty()) ImGui::PushID(static_cast<int>(id));
        else                   ImGui::PushID(row.name.c_str());
        ImGui::BeginGroup();

        // A faint glyph means a bake is coming, unlike a kind with no thumbnail; or, for an asset not
        // loaded, that there is nothing to show until it is.
        ImVec4 glyph = *kind.accent;
        if (kind.thumb || !row.held) glyph.w = 0.30f;
        ImVec2 faceMin, faceMax;
        tileFace(imTexture(tex), face, *kind.accent, kind.icon, glyph, faceMin, faceMax);
        // The face's own hover, which a window lying over it takes; not its rectangle's.
        const bool faceHovered = ImGui::IsItemHovered();
        // Into the viewport, onto an asset slot, or onto an entity that takes it.
        if (ImGui::BeginDragDropSource()) {
            if (const StorageIndex key = take(row)) {
                const AssetDrag drag{kind.type, key};
                ImGui::SetDragDropPayload(ASSET_DRAG, &drag, sizeof(drag));
            }
            ImGui::TextUnformatted(name);
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered()) {
            if (row.held) {
                char full[256];
                kind.detail(resources, row.key, true, full, sizeof(full));
                ImGui::SetTooltip("%s\n%s%s", name, full, orphan ? "\nNothing in this scene uses it" : "");
            } else {
                ImGui::SetTooltip("%s\nIn the library, not loaded: using it loads it", name);
            }
            // Not Delete: it destroys the selected entity first, so both fire.
            if (row.held && kind.rename && ImGui::IsKeyPressed(RENAME_KEY)) {
                openRename(kind.type, row.key, row.name);
            }
        }

        if (ImGui::BeginPopupContextItem("##tilectx")) {
            sectionLabel(name);
            ImGui::Separator();
            if (!row.held && ImGui::MenuItem("Load")) take(row);
            if (opens && ImGui::MenuItem("Open")) {
                if (const StorageIndex key = take(row)) open(key);
            }
            if (kind.assignLabel) {
                ImGui::BeginDisabled(!canAssign);
                if (ImGui::MenuItem(kind.assignLabel)) {
                    if (const StorageIndex key = take(row)) kind.assign(scene, resources, state, sel, key);
                }
                ImGui::EndDisabled();
                if (!canAssign) ImGui::TextDisabled("%s", kind.assignHint);
            }
            if (row.held) heldItems(row, orphan);
            ImGui::EndPopup();
        }

        // On the face, so the tile keeps the common height.
        const bool mine = kind.type == AssetType::AudioClip && m_previewClip == AudioClipHandle{row.key};
        if (kind.type == AssetType::AudioClip && row.held) {
            const float ih  = ImGui::GetFrameHeight();
            const float pad = EditorStyle::px(5.0f);
            const ImVec2 back = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(faceMin.x + pad, faceMax.y - ih - pad));
            const AudioClipAsset& clip = resources.get(AudioClipHandle{row.key});
            if (auditionTransport("abSnd", device, m_previewVoice, mine, &clip, ih))
                m_previewClip = AudioClipHandle{row.key};
            ImGui::SetCursorScreenPos(back);
        }

        // A double-click opens an asset in its own window, as other editors do; a material too. One
        // not loaded is loaded by it.
        const bool doubled = faceHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (doubled && (opens || !row.held)) {
            const StorageIndex key = take(row);
            if (key && opens) open(key);
        }

        clippedLine(tileCaption(row.name).c_str(), face, !row.held);

        // While heard, the detail line shows the audition's progress.
        if (!row.held) {
            clippedLine("not loaded", face, true);
        } else if (mine && device.isVoiceActive(m_previewVoice)) {
            auditionScrubber(
                "abPos",
                device,
                m_previewVoice,
                resources.get(AudioClipHandle{row.key}).duration(),
                face
            );
        } else {
            char detail[96];
            kind.detail(resources, row.key, false, detail, sizeof(detail));
            clippedLine(detail, face, true);
        }

        ImGui::EndGroup();

        ImVec4 strip = *kind.accent;
        if (orphan || !row.held) strip.w *= 0.35f;
        tileStrip(faceMin, faceMax, strip);

        ImGui::PopID();
    };

    // Clipped by grid line, each one tile tall, as the clipper needs.
    const int count = static_cast<int>(rows.size());
    ImGuiListClipper clipper;
    clipper.Begin((count + cols - 1) / cols);
    while (clipper.Step()) {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
            const int last = (std::min)(count, (line + 1) * cols);
            for (int i = line * cols; i < last; ++i) {
                drawTile(rows[static_cast<size_t>(i)]);
                if (i + 1 < last) ImGui::SameLine();
            }
        }
    }
}

void AssetBrowserPanel::openTextureImport() {
    AssetPicker::Options options;
    options.title      = "Import Texture";
    options.root       = ProjectPaths::assets();
    options.recursive  = true;
    options.extensions.assign(IMAGE_EXTENSIONS.begin(), IMAGE_EXTENSIONS.end());
    options.hint       = "PNG / JPG / TGA / BMP, read as color (sRGB)";
    m_texturePicker.open(options);
}

void AssetBrowserPanel::serviceTextureImport(EditorContext& ec) {
    std::string picked;
    if (!m_texturePicker.draw(picked)) return;

    FileImport::importTexture(picked, ec.frame.resources, ec.state);
}

void AssetBrowserPanel::openSoundImport() {
    AssetPicker::Options options;
    options.title      = "Import Sound";
    options.root       = ProjectPaths::assets();
    options.recursive  = true;
    options.extensions.assign(AUDIO_EXTENSIONS.begin(), AUDIO_EXTENSIONS.end());
    options.maxResults = 2000;
    options.hint       = "WAV / MP3 / FLAC";
    m_soundPicker.open(options);
}

void AssetBrowserPanel::serviceSoundImport(EditorContext& ec) {
    std::string picked;
    if (!m_soundPicker.draw(picked)) return;

    FileImport::importSound(picked, ec.frame.resources, ec.state);
}

void AssetBrowserPanel::drawRenameModal(EditorContext& ec) {
    const bool renamed = renameDialog("Rename Asset", m_rename.open, m_renameBuf, sizeof(m_renameBuf));
    if (renamed) {
        const AssetKind* kind = kindOf(m_rename.kind);
        if (m_rename.key && kind && kind->rename) {
            kind->rename(ec, m_rename.key, m_rename.name, m_renameBuf);
        }
    }
    // Cleared by any close, so a dismissed dialog leaves no target armed.
    if (!m_rename.open) m_rename.key = {};
}

void AssetBrowserPanel::drawDeleteModal(EditorContext& ec) {
    if (!beginDialog("Delete Asset", m_delete.open)) return;

    ImGui::Text("Delete '%s'?", m_delete.name.c_str());
    // A restored asset would get a new slot, so undo cannot bring it back.
    ImGui::TextDisabled("Undo cannot bring it back.");

    // Orphans only, but the step that cleared the last reference still holds its handle,
    // checked for null not life; so the history goes too.
    const size_t depth = ec.state.commands.undoDepth() + ec.state.commands.redoDepth();
    if (depth > 0) {
        ImGui::TextDisabled(
            "%zu undo step%s will be discarded: a step from before the asset was orphaned still names it.",
            depth,
            depth == 1 ? "" : "s"
        );
    }

    const bool         destructive = true;
    const DialogResult r = dialogButtons(m_delete.open, "Delete", nullptr, true, false, destructive);
    if (r == DialogResult::Confirm && m_delete.key) {
        // The voice holds the samples by shared_ptr: a removed clip would play on, unstoppable.
        if (m_delete.kind == AssetType::AudioClip && m_previewClip == AudioClipHandle{m_delete.key}) {
            ec.audioSystem.device().stopVoice(m_previewVoice);
            m_previewVoice = 0;
            m_previewClip  = {};
        }
        ec.materialPreviews.evict(AssetEditors::thumbnailKey(m_delete.kind, m_delete.key.index));
        if (const AssetKind* kind = kindOf(m_delete.kind)) kind->remove(ec.frame.resources, m_delete.key);
        ec.state.forgetWorldState();
        ec.state.markSceneDirty();
    }
    if (r != DialogResult::None) m_delete.key = {};
    endDialog();
}

} // namespace Vkm::Engine
