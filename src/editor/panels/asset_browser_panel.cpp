#include "panels/asset_browser_panel.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>

#include "ecs/component/animation/animator.h"
#include "ecs/component/audio/audio_source.h"
#include "system/render/editor_render_hooks.h"
#include "core/system.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"
#include "ui/editor_icons.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"
#include "editor_actions.h"
#include "command/component_edit.h"
#include "command/editor_commands.h"
#include "panels/material_editor_panel.h"
#include "session/material_preview_session.h"
#include "system/audio/audio_system.h"
#include "system/render/render_system.h"
#include "import/audio_loaders.h"
#include "import/texture_loaders.h"
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
    StorageIndex       key;
    const std::string* name;
    uint64_t           version;
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
enum class Verb { NewMaterial, ImportModel, ImportTexture, ImportSound };

using RowsFn   = void (*)(const ResourceManager&, const RowSink&);
using DetailFn = void (*)(const ResourceManager&, StorageIndex, bool, char*, size_t);
using ThumbFn  = GpuTextureId (*)(TileContext&, StorageIndex, uint64_t);
using TargetFn = bool (*)(const Scene&, EntityId);
using AssignFn = void (*)(EditorContext&, EntityId, StorageIndex);
using UsedFn   = void (*)(const EntityRefs&, const Scene&, const ResourceManager&, UsedSlots&);
using RenameFn = void (*)(EditorContext&, StorageIndex, const std::string&, const char*);
using RemoveFn = void (*)(ResourceManager&, StorageIndex);

