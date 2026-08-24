#include "panels/asset_browser_panel.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>

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
#include "io/project_paths.h"
#include "ui/audition_transport.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {
// Distinct cache-key spaces. 0 is reserved for the live Material Editor.
inline uint64_t materialKey(uint32_t id) { return static_cast<uint64_t>(id) + 1ull; }
inline uint64_t meshKey(uint32_t id)     { return (1ull << 40) | id; }

// Total footprint of one cell (image + its frame padding), so the column
// count and the not-yet-baked placeholder line up exactly.
float cellWidth(float cell) {
    return cell + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// The thumbnail image-button (or an equal-size placeholder while it waits
// for its bake turn). Leaves itself as the "last item" so the caller can
// attach a context menu to it. Returns true on left-click.
bool thumbButton(uint32_t tex, float cell) {
    if (tex) {
        return ImGui::ImageButton("##img", imTexture(tex), ImVec2(cell, cell),
                                  ImVec2(0, 1), ImVec2(1, 0));
    }
    const float w = cellWidth(cell);
    ImGui::Button("...", ImVec2(w, w));
    return false;
}

// One clipped name line under a thumbnail (uniform cell height).
void thumbName(const char* name, float cell) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.20s", (name && name[0]) ? name : "(unnamed)");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cellWidth(cell));
    ImGui::TextUnformatted(buf);
    ImGui::PopTextWrapPos();
}
}  // namespace

