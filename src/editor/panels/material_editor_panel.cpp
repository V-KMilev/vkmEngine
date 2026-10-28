#include "panels/material_editor_panel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include "core/fnv1a.h"
#include "io/asset/asset_serializer.h"
#include "core/system.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "resource/asset/material_asset.h"
#include "resource/asset_source_kind.h"
#include "resource/resource_manager.h"
#include "ui/asset_drag.h"
#include "ui/editor_icons.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"
#include "command/component_edit.h"
#include "command/editor_commands.h"
#include "editor_actions.h"
#include "session/material_preview_session.h"
#include "resource/generate/mesh_generators.h"
#include "resource/generate/texture_generators.h"
#include "io/project_paths.h"
#include "import/import_formats.h"
#include "import/material_loaders.h"
#include "import/texture_loaders.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_system.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief One row of the preview shape strip: its button, and what it draws on.
 *
 * A row with no @c mesh is the selection's own.
 */
struct PreviewShape {
    const char* id;              ///< ImGui id of the strip button.
    const char* label;           ///< Button tooltip.
    EditorIcon  icon;            ///< Glyph on the button.
    const char* mesh;            ///< Name the generated mesh is registered under; null = the selection's.
    MeshAsset (*make)();         ///< Builds it the first time it is asked for; null with @c mesh.
};

/**
 * @brief The mesh @p shape draws on, generated on first request.
 *
 * @param resources Where the generated mesh is registered.
 * @param shape     A row with a mesh of its own.
 * @return Its handle.
 */
MeshHandle previewMeshFor(ResourceManager& resources, const PreviewShape& shape);

MeshAsset previewSphere() { return generateSphere(); }
MeshAsset previewCube() { return generateCube(); }
MeshAsset previewPlane() { return generatePlane(2.0f, 2.0f, 1, 1); }

/// The window's width, design pixels, from which the preview and the parameters stand side by side.
constexpr float WIDE_FROM = 620.0f;

/// Narrower, the preview's height, design pixels: about a third of the window, within these.
constexpr float NARROW_LEAST = 160.0f;
constexpr float NARROW_MOST  = 320.0f;

constexpr PreviewShape SHAPES[] = {
    {"shSphere", "Sphere",      EditorIcon::Sphere, "mesh:preview_sphere", &previewSphere},
    {"shCube",   "Cube",        EditorIcon::Cube,   "mesh:preview_cube",   &previewCube},
    {"shPlane",  "Plane",       EditorIcon::Plane,  "mesh:preview_plane",  &previewPlane},
    {"shMesh",   "As selected", EditorIcon::Mesh,   nullptr,               nullptr},
};

MeshHandle previewMeshFor(ResourceManager& resources, const PreviewShape& shape) {
    MeshHandle h = resources.findByName<MeshAsset>(shape.mesh);
    if (!h) h = resources.addPrivate(shape.make(), shape.mesh);
    return h;
}

/**
 * @brief One texture slot: what the panel calls it, where it binds, how it is read, and what it does.
 */
struct MapDesc {
    const char* label;
    TextureHandle MaterialAsset::* member;
    TextureUsage usage;
    const char* hint;
};

constexpr MapDesc MAPS[] = {
    {
        "Albedo",
        &MaterialAsset::albedoTexture,
        TextureUsage::Color,
        "Base color. Its alpha is what AlphaMask cuts and Transparent blends on"
    },
    {
        "Normal",
        &MaterialAsset::normalTexture,
        TextureUsage::Normal,
        "Tangent-space normal map; the scale beside it sets how far it bends the surface"
    },
    {
        "Roughness",
        &MaterialAsset::roughnessTexture,
        TextureUsage::Data,
        "Roughness from the red channel, multiplied by the Roughness slider"
    },
    {
        "Metallic",
        &MaterialAsset::metallicTexture,
        TextureUsage::Data,
        "Metalness from the red channel, multiplied by the Metallic slider"
    },
    {
        "AO",
        &MaterialAsset::aoTexture,
        TextureUsage::Data,
        "Ambient occlusion from the red channel; darkens indirect light only"
    },
    {
        "Emission",
        &MaterialAsset::emissionTexture,
        TextureUsage::Color,
        "Emissive color, multiplied by the Emission tint"
    },
    {
        "Metal+Rough",
        &MaterialAsset::metallicRoughnessTexture,
        TextureUsage::Data,
        "glTF packing: roughness in green, metallic in blue. Bound, it is used instead of the separate "
        "Roughness and Metallic maps"
    },
    {
        "AO+M+R",
        &MaterialAsset::aoMetallicRoughnessTexture,
        TextureUsage::Data,
        "AO in red, roughness in green, metallic in blue. Bound, it is used instead of the three separate "
        "maps"
    },
    {
        "Height",
        &MaterialAsset::heightTexture,
        TextureUsage::Data,
        "Height field for parallax, white where the surface is high; needs a Height Scale above 0 to do "
        "anything"
    },
    {
        "Clearcoat",
        &MaterialAsset::clearcoatTexture,
        TextureUsage::Data,
        "Clearcoat strength mask; needs the Clearcoat feature"
    },
    {
        "Transmission",
        &MaterialAsset::transmissionTexture,
        TextureUsage::Data,
        "Transmission mask; needs the Transmission feature"
    },
};
// Order and wording are editorial (see VKM_MATERIAL_MAPS); a slot missing here cannot be bound.
static_assert(std::size(MAPS) == MATERIAL_MAP_COUNT, "every material map needs a row");