/**
 * @brief Everything the browser needs to know about one asset kind.
 *
 * rows, detail, used and remove are never null.
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

template<typename Asset>
struct KindOps {
    static void rows(const ResourceManager& resources, const RowSink& sink) {
        resources.forEachOfType<Asset>([&](Handle<Asset> h, const Asset& a) {
            if (a.isHidden()) return;  // editor helpers / preview primitives are not user-facing
            sink(AssetRow{h.key, &a.name(), a.version()});
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
};

// One key space per kind, never 0: MaterialEditorPanel::drawPreview's live pane key.
uint64_t previewKey(AssetType type, uint32_t id) {
    return ((static_cast<uint64_t>(type) + 1ull) << 40) | id;
}

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
    req.key      = previewKey(AssetType::Material, MaterialHandle{key}.id());
    req.material = MaterialHandle{key};
    req.mesh     = tc.sphere;
    req.yawDeg   = 30.0f;
    req.pitchDeg = 18.0f;
    req.distance = 2.6f;
    const bool live = false;
    return tc.ec.materialPreviews.texture(tc.ec.frame.resources, req, version, live);
}

GpuTextureId meshThumb(TileContext& tc, StorageIndex key, uint64_t version) {
    PreviewRequest req;
    req.key      = previewKey(AssetType::Mesh, MeshHandle{key}.id());
    req.material = tc.neutral;
    req.mesh     = MeshHandle{key};
    req.yawDeg   = 25.0f;
    req.pitchDeg = 15.0f;
    req.distance = 2.6f;
    const bool live = false;
    return tc.ec.materialPreviews.texture(tc.ec.frame.resources, req, version, live);
}

GpuTextureId textureThumb(TileContext& tc, StorageIndex key, uint64_t) {
    // The tile samples the renderer's mirror; nothing is copied. A re-upload keeps its id,
    // so the version is unused.
    EditorRenderHooks* backend = editorRenderHooks(tc.ec.renderSystem.backend());
    if (!backend) return 0;

    const TextureHandle handle{key};
    if (const GpuTextureId resident = backend->textureId(handle)) return resident;

    // Not resident, and never will be on its own; see EditorRenderHooks::textureId.
    if (tc.uploads <= 0) return 0;
    --tc.uploads;
    return backend->ensureTexture(handle, tc.ec.frame.resources);
}

// Assignment, through an EditScope: an undo step, and a prefab override on an instance.

bool meshTarget(const Scene& scene, EntityId id) { return scene.has<Mesh>(id); }

void assignMaterial(EditorContext& ec, EntityId id, StorageIndex key) {
    EditScope<Mesh> mesh(ec.frame.scene, ec.frame.resources, ec.state, id, "Assign Material");
    mesh->material = MaterialHandle{key};
}

void assignMesh(EditorContext& ec, EntityId id, StorageIndex key) {
    EditScope<Mesh> mesh(ec.frame.scene, ec.frame.resources, ec.state, id, "Assign Mesh");
    mesh->mesh = MeshHandle{key};
}

// One Animator carries both the rig and its clip.
bool animatorTarget(const Scene& scene, EntityId id) { return scene.has<Animator>(id); }

void assignSkeleton(EditorContext& ec, EntityId id, StorageIndex key) {
    EditScope<Animator> animator(ec.frame.scene, ec.frame.resources, ec.state, id, "Assign Skeleton");
    animator->skeleton = SkeletonHandle{key};
}

void assignClip(EditorContext& ec, EntityId id, StorageIndex key) {
    EditScope<Animator> animator(ec.frame.scene, ec.frame.resources, ec.state, id, "Assign Clip");
    animator->clip = AnimationClipHandle{key};
    // Cut, not crossFadeTo: the editor does not advance the clock a blend runs on.
    animator->time = 0.0f;
}

bool audioTarget(const Scene& scene, EntityId id) { return scene.has<AudioSource>(id); }

void assignSound(EditorContext& ec, EntityId id, StorageIndex key) {
    EditScope<AudioSource> source(ec.frame.scene, ec.frame.resources, ec.state, id, "Assign Sound");
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

void materialsInUse(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.materials, refs, resources, used);
}

void meshesInUse(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.meshes, refs, resources, used);
}

void texturesInUse(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.textures, refs, resources, used);
    // Every material, not only drawn ones: an unplaced material still owns its textures.
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle, const MaterialAsset& m) {
        for (TextureHandle MaterialAsset::* slot : MATERIAL_TEXTURE_SLOTS) {
            if (m.*slot) used.mark((m.*slot).id());
        }
    });
}

void skeletonsInUse(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.skeletons, refs, resources, used);
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
    markReferenced(refs.handles.clips, refs, resources, used);
    // A crossfade's outgoing clip is unsaved session state but still sampled.
    scene.forEach<Animator>([&](EntityId, const Animator& a) {
        if (a.fadeFrom) used.mark(a.fadeFrom.id());
    });
}

void soundsInUse(const EntityRefs& refs, const Scene&, const ResourceManager& resources, UsedSlots& used) {
    markReferenced(refs.handles.sounds, refs, resources, used);
}

// A kind is a builder below plus its place in KINDS; an omitted field is null (no picture,
// assign or rename). Rail order and hues: docs/reference/editor.md.
AssetKind materialKind() {
    AssetKind kind{};
    kind.type        = AssetType::Material;
    kind.label       = "Materials";
    kind.empty       = "No materials yet. New makes a blank one.";
    kind.icon        = EditorIcon::Material;
    kind.accent      = &EditorStyle::Accent::MAT_BASE;
    kind.verb        = "New";
    kind.verbHint    = "Create a blank material and open it in the Material tab";
    kind.verbAction  = Verb::NewMaterial;
    kind.assignLabel = "Assign to selected entity";
    kind.assignHint  = "(select a mesh entity to assign)";
    kind.rows        = &KindOps<MaterialAsset>::rows;
    kind.detail      = &materialDetail;
    kind.thumb       = &materialThumb;
    kind.target      = &meshTarget;
    kind.assign      = &assignMaterial;
    kind.used        = &materialsInUse;
    kind.rename      = &KindOps<MaterialAsset>::rename;
    kind.remove      = &KindOps<MaterialAsset>::remove;
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
    kind.verbHint   = "Import an image as colour - PNG, JPG, TGA or BMP";
    kind.verbAction = Verb::ImportTexture;
    // No assign item: which material slot is not an entity's question.
    kind.rows       = &KindOps<TextureAsset>::rows;
    kind.detail     = &textureDetail;
    kind.thumb      = &textureThumb;
    kind.used       = &texturesInUse;
    kind.rename     = &KindOps<TextureAsset>::rename;
    kind.remove     = &KindOps<TextureAsset>::remove;
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
    kind.rename      = &KindOps<MeshAsset>::rename;
    kind.remove      = &KindOps<MeshAsset>::remove;
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
    kind.remove      = &KindOps<SkeletonAsset>::remove;
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
    kind.rename      = &KindOps<AnimationClipAsset>::rename;
    kind.remove      = &KindOps<AnimationClipAsset>::remove;
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
    kind.rename      = &KindOps<AudioClipAsset>::rename;
    kind.remove      = &KindOps<AudioClipAsset>::remove;
    return kind;
}

const AssetKind KINDS[] = {
    materialKind(),
    textureKind(),
    meshKind(),
    skeletonKind(),
    clipKind(),
    soundKind()
};

const AssetKind& kindOf(AssetType type) {
    for (const AssetKind& k : KINDS) {
        if (k.type == type) return k;
    }
    return KINDS[0];
}

} // namespace

void AssetBrowserPanel::openRename(AssetType kind, StorageIndex key, const std::string& name) {
    snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", name.c_str());
    m_rename = AssetTarget{kind, key, name, true};
}

void AssetBrowserPanel::ensureAssets(ResourceManager& resources) {
    m_sphere = materialPreviewSphere(resources);

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

    for (const AssetKind& kind : KINDS) {
        // Through the grid's filter, so searching narrows the rail too.
        int count = 0;
        kind.rows(ec.frame.resources, [&](const AssetRow& row) {
            if (matchesFilter(row.name->c_str(), m_filter)) ++count;
        });

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
        snprintf(buf, sizeof(buf), "%d", count);
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
    const AssetKind& kind = kindOf(m_kind);

    // The kind's primary action, always first; see docs/reference/editor.md, "One verb slot".
    if (ImGui::Button(kind.verb)) {
        switch (kind.verbAction) {
            case Verb::NewMaterial:
                if (MaterialHandle h = EditorActions::createNewMaterial(ec.frame.resources, ec.state)) {
                    ec.state.openMaterial(h);
                }
                break;
            case Verb::ImportModel:   ec.state.requestModelImport = true; break;
            case Verb::ImportTexture: openTextureImport(); break;
            case Verb::ImportSound:   openSoundImport();   break;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kind.verbHint);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(EditorStyle::px(200.0f));
    // Escape empties the box rather than reverting it.
    ImGui::InputTextWithHint(
        "##assetFilter",
        "Search...",
        m_filter,
        sizeof(m_filter),
        ImGuiInputTextFlags_EscapeClearsAll
    );

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
    const AssetKind& kind      = kindOf(m_kind);
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;
    Scene&           scene     = ec.frame.scene;

    const EntityId sel = state.selectedEntity;
    const bool canAssign = kind.assign && sel && scene.isAlive(sel) && kind.target(scene, sel);

    // Snapshotted for the clipper: an off-screen tile bakes no thumbnail.
    std::vector<AssetRow> rows;
    kind.rows(resources, [&](const AssetRow& row) {
        if (matchesFilter(row.name->c_str(), m_filter)) rows.push_back(row);
    });
    if (rows.empty()) {
        ImGui::TextDisabled("%s", m_filter[0] ? "Nothing matches the search." : kind.empty);
        return;
    }

    // Rebuilt every frame, so no scene change has to invalidate it.
    EntityRefs refs;
    scene.forEachEntity([&](EntityId id) {
        AssetSerializer::collectAssetRefs(scene, id, resources, refs);
    });
    std::fill(m_used.begin(), m_used.end(), uint8_t{0});
    UsedSlots used{m_used};
    kind.used(refs, scene, resources, used);

    TileContext tc{ec, m_sphere, m_neutral, TEXTURE_UPLOADS_PER_FRAME};
    AudioDevice& device = ec.audioSystem.device();

    const float face = EditorStyle::px(m_cell);
    const float step = face + ImGui::GetStyle().ItemSpacing.x;
    const int   cols = (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / step));

    const auto drawTile = [&](const AssetRow& row) {
        const uint32_t     id     = row.key.index;
        const GpuTextureId tex    = kind.thumb ? kind.thumb(tc, row.key, row.version) : 0u;
        const bool         orphan = !used.has(id);

        ImGui::PushID(static_cast<int>(id));
        ImGui::BeginGroup();

        // A faint glyph means a bake is coming, unlike a kind with no thumbnail.
        ImVec4 glyph = *kind.accent;
        if (kind.thumb) glyph.w = 0.30f;
        ImVec2 faceMin, faceMax;
        const bool clicked = tileFace(imTexture(tex), face, *kind.accent, kind.icon, glyph, faceMin, faceMax);
        if (ImGui::IsItemHovered()) {
            char full[256];
            kind.detail(resources, row.key, true, full, sizeof(full));
            ImGui::SetTooltip(
                "%s\n%s%s",
                row.name->empty() ? "(unnamed)" : row.name->c_str(),
                full,
                orphan ? "\nNothing in this project uses it" : ""
            );
            // As in the Hierarchy. Not Delete: it destroys the selected entity first, so both fire.
            if (kind.rename && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                openRename(kind.type, row.key, *row.name);
            }
        }

        if (ImGui::BeginPopupContextItem("##tilectx")) {
            sectionLabel(row.name->empty() ? "(unnamed)" : row.name->c_str());
            ImGui::Separator();
            if (kind.type == AssetType::Material && ImGui::MenuItem("Edit Material")) {
                state.openMaterial(MaterialHandle{row.key});
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
            // Not during a session: delete discards the undo history, the set-aside one too.
            ImGui::BeginDisabled(!orphan || ec.input.playing);
            if (ImGui::MenuItem("Delete...")) {
                m_delete = AssetTarget{kind.type, row.key, *row.name, true};
            }
            ImGui::EndDisabled();
            if (ec.input.playing) {
                ImGui::TextDisabled("(stop the session first)");
            } else if (!orphan) {
                ImGui::TextDisabled("(in use - clear the references first)");
            }
            ImGui::EndPopup();
        }

        // On the face, so the tile keeps the common height.
        const bool mine = kind.type == AssetType::AudioClip && m_previewClip == AudioClipHandle{row.key};
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
            state.openMaterial(MaterialHandle{row.key});
        }

        clippedLine(row.name->c_str(), face, false);

        // While heard, the detail line shows the audition's progress.
        if (mine && device.isVoiceActive(m_previewVoice)) {
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
        if (orphan) strip.w *= 0.35f;
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
    options.extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
    options.hint       = "PNG / JPG / TGA / BMP, read as colour (sRGB)";
    m_texturePicker.open(options);
}

void AssetBrowserPanel::serviceTextureImport(EditorContext& ec) {
    std::string picked;
    if (!m_texturePicker.draw(picked)) return;

    // Asked first: loadTexture would decode again and replace the asset in place.
    ResourceManager& resources = ec.frame.resources;
    const std::string ref = ProjectPaths::toProjectRelative(picked);
    if (resources.findByName<TextureAsset>(ref)) {
        ec.state.pushToast(ToastKind::Info, ref + " is already imported");
        return;
    }

    // Colour: a hand-picked texture is art. Data maps come with a model or a Material Editor slot.
    if (!loadTexture(picked, resources, TextureUsage::Color)) {
        ec.state.pushToast(ToastKind::Error, "Could not decode " + picked);
    } else {
        ec.state.markSceneDirty();
    }
}

void AssetBrowserPanel::openSoundImport() {
    AssetPicker::Options options;
    options.title      = "Import Sound";
    options.root       = ProjectPaths::assets();
    options.recursive  = true;
    options.extensions = {".wav", ".mp3", ".flac"};
    options.maxResults = 2000;
    options.hint       = "WAV / MP3 / FLAC";
    m_soundPicker.open(options);
}

void AssetBrowserPanel::serviceSoundImport(EditorContext& ec) {
    std::string picked;
    if (!m_soundPicker.draw(picked)) return;

    // Asked first: loadAudioClip returns a held name's clip silently, hiding which outcome it was.
    ResourceManager& resources = ec.frame.resources;
    const std::string ref = ProjectPaths::toProjectRelative(picked);
    const bool alreadyHeld = static_cast<bool>(resources.findByName<AudioClipAsset>(ref));

    if (!loadAudioClip(picked, resources)) {
        ec.state.pushToast(ToastKind::Error, "Could not decode " + picked);
    } else if (alreadyHeld) {
        ec.state.pushToast(ToastKind::Info, ref + " is already imported");
    } else {
        ec.state.markSceneDirty();
    }
}

void AssetBrowserPanel::drawRenameModal(EditorContext& ec) {
    const bool renamed = renameDialog("Rename Asset", m_rename.open, m_renameBuf, sizeof(m_renameBuf));
    if (renamed) {
        const AssetKind& kind = kindOf(m_rename.kind);
        if (m_rename.key && kind.rename) {
            kind.rename(ec, m_rename.key, m_rename.name, m_renameBuf);
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

    const DialogResult r = dialogButtons(m_delete.open, "Delete");
    if (r == DialogResult::Confirm && m_delete.key) {
        // The voice holds the samples by shared_ptr: a removed clip would play on, unstoppable.
        if (m_delete.kind == AssetType::AudioClip && m_previewClip == AudioClipHandle{m_delete.key}) {
            ec.audioSystem.device().stopVoice(m_previewVoice);
            m_previewVoice = 0;
            m_previewClip  = {};
        }
        ec.materialPreviews.evict(previewKey(m_delete.kind, m_delete.key.index));
        kindOf(m_delete.kind).remove(ec.frame.resources, m_delete.key);
        ec.state.commands.clear();
        ec.state.markSceneDirty();
    }
    if (r != DialogResult::None) m_delete.key = {};
    endDialog();
}

} // namespace Vkm::Engine
