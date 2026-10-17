#include "panels/terrain_editor_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "command/editor_commands.h"
#include "core/reflect.h"
#include "core/system.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/terrain.h"
#include "ecs/scene.h"
#include "editor_actions.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "overlays/terrain_tool.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/resource_manager.h"
#include "system/render/editor_render_hooks.h"
#include "system/terrain/terrain_brush.h"
#include "ui/asset_combo.h"
#include "ui/editor_icons.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

/// The tools in the order their tiles stand, each with its glyph.
struct ToolTile {
    TerrainTool tool;
    EditorIcon  icon;
};
constexpr ToolTile TOOLS[] = {
    {TerrainTool::Raise,   EditorIcon::Raise},
    {TerrainTool::Lower,   EditorIcon::Lower},
    {TerrainTool::Smooth,  EditorIcon::Smooth},
    {TerrainTool::Flatten, EditorIcon::Flatten},
    {TerrainTool::Paint,   EditorIcon::Paint},
    {TerrainTool::Plants,  EditorIcon::Plants},
};

/// Samples across the brush's profile.
constexpr int PROFILE_SAMPLES = 48;

/// An entity's name, or what it is when it has none.
std::string entityName(const Scene& scene, EntityId id) {
    const Name* name = scene.tryGet<Name>(id);
    return name && name->value[0] ? std::string(name->value) : std::string("Terrain");
}

/**
 * @brief An asset's name cut to a tile's caption: its file's stem and what it is of that file, or
 *        with no file in the name, what follows its prefix.
 *
 * "material:default" reads "default", "a/rocks.gltf:mesh0" "rocks mesh0".
 *
 * @param name The asset's name.
 * @return The caption.
 */
std::string shortName(const std::string& name) {
    const size_t slash = name.find_last_of('/');
    std::string  tail  = slash == std::string::npos ? name : name.substr(slash + 1);
    std::string  part;
    if (const size_t colon = tail.find(':'); colon != std::string::npos) {
        part = tail.substr(colon + 1);
        tail = tail.substr(0, colon);
    }
    if (const size_t dot = tail.find_last_of('.'); dot != std::string::npos && dot > 0) tail.resize(dot);
    if (slash == std::string::npos && !part.empty()) return part;
    std::replace(part.begin(), part.end(), ':', ' ');
    return part.empty() ? tail : tail + " " + part;
}

/**
 * @brief What a tile's face shows.
 */
struct Tile {
    GpuTextureId picture = 0;     ///< 0 shows the glyph instead.
    ImVec4       tint{1.0f, 1.0f, 1.0f, 1.0f};  ///< The picture's tint, or the glyph's colour.
    EditorIcon   glyph = EditorIcon::Material;
    int          badge = 0;       ///< A number in its corner, the layer's; 0 for none.
};

/**
 * @brief A square tile: a picture or a glyph, lit when picked, a caption under it.
 *
 * @param id Unique within the window.
 * @param tile What its face shows.
 * @param caption Under the face, cut to its width.
 * @param on Lit as picked.
 * @param face Edge length, pixels.
 * @param tooltip Shown on hover: the whole names the caption cuts.
 * @return Whether it was clicked.
 */
