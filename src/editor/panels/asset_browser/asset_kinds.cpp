#include "panels/asset_browser/asset_kinds.h"

#include <cstdio>
#include <string>

#include "actions/asset_actions.h"
#include "ecs/component/animation/animator.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "preview/preview_cache.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

using EntityRefs = AssetKind::EntityRefs;

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
    const size_t tris     = mesh.indices.size() / 3;
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
    req.key      = PreviewCache::thumbnailKey(AssetType::Material, MaterialHandle{key}.id());
    req.draws    = {{tc.sphere, MaterialHandle{key}}};
    req.yawDeg   = 30.0f;
    req.pitchDeg = 18.0f;
    req.distance = 2.6f;
    const bool live = false;
    return tc.ec.previews.texture(tc.ec.frame.resources, req, version, live);
}

GpuTextureId meshThumb(TileContext& tc, StorageIndex key, uint64_t version) {
    PreviewRequest req;
    req.key      = PreviewCache::thumbnailKey(AssetType::Mesh, MeshHandle{key}.id());
    req.draws    = {{MeshHandle{key}, tc.neutral}};
    req.yawDeg   = 25.0f;
    req.pitchDeg = 15.0f;
    req.distance = 2.6f;
    const bool live = false;
    return tc.ec.previews.texture(tc.ec.frame.resources, req, version, live);
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

// Usage walks. Entity references come from SceneAssets::collectAssetRefs, the walk a scene save
// uses; what other assets hold - a terrain's materials and meshes, a material's maps, the rig a
// mesh or clip names - walks the resources (see docs/reference/editor.md).

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
    SceneAssets::forEachMaterialOf(refs, resources, mark);
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
        terrain.forEachReference(mark, [](MeshHandle) {});
    });
}

void meshesInUse(
    const EntityRefs& refs,
    const Scene& scene,
    const ResourceManager& resources,
    UsedSlots& used
) {
    meshesNamed(refs, scene, resources, used);
    const auto mark = [&](MeshHandle mesh) { used.mark(mesh.id()); };
    resources.forEachOfType<TerrainAsset>([&](TerrainHandle, const TerrainAsset& terrain) {
        terrain.forEachReference([](MaterialHandle) {}, mark);
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

// A kind is a builder below plus its place in ASSET_KINDS; an omitted field is null (no picture,
// assign or rename).
AssetKind materialKind() {
    AssetKind kind{};
    kind.type        = AssetType::Material;
    kind.label       = "Materials";
    kind.empty       = "No materials yet. New makes a blank one.";
    kind.icon        = EditorIcon::Material;
    kind.verb        = "New";
    kind.verbHint    = "Create a blank material and open it in a window of its own";
    kind.verbAction  = AssetKind::Verb::NewMaterial;
    kind.assignLabel = "Assign to selected entity";
    kind.assignHint  = "(select a mesh entity to assign)";
    kind.rows        = &KindOps<MaterialAsset>::rows;
    kind.detail      = &materialDetail;
    kind.thumb       = &materialThumb;
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
    kind.verb       = "Import...";
    kind.verbHint   = "Import an image as color - PNG, JPG, TGA or BMP";
    kind.verbAction = AssetKind::Verb::ImportTexture;
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
    kind.verb        = "Import...";
    kind.verbHint    = "Import a model - glTF, GLB, OBJ or FBX";
    kind.verbAction  = AssetKind::Verb::ImportModel;
    kind.assignLabel = "Assign to selected entity";
    kind.assignHint  = "(select a mesh entity to assign)";
    kind.rows        = &KindOps<MeshAsset>::rows;
    kind.detail      = &meshDetail;
    kind.thumb       = &meshThumb;
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
    kind.verb        = "Import...";
    kind.verbHint    = "Import a rigged model - glTF, GLB or FBX";
    kind.verbAction  = AssetKind::Verb::ImportModel;
    kind.assignLabel = "Assign to selected Animator";
    kind.assignHint  = "(select an entity with an Animator)";
    // SkeletalAnimationSystem::resolveClip refuses a clip whose rig name stops matching.
    kind.noRename    = "(a mesh and a clip name their rig - a rename would unbind them)";
    kind.rows        = &KindOps<SkeletonAsset>::rows;
    kind.detail      = &skeletonDetail;
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
    kind.verb        = "Import...";
    kind.verbHint    = "Import an animated model - glTF, GLB or FBX";
    kind.verbAction  = AssetKind::Verb::ImportModel;
    kind.assignLabel = "Assign to selected Animator";
    kind.assignHint  = "(select an entity with an Animator)";
    kind.rows        = &KindOps<AnimationClipAsset>::rows;
    kind.detail      = &clipDetail;
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
    kind.verb        = "Import...";
    kind.verbHint    = "Import a sound - WAV, MP3 or FLAC";
    kind.verbAction  = AssetKind::Verb::ImportSound;
    kind.assignLabel = "Assign to selected Audio Source";
    kind.assignHint  = "(select an entity with an Audio Source)";
    kind.rows        = &KindOps<AudioClipAsset>::rows;
    kind.detail      = &soundDetail;
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
    kind.verb        = "New";
    kind.verbHint    = "Create a terrain entity on a new terrain and open the Terrain window";
    kind.verbAction  = AssetKind::Verb::NewTerrain;
    kind.assignLabel = "Assign to selected entity";
    kind.assignHint  = "(select a terrain entity to assign)";
    // Its name is the path of the file it is saved to.
    kind.noRename    = "(a terrain's name is the path of its .terrain file)";
    kind.rows        = &KindOps<TerrainAsset>::rows;
    kind.detail      = &terrainDetail;
    kind.used        = &terrainsInUse;
    kind.named       = &terrainsNamed;
    kind.remove      = &KindOps<TerrainAsset>::remove;
    kind.find        = &KindOps<TerrainAsset>::find;
    return kind;
}

} // namespace

const AssetKind ASSET_KINDS[] = {
    materialKind(),
    textureKind(),
    meshKind(),
    skeletonKind(),
    clipKind(),
    soundKind(),
    terrainKind()
};

const AssetKind* kindOf(AssetType type) {
    for (const AssetKind& k : ASSET_KINDS) {
        if (k.type == type) return &k;
    }
    return nullptr;
}

} // namespace Vkm::Engine