/**
 * @brief One secondary lobe, and everything the panel needs to offer it.
 *
 * Cards only while on: `active` reads the asset, not an unserialized flag; `enable` gives
 * a visible value; `disable` resets every field it owns - the texture too, or the card
 * would come straight back.
 */
struct Feature {
    const char*   name;
    const ImVec4* accent;
    const char*   hint;
    bool (*active)(const MaterialAsset& m);
    void (*enable)(MaterialAsset& m);
    void (*disable)(MaterialAsset& m);
    bool (*body)(MaterialAsset& m);
    TextureHandle MaterialAsset::* mask = nullptr;  ///< Its own map, shown in its card; null for none.
};

bool transmissionBody(MaterialAsset& m) {
    bool changed = propSlider(
        "Transmission",
        &m.transmission,
        0.0f,
        1.0f,
        "%.2f",
        "How much light refracts through instead of scattering off"
    );
    changed |= propDrag(
        "Thickness",
        &m.thicknessFactor,
        0.01f,
        0.0f,
        100.0f,
        "%.3f",
        "Volume thickness in metres. 0 = thin-walled, and nothing is absorbed"
    );
    changed |= propDrag(
        "Absorb Length",
        &m.attenuationDistance,
        0.01f,
        0.0001f,
        1000.0f,
        "%.3f",
        "Path length at which white light becomes the absorption color"
    );
    changed |= propColor3(
        "Absorb Tint",
        glm::value_ptr(m.attenuationColor),
        ImGuiColorEditFlags_Float,
        "What white light turns into over one absorption distance (white = clear)"
    );
    return changed;
}

bool clearcoatBody(MaterialAsset& m) {
    bool changed = propSlider("Strength", &m.clearcoat, 0.0f, 1.0f, "%.2f");
    changed |= propSlider(
        "Roughness",
        &m.clearcoatRoughness,
        0.0f,
        1.0f,
        "%.2f",
        "Roughness of the coat alone; the layer under it keeps its own"
    );
    return changed;
}

bool anisotropyBody(MaterialAsset& m) {
    bool changed = propSlider("Strength", &m.anisotropy, 0.0f, 1.0f, "%.2f");
    changed |= propDrag3(
        "Direction",
        glm::value_ptr(m.anisotropyDirection),
        0.01f,
        -1.0f,
        1.0f,
        "%.2f",
        "Which way the grain runs, in tangent space"
    );
    return changed;
}

bool subsurfaceBody(MaterialAsset& m) {
    bool changed = propSlider("Strength", &m.subsurface, 0.0f, 1.0f, "%.2f");
    changed |= propColor3(
        "Tint",
        glm::value_ptr(m.subsurfaceColor),
        ImGuiColorEditFlags_Float,
        "Color light picks up on its way back out"
    );
    return changed;
}

bool sheenBody(MaterialAsset& m) {
    bool changed = propColor3(
        "Color",
        glm::value_ptr(m.sheenColor),
        ImGuiColorEditFlags_Float,
        "Black is no sheen at all, which is how this feature switches off"
    );
    changed |= propSlider("Roughness", &m.sheenRoughness, 0.0f, 1.0f, "%.2f");
    return changed;
}