bool pictureTile(
    const char* id,
    const Tile& tile,
    const char* caption,
    bool on,
    float face,
    const std::string& tooltip
) {
    ImGui::PushID(id);
    ImGui::BeginGroup();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool   hit = ImGui::InvisibleButton("##face", ImVec2(face, face));
    const ImVec2 max(min.x + face, min.y + face);
    ImDrawList*  dl    = ImGui::GetWindowDrawList();
    const float  round = EditorStyle::px(4.0f);
    if (tile.picture) {
        // Loaded textures are flipped at decode, so the UVs unflip.
        const ImU32 shade = ImGui::GetColorU32(tile.tint);
        dl->AddImageRounded(imTexture(tile.picture), min, max, ImVec2(0, 1), ImVec2(1, 0), shade, round);
    } else {
        dl->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), round);
        const ImVec4 glyphColour(tile.tint.x, tile.tint.y, tile.tint.z, 0.8f);
        const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        drawEditorIcon(dl, tile.glyph, centre, face * 0.22f, ImGui::GetColorU32(glyphColour));
    }
    if (tile.badge > 0) {
        char number[16];
        std::snprintf(number, sizeof(number), "%d", tile.badge);
        const ImVec2 text = ImGui::CalcTextSize(number);
        const float  pad  = EditorStyle::px(3.0f);
        const ImVec2 corner(min.x + pad + text.x + 2.0f * pad, min.y + pad + text.y);
        const ImU32  back = ImGui::GetColorU32(EditorStyle::OVERLAY_BG);
        dl->AddRectFilled(ImVec2(min.x + pad, min.y + pad), corner, back, pad);
        dl->AddText(ImVec2(min.x + 2.0f * pad, min.y + pad), ImGui::GetColorU32(ImGuiCol_Text), number);
    }
    const bool  hovered = ImGui::IsItemHovered();
    if (hovered && !tooltip.empty()) ImGui::SetTooltip("%s", tooltip.c_str());
    const float edge    = EditorStyle::px(on ? 2.5f : 1.0f);
    const ImU32 border  = on ? ImGui::GetColorU32(EditorStyle::Accent::TERRAIN)
        : ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Border);
    dl->AddRect(min, max, border, round, 0, edge);
    clippedLine(caption, face, !on);
    ImGui::EndGroup();
    ImGui::PopID();
    return hit;
}

/// A material's albedo map, or 0: what its layer's or plant's tile shows.
GpuTextureId albedoPicture(
    const ResourceManager& resources,
    EditorRenderHooks& hooks,
    MaterialHandle handle
) {
    const MaterialAsset* material = resources.tryGet(handle);
    return material && material->albedoTexture ? hooks.textureId(material->albedoTexture) : 0;
}

/// A material's albedo colour, its tile's tint.
ImVec4 albedoTint(const ResourceManager& resources, MaterialHandle handle) {
    const MaterialAsset* material = resources.tryGet(handle);
    if (!material) return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    return ImVec4(material->albedo.r, material->albedo.g, material->albedo.b, 1.0f);
}

/// How many square tiles of at least @p least pixels fit the row, and the edge that fills it.
float tileEdge(float least, int& columns) {
    const float gap   = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    columns = std::max(1, static_cast<int>((avail + gap) / (least + gap)));
    return std::floor((avail - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
}

} // namespace

void followTerrainSelection(EditorState& state, const Scene& scene) {
    // On a change of selection only, so a terrain chosen in the window holds until the next.
    TerrainToolSettings& brush    = state.terrainTool;
    const EntityId       selected = state.selectedEntity;
    if (selected != brush.followed) {
        brush.followed = selected;
        if (selected && scene.isAlive(selected) && scene.has<Terrain>(selected)) brush.target = selected;
    }
    const EntityId target = brush.target;
    if (target && (!scene.isAlive(target) || !scene.has<Terrain>(target))) state.terrainTool.target = {};
}

void TerrainEditorPanel::draw(EditorContext& ec) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState&     state     = ec.state;

    // With none chosen, the scene's first terrain: the window is never empty while there is one.
    if (!terrainToolTarget(ec)) {
        EntityId first{};
        scene.forEach<Terrain>([&](EntityId id, const Terrain&) {
            if (!first && scene.has<Transform>(id)) first = id;
        });
        state.terrainTool.target = first;
    }
    const EntityId target = terrainToolTarget(ec);
    if (!target) {
        drawEmptyState(ec);
        return;
    }
    Terrain&            component = scene.get<Terrain>(target);
    const TerrainAsset* terrain   = resources.tryGet(component.terrain);
    if (!terrain) {
        drawTargetRow(ec, nullptr);
        ImGui::TextColored(EditorStyle::DANGER, "This entity's Terrain names no terrain asset.");
        ImGui::TextDisabled("Pick one on its Terrain card in the Inspector.");
        return;
    }

    drawTargetRow(ec, terrain);
    ImGui::Spacing();
    drawTools(ec);
    ImGui::Spacing();
    drawBrush(ec);

    // Edits gather as changes and install as one step at the end; the grid is copied only then.
    Edits edits;
    ImGui::Spacing();
    drawLayers(ec, *terrain, edits);
    ImGui::Spacing();
    drawPlants(ec, *terrain, edits);
    ImGui::Spacing();
    drawGrid(*terrain, edits);
    if (!edits.empty()) {
        TerrainAsset after(*terrain);
        for (const auto& edit : edits) edit(after);
        // Swapped in, the copy holds what was there: the step's before.
        TerrainAsset installed(after);
        resources.swapValue(component.terrain, installed);
        auto edit = std::make_unique<TerrainEditCommand>(
            resources,
            component.terrain,
            std::move(installed),
            std::move(after),
            "Edit Terrain"
        );
        state.pushStep(std::move(edit));
    }
}

