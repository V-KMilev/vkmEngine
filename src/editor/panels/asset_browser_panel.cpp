#include "panels/asset_browser_panel.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "ecs/component/animation/animator.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/render/decal.h"
#include "framework/editor_common.h"
#include "framework/editor_actions.h"
#include "framework/component_edit.h"
#include "framework/editor_commands.h"
#include "framework/material_preview_session.h"
#include "system/audio/audio_system.h"
#include "system/render/render_system.h"
#include "generator/mesh_generators.h"
#include "loader/audio_loaders.h"
#include "loader/texture_loaders.h"
#include "io/project_paths.h"
#include "ui/audition_transport.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {

// ---------------------------------------------------------------------------
// The type-erased shape every kind is drawn through.
// ---------------------------------------------------------------------------

/**
 * @brief One asset as the grid sees it: no C++ type, only what a tile needs.
 *
 * The handle is carried whole (index and generation) rather than as a bare id,
 * so the kind's own operations can rebuild the typed handle faithfully and a
 * recycled slot cannot be mistaken for the asset that used to live in it.
 */
struct AssetRow {
    StorageIndex       key;
    const std::string* name;
    uint64_t           version;
};

using RowSink = std::function<void(const AssetRow&)>;

// A texture the grid shows but nothing draws has no GPU mirror, and making one
// is a full upload. Capped so a rail of 4K maps fills in over several frames
// rather than stalling one, the way MaterialPreviewSession caps its bakes.
constexpr int TEXTURE_UPLOADS_PER_FRAME = 3;

/// What the tile face needs to render a preview, gathered once per frame.
struct TileContext {
    EditorContext& ec;
    MeshHandle     sphere;   ///< Shape material thumbnails are drawn on
    MaterialHandle neutral;  ///< Material mesh thumbnails are drawn under
    int            uploads;  ///< Texture uploads still allowed this frame
};

// Which import or create action a kind's primary verb raises. A tag rather
// than a callback because every one of them is a one-line flag on EditorState
// or a call into EditorActions, and naming them here keeps the table readable.
enum class Verb { NewMaterial, ImportModel, ImportTexture, ImportSound };

using RowsFn   = void (*)(const ResourceManager&, const RowSink&);
using DetailFn = void (*)(const ResourceManager&, StorageIndex, bool, char*, size_t);
using ThumbFn  = GpuTextureId (*)(TileContext&, StorageIndex, uint64_t);
using TargetFn = bool (*)(const Scene&, EntityId);
using AssignFn = void (*)(EditorContext&, EntityId, StorageIndex);
using UsedFn   = void (*)(const Scene&, const ResourceManager&, std::unordered_set<uint32_t>&);
using RenameFn = void (*)(EditorContext&, StorageIndex, const std::string&, const char*);
using RemoveFn = void (*)(ResourceManager&, StorageIndex);

