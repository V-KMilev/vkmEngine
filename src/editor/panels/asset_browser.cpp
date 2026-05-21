#include "panels/asset_browser.h"
#include "framework/editor_common.h"

#include "system/render/render_system.h"
#include "generator/mesh_generators.h"

#include <cstdint>
#include <algorithm>

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

void AssetBrowserPanel::ensureAssets(ResourceManager& resources) {
    // Re-acquire every call rather than caching with a "ready" flag:
    // SceneSerializer::load swaps the ResourceManager wholesale, so any
    // cached handle survives the swap as a dangling (index, generation)
    // pair into the now-discarded manager. findByName is O(1) (per-type
    // name index in ResourceManager), so the cost is negligible.
    m_sphere = resources.findByName<MeshAsset>("mesh:preview_sphere");
    if (!m_sphere) m_sphere = resources.addInternal(generateSphere(), "mesh:preview_sphere");

    m_neutral = resources.findByName<MaterialAsset>("mat:thumb_neutral");
    if (!m_neutral) {
        MaterialAsset m;
        m.albedo    = glm::vec4(0.78f, 0.78f, 0.80f, 1.0f);
        m.metallic  = 0.0f;
        m.roughness = 0.55f;
        m_neutral = resources.addInternal(std::move(m), "mat:thumb_neutral");
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

    int i = 0;
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle h, const MaterialAsset& a) {
        if (a.editorOnly) return;  // editor helpers (e.g. thumbnail neutral) are not user-facing

        const uint64_t key = materialKey(h.id());
        const uint32_t tex = ec.renderSystem
            ? ec.renderSystem->materialPreviewTexture(
                  resources, h, m_sphere, 30.0f, 18.0f, 2.6f,
                  key, a.version, /*live*/ false)
            : 0u;

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
            if (ImGui::MenuItem("Assign to selected entity"))
                scene.get<Mesh>(sel).material = h;
            ImGui::EndDisabled();
            if (!canAssign) ImGui::TextDisabled("(select a mesh entity to assign)");
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

    int i = 0;
    resources.forEachOfType<MeshAsset>([&](MeshHandle h, const MeshAsset& a) {
        if (a.editorOnly) return;  // editor preview primitives are not user-facing

        const uint64_t key = meshKey(h.id());
        const uint32_t tex = ec.renderSystem
            ? ec.renderSystem->materialPreviewTexture(
                  resources, m_neutral, h, 25.0f, 15.0f, 2.6f,
                  key, a.version, /*live*/ false)
            : 0u;

        ImGui::PushID(static_cast<int>(h.id()));
        ImGui::BeginGroup();

        thumbButton(tex, m_cell);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", a.name.empty() ? "(unnamed)" : a.name.c_str());

        if (ImGui::BeginPopupContextItem("##meshctx")) {
            ImGui::BeginDisabled(!canAssign);
            if (ImGui::MenuItem("Assign to selected entity"))
                scene.get<Mesh>(sel).mesh = h;
            ImGui::EndDisabled();
            if (!canAssign) ImGui::TextDisabled("(select a mesh entity to assign)");
            ImGui::EndPopup();
        }

        thumbName(a.name.c_str(), m_cell);
        ImGui::EndGroup();
        ImGui::PopID();

        if (++i % cols != 0) ImGui::SameLine();
    });

    if (i == 0) ImGui::TextDisabled("No meshes loaded. Use Import Model...");
}

} // namespace Engine