template<typename Asset>
void AssetBrowserPanel::openRename(Handle<Asset> h, const std::string& name) {
    static_assert(std::is_same_v<Asset, MaterialAsset> || std::is_same_v<Asset, MeshAsset>,
                  "AssetBrowserPanel rename only supports materials and meshes");
    snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", name.c_str());
    m_renameOldName = name;
    m_renameKind = std::is_same_v<Asset, MaterialAsset> ? RenameKind::Material : RenameKind::Mesh;
    m_renameKey  = h.key;
    m_renameOpen = true;
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
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;

    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset Browser", &state.showAssetBrowser)) {
        ImGui::End();
        return;
    }

    ensureAssets(resources);

    if (ImGui::Button("Import Model...")) state.requestModelImport = true;
    ImGui::SameLine();
    if (ImGui::Button("New Material")) {
        if (MaterialHandle h = EditorActions::createNewMaterial(resources, state)) {
            state.materialEditorTarget = h;
            state.showMaterialEditor   = true;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Create a blank PBR material and open it in the Material Editor");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(EditorStyle::px(140.0f));
    ImGui::SliderFloat("##cell", &m_cell, 64.0f, 256.0f, "Size %.0f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Thumbnail size");
    // Search fills the rest of the row - the grid gates on it below.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##assetFilter", "Search...", m_filter, sizeof(m_filter));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Left-click a material to edit it.\n"
                          "Right-click any tile for Assign / actions.");
    ImGui::Separator();

    if (ImGui::BeginTabBar("##abtabs")) {
        if (ImGui::BeginTabItem("Materials")) {
            ImGui::BeginChild("##matgrid");
            drawMaterials(ec);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Meshes")) {
            ImGui::BeginChild("##meshgrid");
            drawMeshes(ec);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sounds")) {
            ImGui::BeginChild("##soundlist");
            drawSounds(ec);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Shared rename modal, opened from either tab's context menu. Rendered at
    // panel scope (not inside the tab child) so the popup id resolves cleanly.
    if (beginDialog("Rename Asset", m_renameOpen)) {
        ImGui::SetNextItemWidth(EditorStyle::px(280.0f));
        const bool commit = ImGui::InputText("##rnbuf", m_renameBuf, sizeof(m_renameBuf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);

        const DialogResult r = dialogButtons(m_renameOpen, "Rename",
                                             m_renameBuf[0] != '\0', commit);
        if (r == DialogResult::Confirm) {
            // Apply now, then push the reverse (the command captures before/
            // after names and re-applies on redo). Routed through the stack
            // so an accidental rename is one Ctrl+Z away.
            if (m_renameKey) {
                if (m_renameKind == RenameKind::Material) {
                    const MaterialHandle h{m_renameKey};
                    resources.rename(h, m_renameBuf);
                    state.commands.push(std::make_unique<RenameAssetCommand<MaterialHandle>>(
                        resources, h, m_renameOldName, m_renameBuf, "Rename Material"));
                } else {
                    const MeshHandle h{m_renameKey};
                    resources.rename(h, m_renameBuf);
                    state.commands.push(std::make_unique<RenameAssetCommand<MeshHandle>>(
                        resources, h, m_renameOldName, m_renameBuf, "Rename Mesh"));
                }
            }
            state.markSceneDirty();
            m_renameKey = {};
        }
        if (r == DialogResult::Cancel) m_renameKey = {};
        endDialog();
    }

    ImGui::End();
}

// One grid body for both asset families. The material-only behavior (an "Open
// in Material Editor" context item and left-click-to-edit, plus a sphere
// preview vs. a neutral-material preview) is selected with `if constexpr`;
// everything else is shared verbatim.
template<typename Asset>
void AssetBrowserPanel::drawAssetGrid(EditorContext& ec) {
    static_assert(std::is_same_v<Asset, MaterialAsset> || std::is_same_v<Asset, MeshAsset>,
                  "AssetBrowserPanel grid only supports materials and meshes");
    constexpr bool isMaterial = std::is_same_v<Asset, MaterialAsset>;
    using AssetHandle = Handle<Asset>;

    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;
    Scene&           scene     = ec.frame.scene;

    const EntityId sel = state.selectedEntity;
    const bool canAssign = sel && scene.isAlive(sel) && scene.has<Mesh>(sel);

    const float step = cellWidth(m_cell) + ImGui::GetStyle().ItemSpacing.x;
    const int   cols = (std::max)(1, static_cast<int>(
                            ImGui::GetContentRegionAvail().x / step));

    // Assets referenced by any entity - delete is disabled for these so we
    // never leave a component pointing at a freed handle (the render path
    // get()s the asset with no liveness guard). Every component that stores a
    // handle of this family has to be walked, not just Mesh: a decal-only
    // material or an LOD-only mesh is just as live to the renderer.
    std::unordered_set<uint32_t> used;
    scene.forEach<Mesh>([&](EntityId, const Mesh& m) {
        if constexpr (isMaterial) { if (m.material) used.insert(m.material.id()); }
        else                      { if (m.mesh)     used.insert(m.mesh.id()); }
    });
    if constexpr (isMaterial) {
        scene.forEach<Decal>([&](EntityId, const Decal& d) {
            if (d.material) used.insert(d.material.id());
        });
    } else {
        scene.forEach<LOD>([&](EntityId, const LOD& l) {
            for (const LODLevel& level : l.levels) {
                if (level.mesh) used.insert(level.mesh.id());
            }
        });
    }
    AssetHandle toDelete{};

    int i = 0;
    resources.template forEachOfType<Asset>([&](AssetHandle h, const Asset& a) {
        if (a.hidden) return;  // editor helpers / preview primitives are not user-facing
        if (!matchesFilter(a.name.c_str(), m_filter)) return;

        // Material thumbnails render the asset on a shared preview sphere; mesh
        // thumbnails render the asset under a shared neutral material.
        PreviewRequest req;
        if constexpr (isMaterial) {
            req.key      = materialKey(h.id());
            req.material = h;
            req.mesh     = m_sphere;
            req.yawDeg   = 30.0f;
            req.pitchDeg = 18.0f;
        } else {
            req.key      = meshKey(h.id());
            req.material = m_neutral;
            req.mesh     = h;
            req.yawDeg   = 25.0f;
            req.pitchDeg = 15.0f;
        }
        req.distance = 2.6f;
        const uint32_t tex =
            ec.materialPreviews.texture(resources, req, a.version, /*live*/ false);

        ImGui::PushID(static_cast<int>(h.id()));
        ImGui::BeginGroup();

        const bool clicked [[maybe_unused]] = thumbButton(tex, m_cell);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", a.name.empty() ? "(unnamed)" : a.name.c_str());

        if (ImGui::BeginPopupContextItem("##assetctx")) {
            if constexpr (isMaterial) {
                if (ImGui::MenuItem("Open in Material Editor")) {
                    state.materialEditorTarget = h;
                    state.showMaterialEditor   = true;
                }
            }
            ImGui::BeginDisabled(!canAssign);
            if (ImGui::MenuItem("Assign to selected entity")) {
                // The same edit the Inspector's asset dropdown makes, so it
                // takes the same road: pushEdit is what gives it an undo step
                // and what turns it into a prefab override when the entity is
                // an instance. Writing the component here instead left the
                // instance's override list empty, and the next save wrote the
                // prefab's mesh back over the one on screen.
                Mesh& mesh = scene.get<Mesh>(sel);
                const Mesh before = mesh;
                if constexpr (isMaterial) mesh.material = h;
                else                      mesh.mesh     = h;
                pushEdit<Mesh>(scene, resources, state, sel, before, mesh,
                               isMaterial ? "Assign Material" : "Assign Mesh");
            }
            ImGui::EndDisabled();
            if (!canAssign) ImGui::TextDisabled("(select a mesh entity to assign)");

            ImGui::Separator();
            if (ImGui::MenuItem("Rename...")) openRename<Asset>(h, a.name);
            const bool inUse = used.count(h.id()) != 0;
            ImGui::BeginDisabled(inUse);
            if (ImGui::MenuItem("Delete")) toDelete = h;
            ImGui::EndDisabled();
            if (inUse) ImGui::TextDisabled("(in use - reassign before deleting)");
            ImGui::EndPopup();
        }

        if constexpr (isMaterial) {
            if (clicked) {                       // left-click = edit
                state.materialEditorTarget = h;
                state.showMaterialEditor   = true;
            }
        }

        thumbName(a.name.c_str(), m_cell);
        ImGui::EndGroup();
        ImGui::PopID();

        if (++i % cols != 0) ImGui::SameLine();
    });

    if (toDelete) {
        ec.materialPreviews.evict(isMaterial ? materialKey(toDelete.id()) : meshKey(toDelete.id()));
        resources.remove(toDelete);
        state.markSceneDirty();
    }

    if (i == 0) {
        if constexpr (isMaterial) ImGui::TextDisabled("No materials. Import a model or duplicate one.");
        else                      ImGui::TextDisabled("No meshes loaded. Use Import Model...");
    }
}

void AssetBrowserPanel::drawSounds(EditorContext& ec) {
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;
    Scene&           scene     = ec.frame.scene;
    AudioDevice&     device    = ec.audioSystem.device();

    if (ImGui::Button("Import Sound...")) m_requestSoundImport = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Decode a wav / mp3 / flac into the project's assets");
    ImGui::SameLine();
    // Both ways a clip can be inaudible with nothing on this tab wrong, said
    // where the tab already says the first one. A muted mix is the quieter of
    // the two: the row shows a Pause and a cursor running against the clip's
    // length, so the audition looks exactly like one that is working.
    if (!device.isOpen()) {
        ImGui::TextColored(EditorStyle::WARNING, "No audio device - clips import but cannot be heard.");
    } else if (device.masterVolume() <= 0.0f) {
        ImGui::TextColored(EditorStyle::WARNING,
                           "Audio Listener volume is 0 - an audition is silent too.");
    } else {
        ImGui::TextDisabled("Play a clip to hear it; auditioning changes nothing in the scene.");
    }
    ImGui::Separator();

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
    if (std::string picked; m_soundPicker.draw(picked)) {
        // Asked before the import, because loadAudioClip answers a name it
        // already holds with the clip it already has and decodes nothing. The
        // three outcomes then read apart: a file that would not decode, one
        // that is already here, and a clip that is new. Picking a file and
        // being told nothing at all is how the second one looked, and it was
        // the one that also dirtied the scene - an unsaved-changes prompt for
        // an import that did not happen is the prompt meaning less.
        const std::string ref = ProjectPaths::toProjectRelative(picked);
        const bool alreadyHeld = static_cast<bool>(resources.findByName<AudioClipAsset>(ref));

        if (!loadAudioClip(picked, resources)) {
            state.pushToast(EditorState::ToastKind::Error, "Could not decode " + picked);
        } else if (alreadyHeld) {
            state.pushToast(EditorState::ToastKind::Info, ref + " is already imported");
        } else {
            state.markSceneDirty();
        }
    }

    // Assigning needs somewhere to assign to. A source with no clip is the
    // usual state right after adding the component, so this is the path that
    // fills it in without going back to the Inspector.
    const EntityId sel = state.selectedEntity;
    const bool canAssign = sel && scene.isAlive(sel) && scene.has<AudioSource>(sel);

    constexpr ImGuiTableFlags TABLE_FLAGS =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##sounds", 5, TABLE_FLAGS)) return;

    // Two buttons wide, and wide enough for two even on the rows that show one:
    // a column that grew with the sounding row would shift every name sideways
    // whenever an audition started.
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetFrameHeight() * 2.0f + EditorStyle::px(24.0f));
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    // Wide enough for the scrubber that takes this cell over while the row is
    // sounding, because a position measured against a length belongs in the
    // column that states the length.
    ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, EditorStyle::px(150.0f));
    ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, EditorStyle::px(96.0f));
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, EditorStyle::px(64.0f));
    ImGui::TableHeadersRow();

    int shown = 0;
    resources.forEachOfType<AudioClipAsset>([&](AudioClipHandle h, const AudioClipAsset& clip) {
        if (clip.hidden || !matchesFilter(clip.name.c_str(), m_filter)) return;
        ++shown;

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(h.id()));

        // The row that is sounding carries the whole transport - hold it, let it
        // go, cut it short - and every other row a Play that replaces it. That
        // is the Inspector card's transport, drawn from the same place, because
        // two surfaces disagreeing about what auditioning means is the drift
        // this tab used to have: it answered the missing half with one button
        // for the whole tab, which is an implementation's single remembered
        // voice showing through into the UI.
        ImGui::TableNextColumn();
        const float ih   = ImGui::GetFrameHeight();
        const bool  mine = m_previewClip == h;
        if (auditionTransport("abSound", device, m_previewVoice, mine, &clip, ih))
            m_previewClip = h;

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(clip.name.empty() ? "(unnamed)" : clip.name.c_str());
        if (ImGui::BeginPopupContextItem("##soundctx")) {
            ImGui::BeginDisabled(!canAssign);
            if (ImGui::MenuItem("Assign to selected Audio Source")) {
                AudioSource& source = scene.get<AudioSource>(sel);
                const AudioSource before = source;
                source.clip = h;
                // Through pushEdit rather than the plain edit command, for the
                // reason the material assign above is: on a prefab instance
                // this is an override, and one recorded nowhere is one the save
                // does not write.
                pushEdit<AudioSource>(scene, resources, state, sel,
                                      before, source, "Assign Sound");
            }
            ImGui::EndDisabled();
            if (!canAssign) ImGui::TextDisabled("(select an entity with an Audio Source)");
            ImGui::EndPopup();
        }

        // The length, until this row is the one being heard: then the same
        // column says how far into that length the audition has got, and moves
        // it. An audition still does not follow the user out of the tab - it is
        // the only place holding the voice's id, so leaving one playing and
        // coming back finds the row still sounding, with its Stop lit.
        ImGui::TableNextColumn();
        if (mine && device.isVoiceActive(m_previewVoice)) {
            auditionScrubber("SndPos", device, m_previewVoice, clip.duration(), -1.0f);
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%.2fs", static_cast<double>(clip.duration()));
        }

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        // Named for one and two channels, counted past that. The cooker accepts
        // up to eight - 7.1 source material is the outer edge it draws - so
        // "stereo" against a six-channel file is this column reporting a format
        // the file does not have, and it is the only place in the tab that says
        // what a clip is.
        if      (clip.channels == 1) ImGui::Text("mono %u Hz",   clip.sampleRate);
        else if (clip.channels == 2) ImGui::Text("stereo %u Hz", clip.sampleRate);
        else                         ImGui::Text("%u ch %u Hz",  clip.channels, clip.sampleRate);

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%.1f MB",
                    static_cast<double>(clip.sampleCount() * sizeof(int16_t)) / (1024.0 * 1024.0));

        ImGui::PopID();
    });

    ImGui::EndTable();

    if (shown == 0) ImGui::TextDisabled("No sounds. Use Import Sound...");
}

void AssetBrowserPanel::drawMaterials(EditorContext& ec) { drawAssetGrid<MaterialAsset>(ec); }
void AssetBrowserPanel::drawMeshes(EditorContext& ec)    { drawAssetGrid<MeshAsset>(ec); }

} // namespace Vkm::Engine