/**
 * @brief Everything the browser needs to know about one asset kind.
 *
 * The body below walks this and never names an asset type, so a kind with no
 * thumbnail (`thumb` null), nothing on an entity to assign to (`assign` null)
 * or nothing safe to rename (`rename` null) still gets the same rail row, tile
 * and verb slot. The four unnullable slots are the ones all six kinds answer:
 * a kind that cannot be listed, described, walked for users or destroyed is not
 * one this panel can show.
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
    RenameFn      rename;       ///< Null: renaming would break a reference, see noRename
    RemoveFn      remove;
};

// ---------------------------------------------------------------------------
// Per-type operations. The only place an asset's C++ type is named.
// ---------------------------------------------------------------------------

template<typename Asset>
struct KindOps {
    static void rows(const ResourceManager& resources, const RowSink& sink) {
        resources.forEachOfType<Asset>([&](Handle<Asset> h, const Asset& a) {
            if (a.hidden) return;  // editor helpers / preview primitives are not user-facing
            sink(AssetRow{h.key, &a.name, a.version});
        });
    }

    static void rename(EditorContext& ec, StorageIndex key,
                       const std::string& from, const char* to) {
        const Handle<Asset> h{key};
        ResourceManager& resources = ec.frame.resources;
        resources.rename(h, to);

        // ResourceManager keeps names unique per type by suffixing a taken one,
        // so the asset may not be called what was typed. Said out loud, because
        // an author who is not told goes looking for a name nothing holds.
        const std::string assigned = resources.get(h).name;
        if (assigned != to) {
            ec.state.pushToast(EditorState::ToastKind::Info,
                               "'" + std::string(to) + "' was taken - renamed to '"
                                   + assigned + "'");
        }

        // Applied first, then the reverse pushed, so an accidental rename is
        // one Ctrl+Z away. Its after is the name assigned rather than the one
        // asked for, so redo repeats what happened.
        ec.state.commands.push(std::make_unique<RenameAssetCommand<Handle<Asset>>>(
            resources, h, from, assigned, "Rename Asset"));
    }

    static void remove(ResourceManager& resources, StorageIndex key) {
        resources.remove(Handle<Asset>{key});
    }
};

// Preview cache keys. One space per kind, and none of them 0 - that is
// reserved for the Material Editor's live pane.
uint64_t previewKey(AssetType type, uint32_t id) {
    return ((static_cast<uint64_t>(type) + 1ull) << 40) | id;
}

// ---------------------------------------------------------------------------
// Detail lines. One string per kind, carrying the fact that kind actually has,
// which is what buys back the columns a table would have spent on it.
//
// Each writes two forms. The short one is the tile's second line and has a
// tile's width to live in - about fourteen characters at the default size - so
// it carries the one fact that distinguishes assets of that kind. The verbose
// one goes in the hover tooltip and is where the rest of what a table would
// have columned goes, which is how the Sounds table's Format and Size survive
// losing their columns rather than being dropped.
// ---------------------------------------------------------------------------

void materialDetail(const ResourceManager& resources, StorageIndex key,
                    bool verbose, char* out, size_t n) {
    const MaterialAsset& m = resources.get(MaterialHandle{key});
    const char* type = "opaque";
    switch (m.type) {
        case MaterialType::Transparent: type = "transparent"; break;
        case MaterialType::Unlit:       type = "unlit";       break;
        case MaterialType::AlphaMask:   type = "alpha mask";  break;
        default: break;
    }
    if (verbose) {
        snprintf(out, n, "%s . metal %.2f . rough %.2f", type,
                 static_cast<double>(m.metallic), static_cast<double>(m.roughness));
    } else if (m.type != MaterialType::Opaque) {
        // The line says the one thing about a material its thumbnail cannot.
        // A transparent or masked material looks like an opaque one on a
        // preview sphere and behaves nothing like it, so when the render path
        // is the unusual one that is the fact worth the line; when it is the
        // ordinary one, roughness is what actually differs tile to tile.
        snprintf(out, n, "%s", type);
    } else {
        snprintf(out, n, "rough %.2f", static_cast<double>(m.roughness));
    }
}

void meshDetail(const ResourceManager& resources, StorageIndex key,
                bool verbose, char* out, size_t n) {
    const MeshAsset& mesh = resources.get(MeshHandle{key});
    const size_t tris = mesh.indices.size() / 3;
    // A mesh being skinned is why its thumbnail can look like nothing
    // recognisable: the preview draws bind-pose vertices with no rig behind
    // them. Saying so on the tile is cheaper than posing one for a picture.
    const char* skin = mesh.skin.empty() ? "" : " . skinned";
    if (verbose) {
        snprintf(out, n, "%zu tris . %zu verts%s", tris, mesh.vertices.size(), skin);
    } else {
        snprintf(out, n, "%zu tris%s", tris, skin);
    }
}

void soundDetail(const ResourceManager& resources, StorageIndex key,
                 bool verbose, char* out, size_t n) {
    const AudioClipAsset& clip = resources.get(AudioClipHandle{key});
    // Named for one and two channels, counted past that. The cooker accepts up
    // to eight, so "stereo" against a six-channel file would be this line
    // reporting a format the file does not have.
    char layout[16];
    if      (clip.channels == 1) snprintf(layout, sizeof(layout), "mono");
    else if (clip.channels == 2) snprintf(layout, sizeof(layout), "stereo");
    else                         snprintf(layout, sizeof(layout), "%u ch", clip.channels);

    if (verbose) {
        // A positioned source hears each channel on the side it was authored
        // for, so a wide clip loses half its field. Said where a clip is
        // picked; the Inspector says it again where one is put on a source.
        const char* mono = clip.channels > 1
            ? "\nEach channel sticks to one ear - positioning wants mono"
            : "";
        snprintf(out, n, "%.2fs . %s %u Hz . %.1f MB%s", static_cast<double>(clip.duration()),
                 layout, clip.sampleRate,
                 static_cast<double>(clip.sampleCount() * sizeof(int16_t)) / (1024.0 * 1024.0),
                 mono);
    } else {
        snprintf(out, n, "%.2fs . %s", static_cast<double>(clip.duration()), layout);
    }
}

void textureDetail(const ResourceManager& resources, StorageIndex key,
                   bool verbose, char* out, size_t n) {
    const TextureAsset& tex = resources.get(TextureHandle{key});
    if (tex.loading) {
        // An async import holds a handle with no pixels and no dimensions for
        // a frame or three. "0x0" would read as a broken file rather than as
        // one that has not arrived.
        snprintf(out, n, "decoding...");
        return;
    }
    if (!verbose) {
        snprintf(out, n, "%ux%u", tex.params.width, tex.params.height);
        return;
    }
    // Colour space answers a question the picture raises - why is this one flat
    // blue - rather than one an author scans a grid for, so it goes here.
    const size_t pixels = static_cast<size_t>(tex.params.width) * tex.params.height;
    const unsigned channels = pixels
        ? static_cast<unsigned>(tex.pixelData.size() / pixels)
        : 0u;
    snprintf(out, n, "%ux%u . %u ch . %s . %.1f MB",
             tex.params.width, tex.params.height, channels,
             tex.srgb ? "sRGB" : "linear",
             static_cast<double>(tex.pixelData.size()) / (1024.0 * 1024.0));
}

void skeletonDetail(const ResourceManager& resources, StorageIndex key,
                    bool verbose, char* out, size_t n) {
    const SkeletonAsset& rig = resources.get(SkeletonHandle{key});
    if (rig.bones.empty()) {
        snprintf(out, n, "no bones");
        return;
    }
    if (verbose) {
        // The root bone is what an author recognises a rig by: the importer
        // takes it from the armature node, so two rigs out of one file differ
        // there long before they differ in bone count.
        snprintf(out, n, "%zu bones . root '%s'", rig.bones.size(),
                 rig.bones.front().name.c_str());
    } else {
        snprintf(out, n, "%zu bones", rig.bones.size());
    }
}

void clipDetail(const ResourceManager& resources, StorageIndex key,
                bool verbose, char* out, size_t n) {
    const AnimationClipAsset& clip = resources.get(AnimationClipHandle{key});

    // Reported by the rig actually resolved, not by the name carried: the pose
    // system refuses a clip whose named rig is absent, and a clip that animates
    // nothing looks identical to one that works until this line says so.
    const SkeletonHandle rig = resources.findByName<SkeletonAsset>(clip.skeleton);

    size_t channels = 0;
    for (const ClipBone& bone : clip.bones) {
        channels += (bone.position.count != 0) + (bone.rotation.count != 0)
                  + (bone.scale.count != 0);
    }

    const double seconds = static_cast<double>(clip.duration);
    if (verbose) {
        char markers[32] = {};
        if (!clip.markers.empty()) {
            snprintf(markers, sizeof(markers), " . %zu markers", clip.markers.size());
        }
        if (rig) {
            snprintf(out, n, "%.2fs . %zu channels . rig '%s'%s",
                     seconds, channels, clip.skeleton.c_str(), markers);
        } else {
            snprintf(out, n, "%.2fs . %zu channels . rig '%s' is not in this project%s",
                     seconds, channels, clip.skeleton.c_str(), markers);
        }
    } else if (!rig) {
        snprintf(out, n, "%.2fs . no rig", seconds);
    } else {
        snprintf(out, n, "%.2fs . %zu ch", seconds, channels);
    }
}

// ---------------------------------------------------------------------------
// Thumbnails. Only the kinds that have a picture supply one.
// ---------------------------------------------------------------------------

GpuTextureId materialThumb(TileContext& tc, StorageIndex key, uint64_t version) {
    PreviewRequest req;
    req.key      = previewKey(AssetType::Material, MaterialHandle{key}.id());
    req.material = MaterialHandle{key};
    req.mesh     = tc.sphere;
    req.yawDeg   = 30.0f;
    req.pitchDeg = 18.0f;
    req.distance = 2.6f;
    return tc.ec.materialPreviews.texture(tc.ec.frame.resources, req, version, /*live*/ false);
}

