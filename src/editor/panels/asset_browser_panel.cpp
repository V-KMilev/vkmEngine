#include "panels/asset_browser_panel.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>

#include "framework/editor_common.h"
#include "framework/editor_actions.h"
#include "framework/editor_commands.h"
#include "framework/material_preview_session.h"
#include "system/render/render_system.h"
#include "generator/mesh_generators.h"

namespace Engine {

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
    ImGui::SetNextItemWidth(170.0f);
    ImGui::SliderFloat("##cell", &m_cell, 64.0f, 256.0f, "thumb %.0f");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
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
        ImGui::EndTabBar();
    }

    // Shared rename modal, opened from either tab's context menu. Rendered at
    // panel scope (not inside the tab child) so the popup id resolves cleanly.
    if (m_renameOpen) {
        ImGui::OpenPopup("Rename Asset");
        m_renameOpen = false;
    }
    if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(280.0f);
        const bool commit = ImGui::InputText("##rnbuf", m_renameBuf, sizeof(m_renameBuf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        const bool ok     = ImGui::Button("Rename") || commit;
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel");
        if (ok && m_renameBuf[0] != '\0') {
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
            ImGui::CloseCurrentPopup();
        }
        if (cancel) { m_renameKey = {}; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    ImGui::End();
}

// One grid body for both asset families. Asset selects the type; the
// material-only behavior (an "Open in Material Editor" context item and
// left-click-to-edit, plus a sphere preview vs. a neutral-material preview)
// is selected with `if constexpr`. Everything else - the in-use guard, the
// assign / rename / delete actions, thumbnail sizing, tooltip text, the
// deferred eviction, and the column layout - is shared verbatim.
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
    // never leave a Mesh component pointing at a freed handle (the render path
    // get()s the asset with no liveness guard).
    std::unordered_set<uint32_t> used;
    scene.forEach<Mesh>([&](EntityId, const Mesh& m) {
        if constexpr (isMaterial) { if (m.material) used.insert(m.material.id()); }
        else                      { if (m.mesh)     used.insert(m.mesh.id()); }
    });
    AssetHandle toDelete{};

    int i = 0;
    resources.template forEachOfType<Asset>([&](AssetHandle h, const Asset& a) {
        if (a.hidden) return;  // editor helpers / preview primitives are not user-facing

        // Material thumbnails render the asset on a shared preview sphere; mesh
        // thumbnails render the asset under a shared neutral material. The two
        // preview poses differ, so the texture() call is selected per family.
        uint32_t tex;
        uint64_t key;
        if constexpr (isMaterial) {
            key = materialKey(h.id());
            tex = ec.materialPreviews.texture(
                resources, h, m_sphere, 30.0f, 18.0f, 2.6f,
                key, a.version, /*live*/ false);
        } else {
            key = meshKey(h.id());
            tex = ec.materialPreviews.texture(
                resources, m_neutral, h, 25.0f, 15.0f, 2.6f,
                key, a.version, /*live*/ false);
        }

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
                if constexpr (isMaterial) scene.get<Mesh>(sel).material = h;
                else                      scene.get<Mesh>(sel).mesh     = h;
                state.markSceneDirty();
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

void AssetBrowserPanel::drawMaterials(EditorContext& ec) { drawAssetGrid<MaterialAsset>(ec); }
void AssetBrowserPanel::drawMeshes(EditorContext& ec)    { drawAssetGrid<MeshAsset>(ec); }

} // namespace Engine