constexpr Feature FEATURES[] = {
    {
        "Transmission",
        &EditorStyle::Accent::MAT_GLASS,
        "Light through the surface, and what the volume behind it absorbs: glass, water, gems",
        [](const MaterialAsset& m) {
            return m.transmission > 0.0f || m.thicknessFactor > 0.0f || !!m.transmissionTexture;
        },
        [](MaterialAsset& m) { m.transmission = 1.0f; },
        [](MaterialAsset& m) {
            m.transmission        = 0.0f;
            m.thicknessFactor     = 0.0f;
            m.attenuationDistance = 1.0f;
            m.attenuationColor    = glm::vec3(1.0f);
            m.transmissionTexture = TextureHandle{};
        },
        &transmissionBody,
        &MaterialAsset::transmissionTexture,
    },
    {
        "Clearcoat",
        &EditorStyle::Accent::MAT_COAT,
        "A thin gloss layer over the base: car paint, lacquer, varnished wood",
        [](const MaterialAsset& m) { return m.clearcoat > 0.0f || !!m.clearcoatTexture; },
        [](MaterialAsset& m) { m.clearcoat = 1.0f; },
        [](MaterialAsset& m) {
            m.clearcoat          = 0.0f;
            m.clearcoatRoughness = 0.0f;
            m.clearcoatTexture   = TextureHandle{};
        },
        &clearcoatBody,
        &MaterialAsset::clearcoatTexture,
    },
    {
        "Anisotropy",
        &EditorStyle::Accent::MAT_ANISO,
        "Highlights stretched along a grain: brushed metal, hair, satin",
        [](const MaterialAsset& m) { return m.anisotropy != 0.0f; },
        [](MaterialAsset& m) { m.anisotropy = 0.5f; },
        [](MaterialAsset& m) {
            m.anisotropy          = 0.0f;
            m.anisotropyDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        },
        &anisotropyBody,
    },
    {
        "Subsurface",
        &EditorStyle::Accent::MAT_SSS,
        "Light entering and leaving somewhere else: skin, wax, leaves, marble",
        [](const MaterialAsset& m) { return m.subsurface > 0.0f; },
        [](MaterialAsset& m) { m.subsurface = 0.5f; },
        [](MaterialAsset& m) {
            m.subsurface      = 0.0f;
            m.subsurfaceColor = glm::vec3(1.0f);
        },
        &subsurfaceBody,
    },
    {
        "Sheen",
        &EditorStyle::Accent::MAT_SHEEN,
        "A soft rim at grazing angles: velvet, fabric, dust on a surface",
        [](const MaterialAsset& m) {
            return m.sheenColor.r > 0.0f || m.sheenColor.g > 0.0f || m.sheenColor.b > 0.0f;
        },
        [](MaterialAsset& m) { m.sheenColor = glm::vec3(0.5f); },
        [](MaterialAsset& m) {
            m.sheenColor     = glm::vec3(0.0f);
            m.sheenRoughness = 0.3f;
        },
        &sheenBody,
    },
};

/// The selected entity's Mesh, or null.
Mesh* selectedMesh(EditorContext& ec) {
    return ec.frame.scene.tryGet<Mesh>(ec.state.selectedEntity);
}

/**
 * @brief The selected entity's Mesh when it draws with @p material, or null.
 *
 * What replacing the window's material may write to: the material need not be the selection's.
 *
 * @param ec Holds the selection.
 * @param material The material the window shows.
 * @return The selection's Mesh, or null when it does not draw with @p material.
 */
Mesh* selectedMeshDrawing(EditorContext& ec, MaterialHandle material) {
    Mesh* mesh = selectedMesh(ec);
    return (mesh && material && mesh->material == material) ? mesh : nullptr;
}

/// What fills a slot: the bound texture's file name, or that it was generated.
std::string slotDetail(const ResourceManager& resources, const TextureHandle& slot) {
    if (!slot) return "(none)";
    const TextureAsset& texture = resources.get(slot);
    const std::string kind = texture.hasSource()
        ? texture.sourceJson().value(AssetSourceKey::KIND, std::string{})
        : std::string{};
    if (kind == AssetSourceKind::SOLID) return "(generated)";
    // An imported texture is named by its file.
    const std::string file = std::filesystem::path(texture.name()).filename().string();
    return file.empty() ? texture.name() : file;
}

/// The map row of @p member.
const MapDesc& mapOf(TextureHandle MaterialAsset::* member) {
    for (const MapDesc& map : MAPS) {
        if (map.member == member) return map;
    }
    return MAPS[0];
}

/// Whether @p mat's maps are laid as they are by default: one repeat over its UVs.
bool defaultUVs(const MaterialAsset& mat) {
    return mat.uvScale == glm::vec2(1.0f) && mat.uvOffset == glm::vec2(0.0f) && mat.boxMapping == 0.0f;
}

} // namespace

MaterialEditorPanel::MaterialEditorPanel(MaterialHandle material, uint64_t previewKey)
    : m_material(material)
    , m_viewport(previewKey)
{}