void TerrainEditorPanel::drawEmptyState(EditorContext& ec) {
    emptyStateHeading(
        EditorIcon::Terrain,
        "No terrain in the scene",
        "A terrain is ground to sculpt, paint and plant.",
        1
    );
    const char* label = "Create Terrain";
    const float width = ImGui::CalcTextSize(label).x + 2.0f * ImGui::GetStyle().FramePadding.x;
    centreNextItem(width);
    if (ImGui::Button(label)) {
        const EntityId made = EditorActions::createEntity(
            ec.frame.scene,
            ec.frame.resources,
            ec.state,
            EditorActions::EntityKind::Terrain
        );
        ec.state.selectEntity(made);
    }
}

void TerrainEditorPanel::drawTargetRow(EditorContext& ec, const TerrainAsset* terrain) {
    Scene&         scene  = ec.frame.scene;
    EditorState&   state  = ec.state;
    const EntityId target = state.terrainTool.target;

    // Which terrain, from the scene's; Select puts it in the Inspector and the hierarchy.
    const float  gap    = ImGui::GetStyle().ItemSpacing.x;
    const char*  select = "Select";
    const float  button = ImGui::CalcTextSize(select).x + 2.0f * ImGui::GetStyle().FramePadding.x;
    const ImVec2 at     = ImGui::GetCursorScreenPos();
    const float  icon   = ImGui::GetFrameHeight();
    ImGui::Dummy(ImVec2(icon, icon));
    drawEditorIcon(
        ImGui::GetWindowDrawList(),
        EditorIcon::Terrain,
        ImVec2(at.x + icon * 0.5f, at.y + icon * 0.5f),
        icon * 0.36f,
        ImGui::GetColorU32(EditorStyle::Accent::TERRAIN)
    );
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button - gap);
    if (beginCombo("##TerrainTarget", entityName(scene, target).c_str())) {
        scene.forEach<Terrain>([&](EntityId id, const Terrain&) {
            ImGui::PushID(static_cast<int>(id.slot()));
            if (ImGui::Selectable(entityName(scene, id).c_str(), id == target)) state.terrainTool.target = id;
            ImGui::PopID();
        });
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(select)) state.selectEntity(target);
    if (terrain) {
        ImGui::TextDisabled(
            "%u x %u vertices over %.0f m, %.2f m apart",
            terrain->resolution,
            terrain->resolution,
            terrain->size,
            terrain->spacing()
        );
    }
}