GpuTextureId meshThumb(TileContext& tc, StorageIndex key, uint64_t version) {
    PreviewRequest req;
    req.key      = previewKey(AssetType::Mesh, MeshHandle{key}.id());
    req.material = tc.neutral;
    req.mesh     = MeshHandle{key};
    req.yawDeg   = 25.0f;
    req.pitchDeg = 15.0f;
    req.distance = 2.6f;
    return tc.ec.materialPreviews.texture(tc.ec.frame.resources, req, version, /*live*/ false);
}

GpuTextureId textureThumb(TileContext& tc, StorageIndex key, uint64_t) {
    // Nothing is rendered or copied: the tile samples the mirror the renderer
    // samples and the GPU minifies it. The version is unused because a
    // re-uploaded texture keeps its id, so there is no cached picture to drop.
    EditorRenderHooks* backend = editorRenderHooks(tc.ec.renderSystem.backend());
    if (!backend) return 0;

    const TextureHandle handle{key};
    if (const GpuTextureId resident = backend->textureId(handle)) return resident;

    // Not resident, and it never will be on its own - sync() reaches a texture
    // only through a material something draws, and a library shows the rest too.
    if (tc.uploads <= 0) return 0;
    --tc.uploads;
    return backend->ensureTexture(handle, tc.ec.frame.resources);
}

// ---------------------------------------------------------------------------
// Assignment. Each goes through pushEdit rather than writing the component,
// which is what gives it an undo step and what turns it into a prefab override
// when the entity is an instance. Writing the component here instead left the
// instance's override list empty, and the next save wrote the prefab's own
// asset back over the one on screen.
// ---------------------------------------------------------------------------

bool meshTarget(const Scene& scene, EntityId id) { return scene.has<Mesh>(id); }

void assignMaterial(EditorContext& ec, EntityId id, StorageIndex key) {
    Mesh& mesh = ec.frame.scene.get<Mesh>(id);
    const Mesh before = mesh;
    mesh.material = MaterialHandle{key};
    pushEdit<Mesh>(ec.frame.scene, ec.frame.resources, ec.state, id, before, mesh,
                   "Assign Material");
}

void assignMesh(EditorContext& ec, EntityId id, StorageIndex key) {
    Mesh& mesh = ec.frame.scene.get<Mesh>(id);
    const Mesh before = mesh;
    mesh.mesh = MeshHandle{key};
    pushEdit<Mesh>(ec.frame.scene, ec.frame.resources, ec.state, id, before, mesh,
                   "Assign Mesh");
}

// One Animator carries both halves of a rigged character - the rig and the clip
// running on it - so a skeleton and a clip look for the same component.
bool animatorTarget(const Scene& scene, EntityId id) { return scene.has<Animator>(id); }

void assignSkeleton(EditorContext& ec, EntityId id, StorageIndex key) {
    Animator& animator = ec.frame.scene.get<Animator>(id);
    const Animator before = animator;
    animator.skeleton = SkeletonHandle{key};
    pushEdit<Animator>(ec.frame.scene, ec.frame.resources, ec.state, id, before, animator,
                       "Assign Skeleton");
}

void assignClip(EditorContext& ec, EntityId id, StorageIndex key) {
    Animator& animator = ec.frame.scene.get<Animator>(id);
    const Animator before = animator;
    animator.clip = AnimationClipHandle{key};
    // Cut rather than crossFadeTo: an authoring assignment answers "which clip
    // does this character play", and a blend started from the editor would run
    // down against a simulation clock the editor is not advancing.
    animator.time = 0.0f;
    pushEdit<Animator>(ec.frame.scene, ec.frame.resources, ec.state, id, before, animator,
                       "Assign Clip");
}

bool audioTarget(const Scene& scene, EntityId id) { return scene.has<AudioSource>(id); }

void assignSound(EditorContext& ec, EntityId id, StorageIndex key) {
    AudioSource& source = ec.frame.scene.get<AudioSource>(id);
    const AudioSource before = source;
    source.clip = AudioClipHandle{key};
    pushEdit<AudioSource>(ec.frame.scene, ec.frame.resources, ec.state, id, before, source,
                          "Assign Sound");
}

// ---------------------------------------------------------------------------
// Usage walks: does anything in the project hold a reference to this asset.
// The scene is not the whole project - a texture is named by a material and
// never by an entity - so they take the resources too, and each walks every
// holder rather than the obvious one (see docs/reference/editor.md).
// ---------------------------------------------------------------------------

