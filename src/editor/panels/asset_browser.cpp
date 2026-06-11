#include "panels/asset_browser.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

#include "framework/editor_common.h"
#include "framework/editor_actions.h"
#include "framework/editor_commands.h"
#include "framework/material_preview_session.h"
#include "resource/asset_gc.h"
#include "system/render/render_system.h"
#include "generator/mesh_generators.h"

namespace Engine {

namespace {
// Distinct cache-key spaces. 0 is reserved for the live Material Editor.
inline uint64_t materialKey(uint32_t id) { return static_cast<uint64_t>(id) + 1ull; }
inline uint64_t meshKey(uint32_t id)     { return (1ull << 40) | id; }

ImTextureID asTex(uint32_t gl) {
    return static_cast<ImTextureID>(static_cast<intptr_t>(gl));
}

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
        return ImGui::ImageButton("##img", asTex(tex), ImVec2(cell, cell),
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

void AssetBrowserPanel::openRename(MaterialHandle h, const std::string& name) {
    snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", name.c_str());
    m_renameOldName = name;
    m_renameMat  = h;
    m_renameMesh = {};
    m_renameOpen = true;
}

void AssetBrowserPanel::openRename(MeshHandle h, const std::string& name) {
    snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", name.c_str());
    m_renameOldName = name;
    m_renameMesh = h;
    m_renameMat  = {};
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

    // Deferred so the purge runs after this frame's tabs finish iterating the
    // asset tables (removing mid-iteration would skip tiles).
    bool purgeRequested = false;

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
    if (ImGui::Button("Purge Unused")) purgeRequested = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Free meshes / materials / textures no entity uses\n"
                          "(e.g. after deleting an imported model).\n"
                          "Clears the undo history.");
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
            if (m_renameMat) {
                resources.rename(m_renameMat, m_renameBuf);
                state.commands.push(std::make_unique<RenameAssetCommand<MaterialHandle>>(
                    resources, m_renameMat, m_renameOldName, m_renameBuf, "Rename Material"));
            } else if (m_renameMesh) {
                resources.rename(m_renameMesh, m_renameBuf);
                state.commands.push(std::make_unique<RenameAssetCommand<MeshHandle>>(
                    resources, m_renameMesh, m_renameOldName, m_renameBuf, "Rename Mesh"));
            }
            state.markSceneDirty();
            m_renameMat = {}; m_renameMesh = {};
            ImGui::CloseCurrentPopup();
        }
        if (cancel) { m_renameMat = {}; m_renameMesh = {}; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (purgeRequested) {
        const auto freed = purgeUnusedAssets(ec.frame.scene, resources);
        // Freeing assets dangles any earlier undo snapshot that referenced
        // them (a Delete redo would restore handles to freed slots), so the
        // purge clears the undo history.
        state.commands.clear();
        state.pushToast(EditorState::ToastKind::Info,
            freed > 0 ? ("Purged " + std::to_string(freed) + " unused asset(s)")
                      : std::string("No unused assets to purge"));
    }

    ImGui::End();
}

void AssetBrowserPanel::drawMaterials(EditorContext& ec) {
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;
    Scene&           scene     = ec.frame.scene;

    const EntityId sel = state.selectedEntity;
    const bool canAssign = sel && scene.isAlive(sel) && scene.has<Mesh>(sel);

    const float step = cellWidth(m_cell) + ImGui::GetStyle().ItemSpacing.x;
    const int   cols = (std::max)(1, static_cast<int>(
                            ImGui::GetContentRegionAvail().x / step));

    // Materials referenced by any entity - delete is disabled for these so we
    // never leave a Mesh component pointing at a freed handle (the render path
    // get()s the material with no liveness guard).
    std::unordered_set<uint32_t> usedMats;
    scene.forEach<Mesh>([&](EntityId, const Mesh& m) { if (m.material) usedMats.insert(m.material.id()); });
    MaterialHandle toDelete{};

    int i = 0;
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle h, const MaterialAsset& a) {
        if (a.hidden) return;  // editor helpers (e.g. thumbnail neutral) are not user-facing

        const uint64_t key = materialKey(h.id());
        const uint32_t tex = ec.materialPreviews.texture(
            resources, h, m_sphere, 30.0f, 18.0f, 2.6f,
            key, a.version, /*live*/ false);

        ImGui::PushID(static_cast<int>(h.id()));
        ImGui::BeginGroup();

        const bool clicked = thumbButton(tex, m_cell);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", a.name.empty() ? "(unnamed)" : a.name.c_str());

        if (ImGui::BeginPopupContextItem("##matctx")) {
            if (ImGui::MenuItem("Open in Material Editor")) {
                state.materialEditorTarget = h;
                state.showMaterialEditor   = true;
            }
            ImGui::BeginDisabled(!canAssign);
            if (ImGui::MenuItem("Assign to selected entity")) {
                scene.get<Mesh>(sel).material = h;
                state.markSceneDirty();
            }
            ImGui::EndDisabled();
            if (!canAssign) ImGui::TextDisabled("(select a mesh entity to assign)");

            ImGui::Separator();
            if (ImGui::MenuItem("Rename...")) openRename(h, a.name);
            const bool inUse = usedMats.count(h.id()) != 0;
            ImGui::BeginDisabled(inUse);
            if (ImGui::MenuItem("Delete")) toDelete = h;
            ImGui::EndDisabled();
            if (inUse) ImGui::TextDisabled("(in use - reassign before deleting)");
            ImGui::EndPopup();
        }

        if (clicked) {                       // left-click = edit
            state.materialEditorTarget = h;
            state.showMaterialEditor   = true;
        }

        thumbName(a.name.c_str(), m_cell);
        ImGui::EndGroup();
        ImGui::PopID();

        if (++i % cols != 0) ImGui::SameLine();
    });

