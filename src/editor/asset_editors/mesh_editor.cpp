#include "asset_editors/mesh_editor.h"

#include <cstdio>

#include <imgui.h>

#include "asset_editors/asset_editors.h"
#include "core/fnv1a.h"
#include "editor_actions.h"
#include "core/system.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/view_placement.h"
#include "panels/material_editor_panel.h"
#include "resource/asset/mesh_asset.h"
#include "resource/resource_manager.h"
#include "ui/asset_combo.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

MeshEditor::MeshEditor(StorageIndex key)
    : AssetEditor(AssetType::Mesh, key)
    , m_viewport(AssetEditors::viewportKey(AssetType::Mesh, key))
{}

const Resource* MeshEditor::asset(const ResourceManager& resources) const {
    return resources.tryGet(MeshHandle{key()});
}

void MeshEditor::draw(EditorContext& ec) {
    ResourceManager& resources = ec.frame.resources;
    if (m_look && !resources.tryGet(m_look)) m_look = {};
    const MaterialHandle shown = m_look ? m_look : neutralPreviewMaterial(resources);
    const MeshAsset&     mesh  = resources.get(MeshHandle{key()});
    const MaterialAsset& look  = resources.get(shown);

    const auto view = [&] {
        const uint64_t versions[] = {mesh.version(), look.version(), shown.id()};
        const uint64_t version    = fnv1a64Bytes(versions, sizeof(versions));
        m_viewport.draw(ec, {{MeshHandle{key()}, shown}}, version, glm::vec2(0.0f));
    };
    viewAndDetails(view, [&] { drawDetails(ec); });
}

void MeshEditor::drawDetails(EditorContext& ec) {
    ResourceManager& resources = ec.frame.resources;
    const MeshAsset& mesh = resources.get(MeshHandle{key()});

    sectionLabel("Geometry");
    char line[96];
    std::snprintf(line, sizeof(line), "%zu", mesh.indices.size() / 3);
    factRow("Triangles", line);
    std::snprintf(line, sizeof(line), "%zu", mesh.vertices.size());
    factRow("Vertices", line);
    const glm::vec3 size = mesh.boundsMax - mesh.boundsMin;
    std::snprintf(line, sizeof(line), "%.2f x %.2f x %.2f m", size.x, size.y, size.z);
    factRow("Size", line);
    factRow("Rig", mesh.skin.empty() ? "None" : mesh.skeleton.c_str());
    factRow("Lightmap UVs", mesh.lightmapUVs.empty() ? "None" : "Unwrapped");

    ImGui::Spacing();
    sectionLabel("Preview");
    pickAsset<MaterialAsset>("##look", "Material", resources, ec.state, m_look);
    ImGui::SetItemTooltip("What the preview draws it in; (none) is a plain grey");

    ImGui::Spacing();
    sectionLabel("Scene");
    if (ImGui::Button("Place in Scene", ImVec2(-1.0f, 0.0f))) {
        const float     base = ViewPlacement::meshBase(resources, MeshHandle{key()});
        const glm::vec3 at   = ViewPlacement::placeInView(ec.frame.scene, resources, ec.state.viewAim, base);
        EditorActions::createFromAsset(ec.frame.scene, resources, ec.state, AssetType::Mesh, key(), at);
    }
    ImGui::SetItemTooltip("Add an entity drawing it where the viewport looks, and select it");
}

} // namespace Vkm::Engine