// The serializer, the GL material and the Material Editor each pair these
// eleven members with something of their own; none of those pairings fits here.
constexpr TextureHandle MaterialAsset::* MATERIAL_TEXTURE_SLOTS[] = {
    &MaterialAsset::albedoTexture,
    &MaterialAsset::normalTexture,
    &MaterialAsset::metallicRoughnessTexture,
    &MaterialAsset::metallicTexture,
    &MaterialAsset::roughnessTexture,
    &MaterialAsset::aoTexture,
    &MaterialAsset::aoMetallicRoughnessTexture,
    &MaterialAsset::emissionTexture,
    &MaterialAsset::heightTexture,
    &MaterialAsset::clearcoatTexture,
    &MaterialAsset::transmissionTexture,
};

void materialsInUse(const Scene& scene, const ResourceManager&,
                    std::unordered_set<uint32_t>& used) {
    scene.forEach<Mesh>([&](EntityId, const Mesh& m) {
        if (m.material) used.insert(m.material.id());
    });
    scene.forEach<Decal>([&](EntityId, const Decal& d) {
        if (d.material) used.insert(d.material.id());
    });
}

void meshesInUse(const Scene& scene, const ResourceManager&,
                 std::unordered_set<uint32_t>& used) {
    scene.forEach<Mesh>([&](EntityId, const Mesh& m) {
        if (m.mesh) used.insert(m.mesh.id());
    });
    scene.forEach<LOD>([&](EntityId, const LOD& l) {
        for (const LODLevel& level : l.levels) {
            if (level.mesh) used.insert(level.mesh.id());
        }
    });
}

void texturesInUse(const Scene&, const ResourceManager& resources,
                   std::unordered_set<uint32_t>& used) {
    // Every material, not only the ones the scene draws: a texture bound by a
    // material nothing has placed yet still has an owner to break.
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle, const MaterialAsset& m) {
        for (TextureHandle MaterialAsset::* slot : MATERIAL_TEXTURE_SLOTS) {
            if (m.*slot) used.insert((m.*slot).id());
        }
    });
}

void skeletonsInUse(const Scene& scene, const ResourceManager& resources,
                    std::unordered_set<uint32_t>& used) {
    scene.forEach<Animator>([&](EntityId, const Animator& a) {
        if (a.skeleton) used.insert(a.skeleton.id());
    });
    // Meshes and clips name their rig as a string, so the reference is
    // invisible to a handle walk and has to be resolved back through the name.
    const auto claimByName = [&](const std::string& name) {
        if (const SkeletonHandle rig = resources.findByName<SkeletonAsset>(name)) {
            used.insert(rig.id());
        }
    };
    resources.forEachOfType<MeshAsset>([&](MeshHandle, const MeshAsset& m) {
        if (!m.skin.empty()) claimByName(m.skeleton);
    });
    resources.forEachOfType<AnimationClipAsset>([&](AnimationClipHandle, const AnimationClipAsset& c) {
        claimByName(c.skeleton);
    });
}

void clipsInUse(const Scene& scene, const ResourceManager&,
                std::unordered_set<uint32_t>& used) {
    scene.forEach<Animator>([&](EntityId, const Animator& a) {
        if (a.clip)     used.insert(a.clip.id());
        // The clip a crossfade is leaving is still being sampled, and is still
        // on screen for as long as the blend runs.
        if (a.fadeFrom) used.insert(a.fadeFrom.id());
    });
}

void soundsInUse(const Scene& scene, const ResourceManager&,
                 std::unordered_set<uint32_t>& used) {
    scene.forEach<AudioSource>([&](EntityId, const AudioSource& s) {
        if (s.clip) used.insert(s.clip.id());
    });
}

// ---------------------------------------------------------------------------
// The table. Adding a kind is this entry plus its handful of small functions.
// Rail order pairs the kinds that are about each other, and each wears a
// registered hue no neighbour is close to; docs/reference/editor.md has the
// reasoning for both, including why Accent::MatTexture cannot serve Textures.
// ---------------------------------------------------------------------------

const AssetKind KINDS[] = {
    {
        AssetType::Material, "Materials",
        "No materials yet. New makes a blank one.",
        EditorIcon::Material, &EditorStyle::Accent::MatBase,
        "New", "Create a blank material and open it in the Material Editor",
        Verb::NewMaterial,
        "Assign to selected entity", "(select a mesh entity to assign)",
        nullptr,
        &KindOps<MaterialAsset>::rows, &materialDetail, &materialThumb,
        &meshTarget, &assignMaterial, &materialsInUse,
        &KindOps<MaterialAsset>::rename, &KindOps<MaterialAsset>::remove,
    },
    {
        AssetType::Texture, "Textures",
        "No textures yet. Import brings an image in.",
        EditorIcon::Texture, &EditorStyle::Accent::MatSurface,
        "Import...", "Import an image as colour - PNG, JPG, TGA or BMP",
        Verb::ImportTexture,
        // No assign item: a texture goes into one of a material's eleven slots,
        // and which slot is a question no entity can answer.
        nullptr, nullptr,
        nullptr,
        &KindOps<TextureAsset>::rows, &textureDetail, &textureThumb,
        nullptr, nullptr, &texturesInUse,
        &KindOps<TextureAsset>::rename, &KindOps<TextureAsset>::remove,
    },
    {
        AssetType::Mesh, "Meshes",
        "No meshes yet. Import brings some in.",
        EditorIcon::Mesh, &EditorStyle::Accent::Mesh,
        "Import...", "Import a model - glTF, GLB, OBJ, FBX, DAE, STL, PLY or 3DS",
        Verb::ImportModel,
        "Assign to selected entity", "(select a mesh entity to assign)",
        nullptr,
        &KindOps<MeshAsset>::rows, &meshDetail, &meshThumb,
        &meshTarget, &assignMesh, &meshesInUse,
        &KindOps<MeshAsset>::rename, &KindOps<MeshAsset>::remove,
    },
    {
        AssetType::Skeleton, "Skeletons",
        "No rigs yet. Import a rigged model to get one.",
        EditorIcon::Skeleton, &EditorStyle::Accent::Transform,
        "Import...", "Import a rigged model - glTF, GLB, FBX, DAE or 3DS",
        Verb::ImportModel,
        "Assign to selected Animator", "(select an entity with an Animator)",
        // A skinned mesh and a clip each carry the rig's name as a string, and
        // the pose system throws out a clip whose name stops matching.
        "(a mesh and a clip name their rig - a rename would unbind them)",
        &KindOps<SkeletonAsset>::rows, &skeletonDetail, nullptr,
        &animatorTarget, &assignSkeleton, &skeletonsInUse,
        nullptr, &KindOps<SkeletonAsset>::remove,
    },
    {
        AssetType::AnimationClip, "Clips",
        "No animation clips yet. Import an animated model to get some.",
        EditorIcon::Anim, &EditorStyle::Accent::Anim,
        "Import...", "Import an animated model - glTF, GLB, FBX, DAE or 3DS",
        Verb::ImportModel,
        "Assign to selected Animator", "(select an entity with an Animator)",
        nullptr,
        &KindOps<AnimationClipAsset>::rows, &clipDetail, nullptr,
        &animatorTarget, &assignClip, &clipsInUse,
        &KindOps<AnimationClipAsset>::rename, &KindOps<AnimationClipAsset>::remove,
    },
    {
        AssetType::AudioClip, "Sounds",
        "No sounds yet. Import decodes a wav / mp3 / flac.",
        EditorIcon::Audio, &EditorStyle::Accent::Audio,
        "Import...", "Import a sound - WAV, MP3 or FLAC",
        Verb::ImportSound,
        "Assign to selected Audio Source", "(select an entity with an Audio Source)",
        nullptr,
        &KindOps<AudioClipAsset>::rows, &soundDetail, nullptr,
        &audioTarget, &assignSound, &soundsInUse,
        &KindOps<AudioClipAsset>::rename, &KindOps<AudioClipAsset>::remove,
    },
};