    if (toDelete) {
        ec.materialPreviews.evict(materialKey(toDelete.id()));
        resources.remove(toDelete);
        state.markSceneDirty();
    }

    if (i == 0) ImGui::TextDisabled("No materials. Import a model or duplicate one.");
}

void AssetBrowserPanel::drawMeshes(EditorContext& ec) {
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;
    Scene&           scene     = ec.frame.scene;

    const EntityId sel = state.selectedEntity;
    const bool canAssign = sel && scene.isAlive(sel) && scene.has<Mesh>(sel);

    const float step = cellWidth(m_cell) + ImGui::GetStyle().ItemSpacing.x;
    const int   cols = (std::max)(1, static_cast<int>(
                            ImGui::GetContentRegionAvail().x / step));

    // Meshes referenced by any entity (Mesh.mesh) - delete
    // is disabled for these so the render path never get()s a freed handle.
    std::unordered_set<uint32_t> usedMeshes;
    scene.forEach<Mesh>([&](EntityId, const Mesh& m) { if (m.mesh) usedMeshes.insert(m.mesh.id()); });
    MeshHandle toDelete{};

    int i = 0;
    resources.forEachOfType<MeshAsset>([&](MeshHandle h, const MeshAsset& a) {
        if (a.hidden) return;  // editor preview primitives are not user-facing

        const uint64_t key = meshKey(h.id());
        const uint32_t tex = ec.materialPreviews.texture(
            resources, m_neutral, h, 25.0f, 15.0f, 2.6f,
            key, a.version, /*live*/ false);

        ImGui::PushID(static_cast<int>(h.id()));
        ImGui::BeginGroup();

        thumbButton(tex, m_cell);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", a.name.empty() ? "(unnamed)" : a.name.c_str());

        if (ImGui::BeginPopupContextItem("##meshctx")) {
            ImGui::BeginDisabled(!canAssign);
            if (ImGui::MenuItem("Assign to selected entity")) {
                scene.get<Mesh>(sel).mesh = h;
                state.markSceneDirty();
            }
            ImGui::EndDisabled();
            if (!canAssign) ImGui::TextDisabled("(select a mesh entity to assign)");

            ImGui::Separator();
            if (ImGui::MenuItem("Rename...")) openRename(h, a.name);
            const bool inUse = usedMeshes.count(h.id()) != 0;
            ImGui::BeginDisabled(inUse);
            if (ImGui::MenuItem("Delete")) toDelete = h;
            ImGui::EndDisabled();
            if (inUse) ImGui::TextDisabled("(in use - reassign before deleting)");
            ImGui::EndPopup();
        }

        thumbName(a.name.c_str(), m_cell);
        ImGui::EndGroup();
        ImGui::PopID();

        if (++i % cols != 0) ImGui::SameLine();
    });

    if (toDelete) {
        ec.materialPreviews.evict(meshKey(toDelete.id()));
        resources.remove(toDelete);
        state.markSceneDirty();
    }

    if (i == 0) ImGui::TextDisabled("No meshes loaded. Use Import Model...");
}

} // namespace Engine