void MaterialEditorPanel::drawChooser(EditorContext& ec, float width) {
    ResourceManager& resources = ec.frame.resources;

    const std::string& name    = resources.get(m_material).name();
    const char*        preview = name.empty() ? "(unnamed)" : name.c_str();

    ImGui::SetNextItemWidth(width > 0.0f ? width : -1.0f);
    if (!beginCombo("##ChooseMat", preview, ImGuiComboFlags_HeightLarge)) return;

    popupSearchField("##matFilter", m_chooserFilter, sizeof(m_chooserFilter));

    // Snapshotted once so ImGuiListClipper can window the visible rows.
    std::vector<std::pair<MaterialHandle, const MaterialAsset*>> rows;
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle h, const MaterialAsset& a) {
        if (a.isHidden()) return;  // editor helpers (thumbnail neutral, and such) are not user-facing
        if (!matchesFilter(a.name().c_str(), m_chooserFilter)) return;
        rows.emplace_back(h, &a);
    });

    if (rows.empty()) {
        ImGui::TextDisabled("%s", m_chooserFilter[0] ? "Nothing matches." : "No materials yet.");
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& [handle, asset] = rows[i];
            ImGui::PushID(static_cast<int>(handle.id()));
            const bool current = handle == m_material;
            if (ImGui::Selectable(asset->name().empty() ? "(unnamed)" : asset->name().c_str(), current)) {
                ec.state.openMaterial(handle);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndCombo();
}

void MaterialEditorPanel::drawIdentityRow(EditorContext& ec) {
    EditorState&      state     = ec.state;
    ResourceManager&  resources = ec.frame.resources;
    Scene&            scene     = ec.frame.scene;
    const ImGuiStyle& style     = ImGui::GetStyle();

    // The entities drawing it, as the Asset Browser counts them: whatever names it, a terrain too.
    const std::array<uint64_t, 4> key = {
        m_material.id(),
        scene.epoch(),
        resources.revision(),
        state.commands.revision()
    };
    if (m_usersKey != key || ec.input.playing) {
        m_users.clear();
        scene.forEachEntity([&](EntityId id) {
            AssetSerializer::EntityAssetRefs refs;
            AssetSerializer::collectAssetRefs(scene, id, resources, refs);
            bool       draws = false;
            const auto match = [&](MaterialHandle material) { draws = draws || material == m_material; };
            AssetSerializer::forEachMaterialOf(refs, resources, match);
            if (draws) m_users.push_back(id);
        });
        m_usersKey = key;
    }
    const size_t userCount = m_users.size();

    char chip[32];
    if (userCount == 0) std::snprintf(chip, sizeof(chip), "unused");
    else std::snprintf(chip, sizeof(chip), "%zu user%s", userCount, userCount == 1 ? "" : "s");

    const float glyphW = ImGui::GetFrameHeight();
    const float menuW  = ImGui::GetFrameHeight();
    const float chipW  = ImGui::CalcTextSize(chip).x + style.FramePadding.x * 2.0f;
    const float comboW = ImGui::GetContentRegionAvail().x - glyphW - chipW - menuW
        - style.ItemSpacing.x * 3.0f;

    // The row opens on what kind of thing it is editing.
    inlineIcon(EditorIcon::Material, glyphW, ImGui::GetColorU32(EditorStyle::Accent::MAT_BASE));
    ImGui::SameLine();
    drawChooser(ec, comboW);

    ImGui::SameLine();
    ImGui::BeginDisabled(userCount == 0);
    if (ImGui::Button(chip, ImVec2(chipW, 0.0f))) {
        state.deselect();
        for (const EntityId id : m_users) {
            if (scene.isAlive(id)) state.addToSelection(id);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const char* chipTooltip = userCount == 0
            ? "Nothing in this project uses it"
            : "Select every entity using this material";
        ImGui::SetTooltip("%s", chipTooltip);
    }

    ImGui::SameLine();
    if (iconButton("matActions", EditorIcon::More, false, true, "Material actions", menuW)) {
        ImGui::OpenPopup("##matActions");
    }

    if (!ImGui::BeginPopup("##matActions")) return;

    const MaterialAsset& current = resources.get(m_material);
    sectionLabel(current.name().empty() ? "(unnamed)" : current.name().c_str());
    ImGui::Separator();

    if (ImGui::MenuItem("Duplicate")) {
        EditorActions::forkMaterial(scene, resources, state, state.selectedEntity, m_material);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fork it, so edits stop reaching the others");

    if (ImGui::MenuItem("Rename...")) {
        std::snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", current.name().c_str());
        m_renameOldName = current.name();
        m_renameOpen    = true;
    }

    ImGui::Separator();
    if (ImGui::MenuItem("New Material")) {
        if (MaterialHandle fresh = EditorActions::createNewMaterial(resources, state)) {
            state.openMaterial(fresh);
        }
    }
    if (ImGui::MenuItem("Load PBR Folder...")) openPbrFolder();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Build a material from a folder of maps named the usual way");
    }
    ImGui::EndPopup();
}

MeshHandle MaterialEditorPanel::previewMesh(ResourceManager& resources, const MeshHandle& entityMesh) {
    const size_t count = std::size(SHAPES);
    const PreviewShape& shape =
        SHAPES[(m_shape >= 0 && static_cast<size_t>(m_shape) < count) ? m_shape : 0];

    // The selection's row, falling back to the first with no selection.
    if (!shape.mesh) return entityMesh ? entityMesh : previewMeshFor(resources, SHAPES[0]);
    return previewMeshFor(resources, shape);
}

void MaterialEditorPanel::drawPreview(EditorContext& ec, MeshHandle entityMesh, glm::vec2 size) {
    ResourceManager& resources = ec.frame.resources;
    const MeshHandle shape     = previewMesh(resources, entityMesh);
    if (!shape) {
        ImGui::TextDisabled("(preview unavailable)");
        return;
    }

    // What it shows, stamped: the material as edited now, on the shape chosen.
    const uint64_t fields[] = {m_material.id(), resources.get(m_material).version(), shape.id()};
    const uint64_t version  = fnv1a64Bytes(fields, sizeof(fields));
    const ImVec2   room     = ImGui::GetContentRegionAvail();
    const ImVec2   extent(size.x > 0.0f ? size.x : room.x, size.y > 0.0f ? size.y : room.y);
    const ImVec2   origin   = ImGui::GetCursorScreenPos();
    m_viewport.draw(ec, {{shape, m_material}}, version, size);

    // The shapes over its foot, where the viewport's own strip, at its head, leaves room.
    const float height = ImGui::GetFrameHeight();
    const float pad    = EditorStyle::px(6.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + extent.y - height - pad));
    ImGui::BeginGroup();
    for (int i = 0; i < static_cast<int>(std::size(SHAPES)); ++i) {
        if (i > 0) ImGui::SameLine(0.0f, EditorStyle::px(3.0f));
        const bool  usable = SHAPES[i].mesh || !!entityMesh;
        const char* tip    = (!SHAPES[i].mesh && !entityMesh)
            ? "Select an entity with a mesh to preview on it"
            : SHAPES[i].label;
        if (iconButton(SHAPES[i].id, SHAPES[i].icon, m_shape == i, usable, tip, height)) m_shape = i;
    }
    ImGui::EndGroup();
    // Back under the picture, as an item of its size would leave the cursor.
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + extent.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

bool MaterialEditorPanel::textureSlot(
    ResourceManager& resources,
    EditorRenderHooks& hooks,
    MaterialAsset& mat,
    const char* label,
    TextureHandle MaterialAsset::* member,
    TextureUsage usage,
    const char* hint
) {
    TextureHandle& slot = mat.*member;
    bool changed = false;
    ImGui::PushID(label);

    // 0 when not resident yet or empty; the live preview syncs a bound slot within a frame.
    const GpuTextureId texId   = slot ? hooks.textureId(slot) : 0;
    const float        side    = ImGui::GetFrameHeight();
    const ImVec2       min     = ImGui::GetCursorScreenPos();
    const ImVec2       max(min.x + side, min.y + side);
    const bool         clicked = ImGui::InvisibleButton("##thumb", ImVec2(side, side));
    const bool         hovered = ImGui::IsItemHovered();

    ImDrawList* dl       = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    const ImGuiCol well = hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg;
    dl->AddRectFilled(min, max, ImGui::GetColorU32(well), rounding);
    if (texId) {
        // Loaded textures are flipped at decode, so the UVs unflip.
        dl->AddImageRounded(imTexture(texId), min, max, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, rounding);
    } else {
        // An empty slot wears the kind's glyph faintly.
        ImVec4 glyph = EditorStyle::Accent::MAT_TEXTURE;
        glyph.w      = hovered ? 0.6f : 0.25f;
        const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        drawEditorIcon(dl, EditorIcon::Texture, centre, side * 0.3f, ImGui::GetColorU32(glyph));
    }
    ImVec4 edge = EditorStyle::Accent::MAT_TEXTURE;
    if (!slot) edge = ImGui::GetStyleColorVec4(ImGuiCol_Border);
    dl->AddRect(min, max, ImGui::GetColorU32(edge), rounding);

    if (hovered) {
        ImGui::BeginTooltip();
        sectionLabel(label);
        if (texId) {
            const float big = EditorStyle::px(128.0f);
            ImGui::Image(imTexture(texId), ImVec2(big, big), ImVec2(0, 1), ImVec2(1, 0));
            const auto& t = resources.get(slot);
            ImGui::TextDisabled("%ux%u%s", t.params.width, t.params.height, t.isSrgb() ? "  sRGB" : "");
            ImGui::TextDisabled("%s", slotDetail(resources, slot).c_str());
        }
        ImGui::PushTextWrapPos(EditorStyle::px(320.0f));
        ImGui::TextDisabled("%s", hint);
        const char* action = slot
            ? "Click to replace, right-click for more, or drop a texture on it"
            : "Click to bind a texture, right-click for more, or drop one on it";
        ImGui::TextDisabled("%s", action);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (clicked) openTexturePicker(member, usage);

    // A texture dragged from the Assets panel binds where it lands, as it was loaded.
    if (ImGui::BeginDragDropTarget()) {
        const std::optional<AssetDrag> drag    = peekAssetDrag();
        const TextureHandle            dropped = drag ? TextureHandle{drag->key} : TextureHandle{};
        const bool takes = drag && drag->type == AssetType::Texture && resources.isAlive(dropped);
        if (takes && ImGui::AcceptDragDropPayload(ASSET_DRAG)) {
            slot    = findLoadedTexture(resources.get(dropped).name(), resources, usage);
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("##slot")) {
        sectionLabel(label);
        ImGui::Separator();
        if (ImGui::MenuItem(slot ? "Replace..." : "Bind Texture...")) openTexturePicker(member, usage);
        ImGui::BeginDisabled(!slot);
        if (ImGui::MenuItem("Clear")) {
            slot    = TextureHandle{};
            changed = true;
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextDisabled("Generate");
        const auto bind = [&](TextureHandle made) {
            slot    = made;
            changed = true;
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button("White"))  bind(generateWhiteTexture(resources));
        ImGui::SameLine();
        if (ImGui::Button("Black"))  bind(generateBlackTexture(resources));
        ImGui::SameLine();
        if (ImGui::Button("Normal")) bind(generateNormalTexture(resources));
        ImGui::SameLine();
        if (ImGui::Button("Grey"))   bind(generateGrayTexture(resources));

        const ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_Float
            | ImGuiColorEditFlags_AlphaPreviewHalf
            | ImGuiColorEditFlags_NoInputs;
        ImGui::ColorEdit4("##genCol", glm::value_ptr(m_genColor), colorFlags);
        ImGui::SameLine();
        if (ImGui::Button("Solid")) bind(createSolidColorTexture(m_genColor, resources, usage));
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

bool MaterialEditorPanel::drawParameters(
    ResourceManager& resources,
    EditorRenderHooks& hooks,
    MaterialAsset& mat
) {
    bool changed = false;

    // A map's thumbnail leads the row of the value it multiplies; @p value draws that value.
    const auto slotRow = [&](TextureHandle MaterialAsset::* member, const char* tooltip, auto&& value) {
        const MapDesc& map = mapOf(member);
        return propRow(map.label, tooltip, [&] {
            bool edited = textureSlot(resources, hooks, mat, map.label, map.member, map.usage, map.hint);
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::SetNextItemWidth(-FLT_MIN);
            edited |= value();
            return edited;
        });
    };
    // A map with no value of its own beside it: its thumbnail, then what fills it.
    const auto mapOnly = [&](TextureHandle MaterialAsset::* member) {
        return slotRow(member, mapOf(member).hint, [&] {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", slotDetail(resources, mat.*member).c_str());
            return false;
        });
    };

    if (beginComponentCard("Surface", EditorStyle::Accent::MAT_BASE, true)) {
        if (propEnumCombo("Type", mat.type)) {
            // Turn the discard on even if the asset had cutoff = 0 (off).
            if (mat.type == MaterialType::AlphaMask && mat.alphaCutoff <= 0.0f) mat.alphaCutoff = 0.5f;
            changed = true;
        }
        static const char* const FACES[] = {"Front only", "Both sides"};
        int faces = mat.doubleSided ? 1 : 0;
        if (propIndexCombo(
                "Faces",
                FACES,
                static_cast<int>(std::size(FACES)),
                &faces,
                "Both sides: a back face draws, lit as its own - cloth, leaves, single-plane cards"
            )) {
            mat.doubleSided = faces == 1;
            changed = true;
        }
        changed |= propEnumCombo(
            "Water",
            mat.water,
            "Body: a lake, a river or a pool - rippled, scattering its Subsurface color, floated "
            "on. Sea: the surface rides the scene's Ocean"
        );
        if (mat.water == WaterKind::Body) {
            changed |= propDrag(
                "Flow",
                &mat.flowSpeed,
                0.05f,
                -20.0f,
                20.0f,
                "%.2f m/s",
                "How fast it runs along the mesh's +U: a river's strip carries its water along its UVs"
            );
            changed |= propSlider(
                "Ripples",
                &mat.rippleScale,
                0.0f,
                1.0f,
                "%.2f",
                "How much of the scene's ripples it shows: 1 open water, toward 0 a sheltered pool"
            );
            changed |= propDrag(
                "Depth",
                &mat.waterDepth,
                0.1f,
                0.0f,
                1000.0f,
                "%.1f m",
                "How far below its surface the water reaches: deeper is under it, not in it - a pool "
                    "on a deck leaves what is under the deck dry"
            );
        }
        if (mat.type == MaterialType::AlphaMask) {
            changed |= propSlider(
                "Cutoff",
                &mat.alphaCutoff,
                0.0f,
                1.0f,
                "%.2f",
                "Fragments whose albedo alpha falls below this are dropped (foliage, leaves)"
            );
        }
        const char* iorTooltip =
            "How strongly a dielectric reflects head-on. 1.0 air, 1.33 water, 1.5 glass, 2.4 diamond";
        changed |= propDrag("IOR", &mat.ior, 0.01f, 1.0f, 3.0f, "%.2f", iorTooltip);
    }
    endComponentCard();

    if (beginComponentCard("Base", EditorStyle::Accent::MAT_BASE, true)) {
        const ImGuiColorEditFlags color = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf;
        changed |= slotRow(&MaterialAsset::albedoTexture, nullptr, [&] {
            return ImGui::ColorEdit4("##v", glm::value_ptr(mat.albedo), color);
        });
        changed |= slotRow(&MaterialAsset::metallicTexture, nullptr, [&] {
            return sliderFloat("##v", &mat.metallic, 0.0f, 1.0f, "%.2f");
        });
        changed |= slotRow(&MaterialAsset::roughnessTexture, nullptr, [&] {
            return sliderFloat("##v", &mat.roughness, 0.0f, 1.0f, "%.2f");
        });
        // A scale does nothing without its map, so it waits for one.
        changed |= slotRow(&MaterialAsset::normalTexture, "How far the normal map bends the surface", [&] {
            ImGui::BeginDisabled(!mat.normalTexture);
            const bool moved = ImGui::DragFloat(
                "##v",
                &mat.normalScale,
                0.01f,
                0.0f,
                5.0f,
                "scale %.2f",
                PROP_CLAMP
            );
            ImGui::EndDisabled();
            return moved;
        });
        changed |= slotRow(&MaterialAsset::aoTexture, "Scales indirect light over the whole material", [&] {
            return sliderFloat("##v", &mat.ao, 0.0f, 1.0f, "%.2f");
        });
        const ImGuiColorEditFlags glow = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;
        changed |= slotRow(&MaterialAsset::emissionTexture, "Light the material gives off on its own", [&] {
            return ImGui::ColorEdit3("##v", glm::value_ptr(mat.emission), glow);
        });
        // A multiplier on black is black, so the strength appears with the color.
        if (mat.emission.r > 0.0f || mat.emission.g > 0.0f || mat.emission.b > 0.0f) {
            changed |= propDrag(
                "Strength",
                &mat.emissiveStrength,
                0.05f,
                0.0f,
                64.0f,
                "%.2f",
                "HDR multiplier on the emission color; this is what drives bloom"
            );
        }
        changed |= slotRow(&MaterialAsset::heightTexture, "Parallax depth; 0.02 to 0.1 is typical", [&] {
            ImGui::BeginDisabled(!mat.heightTexture);
            const bool moved = ImGui::DragFloat(
                "##v",
                &mat.heightScale,
                0.001f,
                0.0f,
                0.5f,
                "depth %.3f",
                PROP_CLAMP
            );
            ImGui::EndDisabled();
            return moved;
        });
    }
    endComponentCard();

    // Packed maps stand in for the separate ones; the card starts folded when neither is bound.
    const bool packed = mat.metallicRoughnessTexture || mat.aoMetallicRoughnessTexture;
    if (beginComponentCard("Packed Maps", EditorStyle::Accent::MAT_TEXTURE, packed)) {
        changed |= mapOnly(&MaterialAsset::metallicRoughnessTexture);
        changed |= mapOnly(&MaterialAsset::aoMetallicRoughnessTexture);
    }
    endComponentCard();

    if (beginComponentCard("UV", EditorStyle::Accent::MAT_TEXTURE, !defaultUVs(mat))) {
        changed |= propDrag2(
            "Tiling",
            &mat.uvScale.x,
            0.05f,
            -10000.0f,
            10000.0f,
            "%.2f",
            "How many times the maps repeat across the mesh's UVs, on each axis. A texture must wrap "
            "(Repeat) to repeat"
        );
        changed |= propDrag2(
            "Offset",
            &mat.uvOffset.x,
            0.01f,
            -10000.0f,
            10000.0f,
            "%.3f",
            "How far the maps slide, in repeats"
        );
        changed |= propDrag(
            "Box Mapping",
            &mat.boxMapping,
            0.05f,
            0.0f,
            1000.0f,
            "%.2f m",
            "Metres one repeat spans, read off the object's own box at its drawn size, so a stretched "
            "cube keeps its texture square. 0 reads the mesh's UVs"
        );
    }
    endComponentCard();

    for (const Feature& feature : FEATURES) {
        if (!feature.active(mat)) continue;
        bool remove = false;
        if (beginComponentCard(feature.name, *feature.accent, true, &remove)) {
            changed |= feature.body(mat);
            if (feature.mask) changed |= mapOnly(feature.mask);
        }
        endComponentCard();
        if (remove) {
            feature.disable(mat);
            changed = true;
        }
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    const bool add = ImGui::Button("+  Add Feature", ImVec2(-1, 0));
    ImGui::PopStyleColor(2);
    if (add) ImGui::OpenPopup("##addFeature");

    if (ImGui::BeginPopup("##addFeature")) {
        sectionLabel("Add Feature");
        ImGui::Separator();
        int offered = 0;
        for (const Feature& feature : FEATURES) {
            if (feature.active(mat)) continue;
            ++offered;
            if (ImGui::MenuItem(feature.name)) {
                feature.enable(mat);
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", feature.hint);
        }
        if (offered == 0) ImGui::TextDisabled("This material already uses every one.");
        ImGui::EndPopup();
    }
    return changed;
}

void MaterialEditorPanel::openTexturePicker(TextureHandle MaterialAsset::* member, TextureUsage usage) {
    AssetPicker::Options options;
    options.title      = "Pick Texture";
    options.root       = ProjectPaths::assets();
    options.recursive  = true;
    options.extensions.assign(IMAGE_EXTENSIONS.begin(), IMAGE_EXTENSIONS.end());
    options.hint       = std::string("Read as ") + Reflect::enumName(usage);
    m_texturePicker.open(options);

    m_pendingSlot         = member;
    m_pendingTextureUsage = usage;
}

void MaterialEditorPanel::serviceTexturePicker(EditorContext& ec) {
    std::string picked;
    if (!m_texturePicker.draw(picked) || !m_pendingSlot) return;

    ResourceManager& resources = ec.frame.resources;
    const TextureHandle bound = loadTexture(picked, resources, m_pendingTextureUsage, true);
    if (bound) {
        const MaterialAsset before = resources.get(m_material);
        resources.edit(m_material).*m_pendingSlot = bound;
        resources.commit(m_material);
        auto bind = std::make_unique<MaterialEditCommand>(
            resources,
            m_material,
            before,
            resources.get(m_material),
            "Bind Texture"
        );
        ec.state.pushStep(std::move(bind));
    }
    m_pendingSlot = nullptr;
}

void MaterialEditorPanel::openPbrFolder() {
    AssetPicker::Options options;
    options.title = "Load PBR Folder";
    options.root  = ProjectPaths::assets();
    options.kind  = AssetPicker::Kind::Directories;
    m_pbrFolderPicker.open(options);
}

void MaterialEditorPanel::servicePbrFolder(EditorContext& ec) {
    std::string folder;
    if (!m_pbrFolderPicker.draw(folder)) return;

    MaterialHandle built = loadMaterialFromFolder(folder, ec.frame.resources);
    if (!built) return;

    // Through the scope, which records a prefab instance's override.
    if (selectedMeshDrawing(ec, m_material)) {
        EditScope<Mesh> mesh(
            ec.frame.scene,
            ec.frame.resources,
            ec.state,
            ec.state.selectedEntity,
            "Assign Material"
        );
        mesh->material = built;
    }
    ec.state.openMaterial(built);
}

void MaterialEditorPanel::draw(EditorContext& ec) {
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;

    // Drawn only while the material lives (AssetEditors), so there is always one to edit.
    const Mesh*      selMesh    = selectedMesh(ec);
    const MeshHandle entityMesh = (selMesh && selMesh->mesh) ? selMesh->mesh : MeshHandle{};

    drawIdentityRow(ec);
    ImGui::Spacing();

    // The parameters, which scroll on their own so the preview stays in view while they move.
    const auto parameters = [&] {
        const ImVec2 rowGap(ImGui::GetStyle().ItemSpacing.x, EditorStyle::px(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, rowGap);
        if (ImGui::BeginChild("##meParams", ImVec2(0, 0))) {
            // Live edit; the commit bumps the version. Copied every frame: a widget reports a
            // change only after making it.
            MaterialAsset&      mat    = resources.edit(m_material);
            const MaterialAsset before = mat;
            if (drawParameters(resources, ec.hooks, mat)) {
                resources.commit(m_material);
                auto edit = std::make_unique<MaterialEditCommand>(
                    resources,
                    m_material,
                    before,
                    mat,
                    "Edit Material"
                );
                state.pushStep(std::move(edit));
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    };

    const ImVec2 room = ImGui::GetContentRegionAvail();
    if (room.x >= EditorStyle::px(WIDE_FROM)) {
        // Wide: the preview fills the left, the parameters a column beside it, the split movable.
        const ImGuiTableFlags split = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("##meSplit", 2, split, ImVec2(0.0f, room.y))) {
            ImGui::TableSetupColumn("##preview", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("##params", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const float tall = room.y - ImGui::GetStyle().CellPadding.y * 2.0f;
            drawPreview(ec, entityMesh, glm::vec2(0.0f, tall));
            ImGui::TableNextColumn();
            parameters();
            ImGui::EndTable();
        }
    } else {
        // Narrow: the preview above, foldable, so the parameters can have the whole window.
        ImGui::SetNextItemOpen(m_open);
        m_open = ImGui::CollapsingHeader("Preview");
        if (m_open) {
            const float least = EditorStyle::px(NARROW_LEAST);
            const float tall  = std::clamp(room.y * 0.35f, least, EditorStyle::px(NARROW_MOST));
            drawPreview(ec, entityMesh, glm::vec2(0.0f, tall));
            ImGui::Spacing();
        }
        parameters();
    }

    // At panel scope: OpenPopup hashes against the calling window.
    serviceTexturePicker(ec);
    servicePbrFolder(ec);

    if (renameDialog("Rename Material", m_renameOpen, m_renameBuf, sizeof(m_renameBuf))) {
        const char* label = "Rename Material";
        EditorActions::renameAsset(resources, state, m_material, m_renameOldName, m_renameBuf, label);
    }
}

MeshHandle materialPreviewSphere(ResourceManager& resources) {
    return previewMeshFor(resources, SHAPES[0]);
}

MaterialHandle neutralPreviewMaterial(ResourceManager& resources) {
    const MaterialHandle found = resources.findByName<MaterialAsset>("mat:thumb_neutral");
    if (found) return found;
    MaterialAsset grey;
    grey.albedo    = glm::vec4(0.78f, 0.78f, 0.80f, 1.0f);
    grey.metallic  = 0.0f;
    grey.roughness = 0.55f;
    return resources.addPrivate(std::move(grey), "mat:thumb_neutral");
}

} // namespace Vkm::Engine