const AssetKind& kindOf(AssetType type) {
    for (const AssetKind& k : KINDS) {
        if (k.type == type) return k;
    }
    return KINDS[0];
}

// ---------------------------------------------------------------------------
// Tile text.
// ---------------------------------------------------------------------------

// Byte offsets of the next / previous character. Never land inside a UTF-8
// sequence: a lone continuation byte renders as the font's replacement box.
size_t utf8Next(const char* s, size_t i, size_t len) {
    for (++i; i < len && (s[i] & 0xC0) == 0x80; ++i) {}
    return i;
}

size_t utf8Prev(const char* s, size_t i) {
    for (--i; i > 0 && (s[i] & 0xC0) == 0x80; --i) {}
    return i;
}

/// Width of a line keeping [0, head) and [tail, len) with the ellipsis between.
float keptWidth(const char* s, size_t head, size_t tail, size_t len) {
    return ImGui::CalcTextSize(s, s + head).x + ImGui::CalcTextSize(s + tail, s + len).x;
}

/**
 * @brief Draw one line of text clipped to a width, ellipsised in the middle.
 *
 * A wrapped name is what broke the old grid's alignment: a two-line name
 * pushed the next row of tiles off the baseline, so every tile after a long
 * name sat wrong. One line always, and the full text stays available on hover.
 *
 * The cut lands mid-line because these lines share their starts and differ at
 * their ends - a clip named by its path, the sixtieth material out of one file
 * - and a tail cut left a row of tiles all reading "assets/audio/to...".
 *
 * @param text Line to draw; empty draws the placeholder.
 * @param maxWidth Width the line must fit inside, in pixels.
 * @param dim Whether to draw in the disabled colour (the detail line does).
 */
void clippedLine(const char* text, float maxWidth, bool dim) {
    const char* str = (text && text[0]) ? text : "(unnamed)";
    char buf[192];
    if (ImGui::CalcTextSize(str).x > maxWidth) {
        const size_t len    = std::strlen(str);
        const float  budget = maxWidth - ImGui::CalcTextSize("...").x;

        // Grown one character in from each end in turn, so the two halves stay
        // the same length whichever end the wide characters are at.
        size_t head = 0;
        size_t tail = len;
        for (;;) {
            const size_t grownHead = utf8Next(str, head, len);
            if (grownHead >= tail || keptWidth(str, grownHead, tail, len) > budget) break;
            head = grownHead;

            const size_t grownTail = utf8Prev(str, tail);
            if (grownTail <= head || keptWidth(str, head, grownTail, len) > budget) break;
            tail = grownTail;
        }
        snprintf(buf, sizeof(buf), "%.*s...%s", static_cast<int>(head), str, str + tail);
        str = buf;
    }
    if (dim) ImGui::TextDisabled("%s", str);
    else     ImGui::TextUnformatted(str);
}

}  // namespace

void AssetBrowserPanel::openRename(AssetType kind, StorageIndex key, const std::string& name) {
    snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", name.c_str());
    m_rename = AssetTarget{kind, key, name, true};
}