void TerrainEditorPanel::drawTools(EditorContext& ec) {
    TerrainToolSettings& brush = ec.state.terrainTool;
    sectionLabel("Tools");

    // Six a row when they fit, else three: never a row of four and two.
    const float gap     = ImGui::GetStyle().ItemSpacing.x;
    const float avail   = ImGui::GetContentRegionAvail().x;
    const int   columns = avail >= 6.0f * EditorStyle::px(56.0f) + 5.0f * gap ? 6 : 3;
    const float gaps    = gap * static_cast<float>(columns - 1);
    const float width   = std::floor((avail - gaps) / static_cast<float>(columns));
    const float height  = EditorStyle::px(54.0f);
    ImDrawList* dl      = ImGui::GetWindowDrawList();
    for (size_t t = 0; t < std::size(TOOLS); ++t) {
        const ToolTile& tile = TOOLS[t];
        const bool      on   = brush.tool == tile.tool;
        if (t % static_cast<size_t>(columns) != 0) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(t));
        const ImVec2 min = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##tool", ImVec2(width, height))) {
            brush.tool = on ? TerrainTool::None : tile.tool;
        }
        const bool   hovered = ImGui::IsItemHovered();
        const ImVec2 max(min.x + width, min.y + height);
        const float  round = EditorStyle::px(5.0f);
        ImVec4       fill  = EditorStyle::Accent::TERRAIN;
        fill.w             = 0.32f;
        const ImU32  back  = on ? ImGui::GetColorU32(fill)
            : ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        dl->AddRectFilled(min, max, back, round);
        const ImU32  accent = ImGui::GetColorU32(EditorStyle::Accent::TERRAIN);
        if (on) dl->AddRect(min, max, accent, round, 0, EditorStyle::px(1.5f));
        const char*  label = Reflect::enumName(tile.tool);
        const ImVec2 text  = ImGui::CalcTextSize(label);
        const float  glyph = EditorStyle::px(9.0f);
        const ImVec2 centre((min.x + max.x) * 0.5f, min.y + height * 0.38f);
        const ImU32  ink   = ImGui::GetColorU32(on ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        drawEditorIcon(dl, tile.icon, centre, glyph, on ? accent : ink);
        dl->AddText(ImVec2(centre.x - text.x * 0.5f, max.y - text.y - EditorStyle::px(5.0f)), ink, label);
        if (hovered) ImGui::SetTooltip("%s", terrainToolHint(tile.tool));
        ImGui::PopID();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (brush.tool == TerrainTool::None) {
        ImGui::TextWrapped("Pick a tool, then drag over the terrain in the viewport.");
    } else {
        const auto& keys = ec.state.prefs.keybinds;
        ImGui::TextWrapped("%s.", terrainToolHint(brush.tool));
        // A no-break space holds the two keys on one line.
        ImGui::TextWrapped(
            "%s\xc2\xa0%s size the brush; %s puts it down.",
            keyLabel(keys.terrainBrushSmaller).buf,
            keyLabel(keys.terrainBrushLarger).buf,
            keyLabel(keys.deselect).buf
        );
    }
    ImGui::PopStyleColor();
}