void AssetBrowserPanel::ensureAssets(ResourceManager& resources) {
    // Re-acquire every call rather than caching with a "ready" flag:
    // SceneSerializer::load swaps the ResourceManager wholesale, so any
    // cached handle survives the swap as a dangling (index, generation)
    // pair into the now-discarded manager. findByName is O(1) (per-type
    // name index in ResourceManager), so the cost is negligible.
    m_sphere = resources.findByName<MeshAsset>("mesh:preview_sphere");
    if (!m_sphere) m_sphere = resources.addPrivate(generateSphere(), "mesh:preview_sphere");

    m_neutral = resources.findByName<MaterialAsset>("mat:thumb_neutral");
    if (!m_neutral) {
        MaterialAsset m;
        m.albedo    = glm::vec4(0.78f, 0.78f, 0.80f, 1.0f);
        m.metallic  = 0.0f;
        m.roughness = 0.55f;
        m_neutral = resources.addPrivate(std::move(m), "mat:thumb_neutral");
    }
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

    // Raised at panel scope rather than inside the grid child: OpenPopup
    // hashes its id against the window it is called from, so a modal opened
    // from the child and begun out here would never match.
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

    for (const AssetKind& kind : KINDS) {
        // Counted through the same filter the grid draws with, so searching
        // narrows the rail alongside it and a kind that has no match says 0
        // rather than offering an empty grid to walk into.
        int count = 0;
        kind.rows(ec.frame.resources, [&](const AssetRow& row) {
            if (matchesFilter(row.name->c_str(), m_filter)) ++count;
        });

        ImGui::PushID(static_cast<int>(kind.type));

        // The row wears its own kind's hue, not the editor's one accent: six
        // rows highlighted in the same blue read as one list, and the strip
        // beside them is three pixels wide and cannot carry the difference
        // alone. Alpha, so the hue stays the kind's registered one.
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

        // The same left accent strip a component card carries, and the same
        // one the tiles below wear: the eye already groups by it here, so the
        // rail row and its grid are tied together without a second device.
        const ImU32 accent = ImGui::GetColorU32(*kind.accent);
        dl->AddRectFilled(ImVec2(mn.x, mn.y + 1.0f), ImVec2(mn.x + stripe, mx.y - 1.0f),
                          selected ? accent : (accent & 0x60FFFFFF));

        char buf[16];
        snprintf(buf, sizeof(buf), "%d", count);
        const float textW = ImGui::CalcTextSize(buf).x;
        dl->AddText(ImVec2(mx.x - textW - EditorStyle::px(6.0f),
                           mn.y + (mx.y - mn.y - ImGui::GetTextLineHeight()) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), buf);
        ImGui::PopID();
    }

    ImGui::EndChild();
}

void AssetBrowserPanel::drawToolbar(EditorContext& ec) {
    const AssetKind& kind = kindOf(m_kind);

    // The verb slot: the first control is always the chosen kind's primary
    // action, at the same place whatever kind that is. The label is the verb
    // alone - the rail two inches to its left already says which kind is
    // showing, so "Import Sound..." spent half a button saying it twice. What
    // the noun did carry, the file formats behind an import, is a tooltip's
    // job and now reads fuller than the noun ever did.
    if (ImGui::Button(kind.verb)) {
        switch (kind.verbAction) {
            case Verb::NewMaterial:
                if (MaterialHandle h = EditorActions::createNewMaterial(ec.frame.resources,
                                                                        ec.state)) {
                    ec.state.materialEditorTarget = h;
                    ec.state.showMaterialEditor   = true;
                }
                break;
            case Verb::ImportModel:   ec.state.requestModelImport = true; break;
            case Verb::ImportTexture: m_requestTextureImport     = true; break;
            case Verb::ImportSound:   m_requestSoundImport       = true; break;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kind.verbHint);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(EditorStyle::px(200.0f));
    // Escape empties the box rather than ImGui's default of reverting it to
    // what it held on focus, which on a search field puts back the needle the
    // author is trying to drop.
    ImGui::InputTextWithHint("##assetFilter", "Search...", m_filter, sizeof(m_filter),
                             ImGuiInputTextFlags_EscapeClearsAll);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(EditorStyle::px(130.0f));
    ImGui::SliderFloat("##cell", &m_cell, 64.0f, 200.0f, "%.0f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tile size");

    // Both ways a clip can be inaudible with nothing on this kind wrong, said
    // where the kind that can be heard is showing. A muted mix is the quieter
    // of the two: a tile shows a Pause and a cursor running against the clip's
    // length, so the audition looks exactly like one that is working.
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
    const AssetKind& kind      = kindOf(m_kind);
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;
    Scene&           scene     = ec.frame.scene;

    const EntityId sel = state.selectedEntity;
    const bool canAssign = kind.assign && sel && scene.isAlive(sel) && kind.target(scene, sel);

    std::unordered_set<uint32_t> used;
    kind.used(scene, resources, used);

    TileContext tc{ec, m_sphere, m_neutral, TEXTURE_UPLOADS_PER_FRAME};
    AudioDevice& device = ec.audioSystem.device();

    // The face is the tile: one square, the same square whatever fills it, and
    // the width the name and detail lines are clipped to.
    const float face = EditorStyle::px(m_cell);
    const float step = face + ImGui::GetStyle().ItemSpacing.x;
    const int   cols = (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / step));
    const float stripe = EditorStyle::px(3.0f);

    // The tile face does not answer the pointer; the hover border does. The
    // theme's button accent cannot - flat blue over a glyph, hidden entirely
    // under a thumbnail - so the two faces would answer one gesture apart.
    const ImVec4 inert = ImGui::GetStyleColorVec4(ImGuiCol_Button);

    int shown = 0;

    kind.rows(resources, [&](const AssetRow& row) {
        if (!matchesFilter(row.name->c_str(), m_filter)) return;

        const uint32_t     id  = row.key.index;
        const GpuTextureId tex = kind.thumb ? kind.thumb(tc, row.key, row.version) : 0u;
        const bool         orphan = used.count(id) == 0;

        ImGui::PushID(static_cast<int>(id));
        ImGui::BeginGroup();

        // The face. A thumbnail where the kind has one, the kind's glyph on
        // the same square where it does not - one tile shape either way, which
        // is what lets a sound sit beside a mesh instead of needing a tab.
        const ImVec2 faceMin = ImGui::GetCursorScreenPos();
        bool clicked = false;
        // The face spans the whole square, so anything drawn over it later -
        // the sound transport below - would be unreachable: the first item
        // submitted holds the mouse over an overlap unless it says otherwise.
        ImGui::SetNextItemAllowOverlap();
        // No frame around the face. An ImageButton insets its picture by
        // FramePadding and a sized Button does not, so on the theme's (8, 4)
        // a thumbnail tile stood eight pixels shorter than a glyph tile and no
        // two kinds' name lines could share a baseline - visible inside one
        // kind too, the moment a thumbnail had not had its bake turn. It also
        // started the picture eight pixels right of the name underneath it.
        // Zeroed, both paths submit the same square, and that square's left
        // edge is the one the name and the detail line start from.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, inert);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, inert);
        if (tex) {
            clicked = ImGui::ImageButton("##face", imTexture(tex), ImVec2(face, face),
                                         ImVec2(0, 1), ImVec2(1, 0));
        } else {
            clicked = ImGui::Button("##face", ImVec2(face, face));
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        const ImVec2 faceMax = ImGui::GetItemRectMax();
        if (ImGui::IsItemHovered()) {
            // One hover treatment for both faces, in the kind's own hue.
            ImGui::GetWindowDrawList()->AddRect(faceMin, faceMax,
                                                ImGui::GetColorU32(*kind.accent),
                                                0.0f, 0, EditorStyle::px(2.0f));
            char full[256];
            kind.detail(resources, row.key, /*verbose*/ true, full, sizeof(full));
            ImGui::SetTooltip("%s\n%s%s", row.name->empty() ? "(unnamed)" : row.name->c_str(),
                              full, orphan ? "\nNothing in this project uses it" : "");
            // The gesture the Hierarchy binds on the row it points at. Delete
            // is not beside it: that key already destroys the selected entity
            // and is read before any panel draws, so both would fire.
            if (kind.rename && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                openRename(kind.type, row.key, *row.name);
            }
        }

        if (ImGui::BeginPopupContextItem("##tilectx")) {
            // The menu covers the tiles either side of the one it belongs to,
            // and a grid of one kind is a row of near-identical squares, so it
            // says which asset it is about before offering to destroy one.
            sectionLabel(row.name->empty() ? "(unnamed)" : row.name->c_str());
            ImGui::Separator();
            if (kind.type == AssetType::Material && ImGui::MenuItem("Open in Material Editor")) {
                state.materialEditorTarget = MaterialHandle{row.key};
                state.showMaterialEditor   = true;
            }
            if (kind.assignLabel) {
                ImGui::BeginDisabled(!canAssign);
                if (ImGui::MenuItem(kind.assignLabel)) kind.assign(ec, sel, row.key);
                ImGui::EndDisabled();
                if (!canAssign) ImGui::TextDisabled("%s", kind.assignHint);
            }
            ImGui::Separator();
            ImGui::BeginDisabled(!kind.rename);
            if (ImGui::MenuItem("Rename...")) openRename(kind.type, row.key, *row.name);
            ImGui::EndDisabled();
            if (!kind.rename) ImGui::TextDisabled("%s", kind.noRename);
            ImGui::BeginDisabled(!orphan);
            if (ImGui::MenuItem("Delete...")) {
                m_delete = AssetTarget{kind.type, row.key, *row.name, true};
            }
            ImGui::EndDisabled();
            if (!orphan) ImGui::TextDisabled("(in use - clear the references first)");
            ImGui::EndPopup();
        }

        if (!tex) {
            // A kind with no thumbnail wears its glyph; a kind that has one but
            // has not had its bake turn yet wears the same glyph faintly, so
            // "there is no picture for this" and "the picture is coming" do not
            // look alike.
            ImVec4 glyph = *kind.accent;
            if (kind.thumb) glyph.w = 0.30f;
            drawEditorIcon(ImGui::GetWindowDrawList(), kind.icon,
                           ImVec2((faceMin.x + faceMax.x) * 0.5f, (faceMin.y + faceMax.y) * 0.5f),
                           face * 0.22f, ImGui::GetColorU32(glyph));
        }

        // Auditioning is what previewing a sound means, so the transport sits
        // on the face the way a play control sits on a video thumbnail - the
        // tile keeps every other kind's height and gains no row of its own.
        const bool mine = kind.type == AssetType::AudioClip
                       && m_previewClip == AudioClipHandle{row.key};
        if (kind.type == AssetType::AudioClip) {
            const float ih  = ImGui::GetFrameHeight();
            const float pad = EditorStyle::px(5.0f);
            const ImVec2 back = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(faceMin.x + pad, faceMax.y - ih - pad));
            const AudioClipAsset& clip = resources.get(AudioClipHandle{row.key});
            if (auditionTransport("abSnd", device, m_previewVoice, mine, &clip, ih))
                m_previewClip = AudioClipHandle{row.key};
            ImGui::SetCursorScreenPos(back);
        }

        if (kind.type == AssetType::Material && clicked) {   // left-click = edit
            state.materialEditorTarget = MaterialHandle{row.key};
            state.showMaterialEditor   = true;
        }

        clippedLine(row.name->c_str(), face, /*dim*/ false);

        // The detail line, until this tile is the one being heard: then the
        // same line says how far into that length the audition has got, and
        // moves it. A position measured against a length belongs where the
        // length was stated.
        if (mine && device.isVoiceActive(m_previewVoice)) {
            auditionScrubber("abPos", device, m_previewVoice,
                             resources.get(AudioClipHandle{row.key}).duration(), face);
        } else {
            char detail[96];
            kind.detail(resources, row.key, /*verbose*/ false, detail, sizeof(detail));
            clippedLine(detail, face, /*dim*/ true);
        }

        ImGui::EndGroup();

        // Drawn after the group so it lies over the face's left edge, not
        // under it. How solid it is answers the one thing a library is asked
        // about its rows - is anything using this - which was otherwise legible
        // only as a greyed-out Delete.
        ImVec4 strip = *kind.accent;
        if (orphan) strip.w *= 0.35f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(faceMin.x, faceMin.y), ImVec2(faceMin.x + stripe, faceMax.y),
            ImGui::GetColorU32(strip));

        ImGui::PopID();

        if (++shown % cols != 0) ImGui::SameLine();
    });

    if (shown == 0) {
        ImGui::TextDisabled("%s", m_filter[0] ? "Nothing matches the search." : kind.empty);
    }
}