void TerrainEditorPanel::drawBrush(EditorContext& ec) {
    TerrainToolSettings& brush = ec.state.terrainTool;
    sectionLabel("Brush");
    propDrag("Radius", &brush.radius, 0.05f, TERRAIN_BRUSH_MIN_RADIUS, TERRAIN_BRUSH_MAX_RADIUS, "%.2f m");
    propSlider("Strength", &brush.strength, 0.0f, 1.0f, "%.2f", "How fast a held brush works");
    propSlider("Hardness", &brush.hardness, 0.0f, 1.0f, "%.2f", "The share of its radius it works in full");

    // The profile a dab leaves across its width, so hardness reads as a shape, not a number.
    const float  width  = ImGui::GetContentRegionAvail().x;
    const float  height = EditorStyle::px(34.0f);
    const ImVec2 min    = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + width, min.y + height);
    dl->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), EditorStyle::px(4.0f));
    TerrainBrush::Dab dab;
    dab.radius   = 1.0f;
    dab.hardness = brush.hardness;
    ImVec2 points[PROFILE_SAMPLES + 1];
    const float pad = EditorStyle::px(4.0f);
    for (int k = 0; k <= PROFILE_SAMPLES; ++k) {
        const float across = 2.0f * static_cast<float>(k) / PROFILE_SAMPLES - 1.0f;
        const float lift   = TerrainBrush::falloff(dab, std::abs(across));
        const float x = min.x + pad + (width - 2.0f * pad) * static_cast<float>(k) / PROFILE_SAMPLES;
        points[k] = ImVec2(x, max.y - pad - (height - 2.0f * pad) * lift);
    }
    const ImU32 line = ImGui::GetColorU32(EditorStyle::Accent::TERRAIN);
    dl->AddPolyline(points, PROFILE_SAMPLES + 1, line, 0, EditorStyle::px(1.5f));

    if (brush.tool == TerrainTool::Flatten) {
        ImGui::Spacing();
        sectionLabel("Flatten To");
        int mode = brush.fixedHeight ? 1 : 0;
        static const char* const MODES[] = {"Where the stroke starts", "A fixed height"};
        if (propIndexCombo("Height", MODES, static_cast<int>(std::size(MODES)), &mode)) {
            brush.fixedHeight = mode == 1;
        }
        if (brush.fixedHeight) {
            const char* heightTooltip = "Above the terrain's origin";
            propDrag("Fixed Height", &brush.height, 0.05f, 0.0f, 0.0f, "%.2f m", heightTooltip);
        }
    }
}

void TerrainEditorPanel::drawLayers(EditorContext& ec, const TerrainAsset& terrain, Edits& edits) {
    ResourceManager&     resources = ec.frame.resources;
    TerrainToolSettings& brush     = ec.state.terrainTool;
    sectionLabel("Paint Layers");

    // A click on a layer picks it and the Paint tool.
    int         columns = 0;
    const float face    = std::min(tileEdge(EditorStyle::px(64.0f), columns), EditorStyle::px(96.0f));
    const int   perRow  = std::max(1, std::min<int>(columns, static_cast<int>(terrain.layers.size())));
    for (size_t l = 0; l < terrain.layers.size(); ++l) {
        if (l % static_cast<size_t>(perRow) != 0) ImGui::SameLine();
        const MaterialHandle layer = terrain.layers[l];
        const MaterialAsset* asset = resources.tryGet(layer);
        char id[32];
        std::snprintf(id, sizeof(id), "layer%zu", l);
        const std::string  caption = asset ? shortName(asset->name()) : std::string("(empty)");
        const bool         on      = brush.tool == TerrainTool::Paint && brush.layer == l;
        const GpuTextureId picture = albedoPicture(resources, ec.hooks, layer);
        const std::string  named   = asset ? asset->name() : std::string("No material");
        const std::string  tip     = named + "\nClick to paint it";
        const Tile tile{picture, albedoTint(resources, layer), EditorIcon::Material, static_cast<int>(l + 1)};
        if (pictureTile(id, tile, caption.c_str(), on, face, tip)) {
            brush.layer = static_cast<uint32_t>(l);
            brush.tool  = TerrainTool::Paint;
        }
    }

    // The picked layer's settings, whichever tool is up.
    const size_t      picked   = std::min<size_t>(brush.layer, terrain.layers.size() - 1);
    MaterialHandle    material = terrain.layers[picked];
    float             tiling   = terrain.layerTiling[picked];
    const std::string label    = "Layer " + std::to_string(picked + 1);
    ImGui::PushID("layer");
    if (pickAsset<MaterialAsset>("##LayerPick", label.c_str(), resources, material)) {
        edits.emplace_back([picked, material](TerrainAsset& a) { a.layers[picked] = material; });
    }
    const char* tileTooltip = "Metres one repeat of its maps spans";
    if (propDrag("Tile Size", &tiling, 0.05f, 0.1f, 1000.0f, "%.2f m", tileTooltip)) {
        edits.emplace_back([picked, tiling](TerrainAsset& a) { a.layerTiling[picked] = tiling; });
    }
    ImGui::PopID();
}