void AssetBrowserPanel::serviceTextureImport(EditorContext& ec) {
    if (m_requestTextureImport) {
        m_texturePicker.options.popupId    = "Import Texture";
        m_texturePicker.options.title      = "Import Texture";
        m_texturePicker.options.root       = ProjectPaths::assets();
        m_texturePicker.options.recursive  = true;
        m_texturePicker.options.kind       = AssetPicker::Kind::Files;
        m_texturePicker.options.extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
        m_texturePicker.options.maxResults = 4000;
        m_texturePicker.options.relativeTo = ProjectPaths::projectRoot();
        m_texturePicker.options.hint       = "PNG / JPG / TGA / BMP, read as colour (sRGB)";
        m_texturePicker.open();
        m_requestTextureImport = false;
    }

    std::string picked;
    if (!m_texturePicker.draw(picked)) return;

    // Asked before the load: loadTexture decodes and adds without looking, and
    // names are kept unique, so a repeat import would leave two assets for one
    // file. The three outcomes read apart the way the sound import's do.
    ResourceManager& resources = ec.frame.resources;
    const std::string ref = ProjectPaths::toProjectRelative(picked);
    if (resources.findByName<TextureAsset>(ref)) {
        ec.state.pushToast(EditorState::ToastKind::Info, ref + " is already imported");
        return;
    }

    // Read as colour: a texture picked by hand off a picker is art. Data maps
    // arrive with their model, or through the Material Editor slot that knows
    // which of the eleven it fills and passes the colour space for it.
    if (!loadTexture(picked, resources, /*srgb*/ true)) {
        ec.state.pushToast(EditorState::ToastKind::Error, "Could not decode " + picked);
    } else {
        ec.state.markSceneDirty();
    }
}

void AssetBrowserPanel::serviceSoundImport(EditorContext& ec) {
    if (m_requestSoundImport) {
        m_soundPicker.options.popupId    = "Import Sound";
        m_soundPicker.options.title      = "Import Sound";
        m_soundPicker.options.root       = ProjectPaths::assets();
        m_soundPicker.options.recursive  = true;
        m_soundPicker.options.kind       = AssetPicker::Kind::Files;
        m_soundPicker.options.extensions = {".wav", ".mp3", ".flac"};
        m_soundPicker.options.maxResults = 2000;
        m_soundPicker.options.relativeTo = ProjectPaths::projectRoot();
        m_soundPicker.options.hint       = "WAV / MP3 / FLAC";
        m_soundPicker.open();
        m_requestSoundImport = false;
    }

    std::string picked;
    if (!m_soundPicker.draw(picked)) return;

    // Asked before the import, because loadAudioClip answers a name it already
    // holds with the clip it already has and decodes nothing. The three
    // outcomes then read apart: a file that would not decode, one that is
    // already here, and a clip that is new. Picking a file and being told
    // nothing at all is how the second one looked, and it was the one that
    // also dirtied the scene - an unsaved-changes prompt for an import that
    // did not happen is the prompt meaning less.
    ResourceManager& resources = ec.frame.resources;
    const std::string ref = ProjectPaths::toProjectRelative(picked);
    const bool alreadyHeld = static_cast<bool>(resources.findByName<AudioClipAsset>(ref));

    if (!loadAudioClip(picked, resources)) {
        ec.state.pushToast(EditorState::ToastKind::Error, "Could not decode " + picked);
    } else if (alreadyHeld) {
        ec.state.pushToast(EditorState::ToastKind::Info, ref + " is already imported");
    } else {
        ec.state.markSceneDirty();
    }
}

void AssetBrowserPanel::drawRenameModal(EditorContext& ec) {
    if (!beginDialog("Rename Asset", m_rename.open)) return;

    // The field takes the keyboard the frame the dialog appears, with the old
    // name selected: a rename is typing, and reaching for the mouse to start
    // it is the whole gesture spent twice.
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(EditorStyle::px(280.0f));
    const bool commit = ImGui::InputText("##rnbuf", m_renameBuf, sizeof(m_renameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue
                                        | ImGuiInputTextFlags_AutoSelectAll);

    const DialogResult r = dialogButtons(m_rename.open, "Rename",
                                         m_renameBuf[0] != '\0', commit);
    if (r == DialogResult::Confirm) {
        const AssetKind& kind = kindOf(m_rename.kind);
        if (m_rename.key && kind.rename) {
            kind.rename(ec, m_rename.key, m_rename.name, m_renameBuf);
            ec.state.markSceneDirty();
        }
    }
    if (r != DialogResult::None) m_rename.key = {};
    endDialog();
}

void AssetBrowserPanel::drawDeleteModal(EditorContext& ec) {
    if (!beginDialog("Delete Asset", m_delete.open)) return;

    ImGui::Text("Delete '%s'?", m_delete.name.c_str());
    // Asked rather than done, and this is the sentence that earns the question:
    // an asset comes back only as a new slot, so undo cannot put this one back
    // where everything that named it would find it.
    ImGui::TextDisabled("Undo cannot bring it back.");

    const DialogResult r = dialogButtons(m_delete.open, "Delete");
    if (r == DialogResult::Confirm && m_delete.key) {
        // The audition outlives its tile - AudioSystem's voice holds the samples
        // by shared_ptr - so a clip removed mid-play would keep sounding with no
        // row left anywhere to stop it.
        if (m_delete.kind == AssetType::AudioClip
                && m_previewClip == AudioClipHandle{m_delete.key}) {
            ec.audioSystem.device().stopVoice(m_previewVoice);
            m_previewVoice = 0;
            m_previewClip  = {};
        }
        ec.materialPreviews.evict(previewKey(m_delete.kind, m_delete.key.index));
        kindOf(m_delete.kind).remove(ec.frame.resources, m_delete.key);
        ec.state.markSceneDirty();
    }
    if (r != DialogResult::None) m_delete.key = {};
    endDialog();
}

} // namespace Vkm::Engine