void TerrainEditorPanel::drawPlants(EditorContext& ec, const TerrainAsset& terrain, Edits& edits) {
    ResourceManager&     resources = ec.frame.resources;
    TerrainToolSettings& brush     = ec.state.terrainTool;
    sectionLabel("Plants");

    // A click on a plant picks it and the Plants tool; the last tile adds one.
    int          columns = 0;
    const float  face    = std::min(tileEdge(EditorStyle::px(64.0f), columns), EditorStyle::px(96.0f));
    const size_t count   = terrain.foliage.size();
    const size_t tiles   = count + (count < TerrainAsset::MAX_FOLIAGE ? 1 : 0);
    for (size_t f = 0; f < tiles; ++f) {
        if (f % static_cast<size_t>(columns) != 0) ImGui::SameLine();
        char id[32];
        std::snprintf(id, sizeof(id), "plant%zu", f);
        if (f == count) {
            const Tile add{0, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), EditorIcon::Plus, 0};
            if (pictureTile(id, add, "Add", false, face, "Add a plant layer")) {
                // A seed no layer has, or two would grow their plants on the same spots.
                edits.emplace_back([](TerrainAsset& a) {
                    uint32_t seed = 0;
                    for (const TerrainFoliage& layer : a.foliage) seed = std::max(seed, layer.seed + 1);
                    a.foliage.emplace_back().seed = seed;
                });
                brush.foliage = static_cast<uint32_t>(count);
                brush.tool    = TerrainTool::Plants;
            }
            continue;
        }
        const TerrainFoliage& plant    = terrain.foliage[f];
        const MaterialHandle  material = plant.parts.empty() ? MaterialHandle{} : plant.parts[0].material;
        const MeshHandle      meshName = plant.parts.empty() ? MeshHandle{} : plant.parts[0].mesh;
        const MeshAsset*      mesh     = resources.tryGet(meshName);
        const MaterialAsset*  look     = resources.tryGet(material);
        const std::string     caption  = mesh ? shortName(mesh->name()) : std::string("(no mesh)");
        const bool            on       = brush.tool == TerrainTool::Plants && brush.foliage == f;
        const std::string     tip      = (mesh ? mesh->name() : std::string("No mesh")) + "\n"
            + (look ? look->name() : std::string("No material")) + "\nClick to grow it";
        const Tile tile{
            albedoPicture(resources, ec.hooks, material),
            albedoTint(resources, material),
            EditorIcon::Plants,
            static_cast<int>(f + 1)
        };
        if (pictureTile(id, tile, caption.c_str(), on, face, tip)) {
            brush.foliage = static_cast<uint32_t>(f);
            brush.tool    = TerrainTool::Plants;
        }
    }
    if (terrain.foliage.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Add a plant layer, give it a mesh, then paint it on.");
        ImGui::PopStyleColor();
        return;
    }

    // The picked plant layer's settings: a copy edited, put back whole but for its painted density.
    // Another terrain, or an undone Add, can leave the pick past the end: the last layer, then.
    const size_t picked = std::min<size_t>(brush.foliage, count - 1);
    brush.foliage       = static_cast<uint32_t>(picked);
    TerrainFoliage plant = terrain.foliage[picked];
    if (plant.parts.empty()) plant.parts.emplace_back();
    bool edited = false;
    ImGui::PushID("plant");
    // Each part a mesh and a material, standing where the plant does: a trunk and its leaves.
    int removed = -1;
    for (size_t p = 0; p < plant.parts.size(); ++p) {
        ImGui::PushID(static_cast<int>(p));
        const bool        several = plant.parts.size() > 1;
        const std::string label   = several ? "Part " + std::to_string(p + 1) : std::string("Mesh");
        edited |= pickAsset<MeshAsset>("##PartMesh", label.c_str(), resources, plant.parts[p].mesh);
        edited |= pickAsset<MaterialAsset>("##PartMaterial", "Material", resources, plant.parts[p].material);
        if (several && ImGui::SmallButton("Remove Part")) removed = static_cast<int>(p);
        ImGui::PopID();
    }
    if (removed >= 0) {
        plant.parts.erase(plant.parts.begin() + removed);
        edited = true;
    }
    if (plant.parts.size() < TerrainAsset::MAX_PLANT_PARTS && ImGui::SmallButton("Add Part")) {
        plant.parts.emplace_back();
        edited = true;
    }
    const char* densityTooltip = "Plants a square metre where it is painted full";
    edited |= propDrag("Per m2", &plant.perSquareMetre, 0.01f, 0.0f, 100.0f, "%.2f", densityTooltip);
    edited |= propDrag("Min Scale", &plant.minScale, 0.01f, 0.05f, plant.maxScale, "%.2f");
    edited |= propDrag("Max Scale", &plant.maxScale, 0.01f, plant.minScale, 20.0f, "%.2f");
    edited |= propDrag("Draw Distance", &plant.maxDistance, 0.5f, 1.0f, 2000.0f, "%.0f m");
    const char* shadowTooltip = "Grass need not; a shrub looks afloat without";
    edited |= propCheckbox("Cast Shadows", &plant.castShadows, shadowTooltip);
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
    const bool remove = ImGui::Button("Remove Plant Layer");
    ImGui::PopStyleColor();
    ImGui::PopID();
    if (edited) {
        plant.density.clear();
        edits.emplace_back([picked, plant](TerrainAsset& a) {
            std::vector<uint8_t> density = std::move(a.foliage[picked].density);
            a.foliage[picked]         = plant;
            a.foliage[picked].density = std::move(density);
        });
    }
    if (remove) {
        edits.emplace_back([picked](TerrainAsset& a) {
            a.foliage.erase(a.foliage.begin() + static_cast<std::ptrdiff_t>(picked));
        });
        if (brush.foliage > 0 && brush.foliage + 1 >= count) --brush.foliage;
    }
}

void TerrainEditorPanel::drawGrid(const TerrainAsset& terrain, Edits& edits) {
    sectionLabel("Grid");
    float size = terrain.size;
    const char* sizeTooltip = "Metres a side; the ground stretches with it";
    if (propDrag("Size", &size, 0.5f, 8.0f, 8192.0f, "%.0f m", sizeTooltip)) {
        edits.emplace_back([size](TerrainAsset& a) { a.size = size; });
    }
    // Every valid resolution, from one chunk's to the most: each square of one cut in four.
    std::vector<uint32_t>    resolutions;
    std::vector<std::string> names;
    for (uint32_t r = TerrainAsset::MIN_RESOLUTION; r <= TerrainAsset::MAX_RESOLUTION; r = 2 * r - 1) {
        resolutions.push_back(r);
        names.push_back(std::to_string(r));
    }
    std::vector<const char*> labels;
    for (const std::string& n : names) labels.push_back(n.c_str());
    const auto at   = std::find(resolutions.begin(), resolutions.end(), terrain.resolution);
    int        pick = at == resolutions.end() ? 0 : static_cast<int>(at - resolutions.begin());
    const int   count             = static_cast<int>(labels.size());
    const char* resolutionTooltip = "Vertices a side; the shape, paint and plants carry over";
    if (propIndexCombo("Resolution", labels.data(), count, &pick, resolutionTooltip)) {
        const uint32_t to = resolutions[static_cast<size_t>(pick)];
        edits.emplace_back([to](TerrainAsset& a) { a.resample(to); });
    }
    ImGui::TextDisabled("Vertices %.2f m apart", terrain.spacing());
}

} // namespace Vkm::Engine
