#include "asset_editors/material_editor.h"

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

#include "base/hash/fnv1a.h"
#include "io/scene/asset_refs.h"
#include "core/system.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "resource/asset/material_asset.h"
#include "io/asset/recipe/asset_source.h"
#include "resource/resource_manager.h"
#include "ui/asset/asset_drag.h"
#include "ui/style/editor_icons.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"
#include "ui/rows.h"
#include "undo/component_edit.h"
#include "undo/asset_commands.h"
#include "actions/asset_actions.h"
#include "preview/preview_assets.h"
#include "preview/preview_cache.h"
#include "io/asset/generate/mesh_generators.h"
#include "io/asset/generate/texture_generators.h"
#include "io/project/roots.h"
#include "import/import_formats.h"
#include "import/material_import.h"
#include "import/texture_import.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_system.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief One row of the preview shape strip: its button, and what it draws on.
 */
struct PreviewShape {
    const char* id;     ///< ImGui id of the strip button.
    const char* label;  ///< Button tooltip.
    EditorIcon  icon;   ///< Glyph on the button.
    MeshHandle (*mesh)(ResourceManager&);  ///< The mesh it draws on, made on the first call and shared
                                           ///< after; null for the selection's.
};

/// A generated mesh found by @p name, registered privately on the first call.
MeshHandle sharedMesh(ResourceManager& resources, const char* name, MeshAsset (*make)()) {
    const MeshHandle found = resources.findByName<MeshAsset>(name);
    return found ? found : resources.addPrivate(make(), name);
}

MeshHandle previewCube(ResourceManager& resources) {
    return sharedMesh(resources, "mesh:preview_cube", [] { return generateCube(); });
}

MeshHandle previewPlane(ResourceManager& resources) {
    return sharedMesh(resources, "mesh:preview_plane", [] { return generatePlane({2.0f, 2.0f, 1, 1}); });
}

constexpr float WIDE_FROM = 620.0f;  ///< The window's width, design pixels, from which the preview and the
                                     ///< parameters stand side by side.

constexpr float NARROW_LEAST = 160.0f;  ///< Narrower, the preview's least height, design pixels: about a third
                                        ///< of the window, within this and NARROW_MOST.
constexpr float NARROW_MOST  = 320.0f;  ///< Narrower, the preview's most height, design pixels.

constexpr PreviewShape SHAPES[] = {
    {"shSphere", "Sphere",      EditorIcon::Sphere, &materialPreviewSphere},
    {"shCube",   "Cube",        EditorIcon::Cube,   &previewCube},
    {"shPlane",  "Plane",       EditorIcon::Plane,  &previewPlane},
    {"shMesh",   "As selected", EditorIcon::Mesh,   nullptr},
};

/**
 * @brief One texture slot: what the panel calls it, where it binds, how it is read, and what it does.
 */
struct MapDesc {
    const char*                    label;
    TextureHandle MaterialAsset::* member;
    TextureUsage                   usage;
    const char*                    hint;
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
 * @brief One secondary lobe, carded only while `active` reads it on in the asset.
 *
 * `enable` gives a visible value; `disable` resets every field it owns, the texture too, or the
 * card would come straight back.
 */
struct Feature {
    const char* name;
    const char* hint;
    bool (*active)(const MaterialAsset& m);
    void (*enable)(MaterialAsset& m);
    void (*disable)(MaterialAsset& m);
    void (*body)(Rows<MaterialAsset>& rows);
    TextureHandle MaterialAsset::* mask = nullptr;  ///< Its own map, shown in its card; null for none.
};

void transmissionBody(Rows<MaterialAsset>& rows) {
    rows.slider<&MaterialAsset::transmission>();
    rows.row<&MaterialAsset::thicknessFactor>("%.3f m");
    rows.row<&MaterialAsset::attenuationDistance>("%.3f m");
    rows.row<&MaterialAsset::attenuationColor>();
}

void clearcoatBody(Rows<MaterialAsset>& rows) {
    rows.slider<&MaterialAsset::clearcoat>();
    rows.slider<&MaterialAsset::clearcoatRoughness>();
}

void subsurfaceBody(Rows<MaterialAsset>& rows) {
    rows.slider<&MaterialAsset::subsurface>();
    rows.row<&MaterialAsset::subsurfaceColor>();
}

void sheenBody(Rows<MaterialAsset>& rows) {
    rows.row<&MaterialAsset::sheenColor>();
    rows.slider<&MaterialAsset::sheenRoughness>();
}

constexpr Feature FEATURES[] = {
    {
        "Transmission",
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
        "Subsurface",
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
    return ec.frame.scene.tryGet<Mesh>(ec.state.selection.active());
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

/// The map row of @p member; the first row when none binds it.
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

MaterialEditor::MaterialEditor(StorageIndex key)
    : AssetEditor(AssetType::Material, key)
    , m_material(MaterialHandle{key})
    , m_viewport(PreviewCache::viewportKey(AssetType::Material, key))
{}

const Resource* MaterialEditor::asset(const ResourceManager& resources) const {
    return resources.tryGet(MaterialHandle{key()});
}

void MaterialEditor::drawChooser(EditorContext& ec, float width) {
    ResourceManager& resources = ec.frame.resources;

    const std::string& name = resources.get(m_material).name();
    const char* preview     = name.empty() ? "(unnamed)" : name.c_str();

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

void MaterialEditor::drawIdentityRow(EditorContext& ec) {
    EditorState& state = ec.state;
    ResourceManager& resources = ec.frame.resources;
    Scene& scene = ec.frame.scene;
    const ImGuiStyle& style = ImGui::GetStyle();

    // The entities drawing it: whatever names it, a terrain too. Walked every frame in play, which
    // edits outside the history.
    const std::array<uint64_t, 4> key = {
        m_material.id(),
        scene.epoch(),
        resources.revision(),
        state.commands.revision()
    };
    if (m_usersKey != key || ec.input.playing) {
        m_users.clear();
        scene.forEachEntity([&](EntityId id) {
            SceneAssets::EntityAssetRefs refs;
            SceneAssets::collectAssetRefs(scene, id, resources, refs);
            bool draws = false;
            const auto match = [&](MaterialHandle material) { draws = draws || material == m_material; };
            SceneAssets::forEachMaterialOf(refs, resources, match);
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

    inlineIcon(EditorIcon::Material, glyphW, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine();
    drawChooser(ec, comboW);

    ImGui::SameLine();
    ImGui::BeginDisabled(userCount == 0);
    if (ImGui::Button(chip, ImVec2(chipW, 0.0f))) {
        state.selection.clear();
        for (const EntityId id : m_users) {
            if (scene.isAlive(id)) state.selection.add(id);
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
        EditorActions::forkMaterial(scene, resources, state, state.selection.active(), m_material);
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

MeshHandle MaterialEditor::previewMesh(ResourceManager& resources, const MeshHandle& entityMesh) {
    const size_t count = std::size(SHAPES);
    const PreviewShape& shape =
        SHAPES[(m_shape >= 0 && static_cast<size_t>(m_shape) < count) ? m_shape : 0];

    if (!shape.mesh) return entityMesh ? entityMesh : SHAPES[0].mesh(resources);
    return shape.mesh(resources);
}

void MaterialEditor::drawPreview(EditorContext& ec, MeshHandle entityMesh, glm::vec2 size) {
    ResourceManager& resources = ec.frame.resources;
    const MeshHandle shape     = previewMesh(resources, entityMesh);
    if (!shape) {
        ImGui::TextDisabled("(preview unavailable)");
        return;
    }

    // What it shows, stamped: the material as edited now, on the shape chosen.
    const uint64_t fields[] = {m_material.id(), resources.get(m_material).version(), shape.id()};
    const uint64_t version  = fnv1a64Bytes(fields, sizeof(fields));
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const ImVec2   extent(size.x > 0.0f ? size.x : room.x, size.y > 0.0f ? size.y : room.y);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
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

bool MaterialEditor::textureSlot(
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
    const GpuTextureId texId = slot ? hooks.textureId(slot) : 0;
    const float side = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2       max(min.x + side, min.y + side);
    const bool clicked = ImGui::InvisibleButton("##thumb", ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    const ImGuiCol well = hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg;
    dl->AddRectFilled(min, max, ImGui::GetColorU32(well), rounding);
    if (texId) {
        // Loaded textures are flipped at decode, so the UVs unflip.
        dl->AddImageRounded(imTexture(texId), min, max, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, rounding);
    } else {
        ImVec4 glyph = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        glyph.w = hovered ? 0.6f : 0.25f;
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        drawEditorIcon(dl, EditorIcon::Texture, center, side * 0.3f, ImGui::GetColorU32(glyph));
    }
    const ImVec4 edge = ImGui::GetStyleColorVec4(slot ? ImGuiCol_TextDisabled : ImGuiCol_Border);
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
        const std::optional<AssetDrag> drag = peekAssetDrag();
        const TextureHandle dropped = drag ? TextureHandle{drag->key} : TextureHandle{};
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
        if (ImGui::Button("Gray"))   bind(generateGrayTexture(resources));

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

bool MaterialEditor::drawParameters(
    ResourceManager& resources,
    EditorRenderHooks& hooks,
    MaterialAsset& mat
) {
    bool changed = false;
    Rows<MaterialAsset> rows(mat, {}, nullptr);

    // A map's thumbnail leads the row of the value it multiplies; @p value draws that value.
    const auto slotFor = [&](TextureHandle MaterialAsset::* member, auto&& value) {
        const MapDesc& map = mapOf(member);
        bool edited = textureSlot(resources, hooks, mat, map.label, map.member, map.usage, map.hint);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(-FLT_MIN);
        edited |= value();
        return edited;
    };
    // A map with no value of its own beside it: its thumbnail, then what fills it.
    const auto mapOnly = [&](TextureHandle MaterialAsset::* member) {
        const MapDesc& map = mapOf(member);
        return propRow(map.label, map.hint, [&] {
            return slotFor(member, [&] {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", slotDetail(resources, mat.*member).c_str());
                return false;
            });
        });
    };

    if (beginComponentCard("Surface", EditorIcon::None, true)) {
        // Turn the discard on even if the asset had cutoff = 0 (off).
        const bool retyped = rows.row<&MaterialAsset::type>();
        if (retyped && mat.type == MaterialType::AlphaMask && mat.alphaCutoff <= 0.0f) mat.alphaCutoff = 0.5f;
        rows.row<&MaterialAsset::doubleSided>();
        if (mat.type == MaterialType::AlphaMask) rows.slider<&MaterialAsset::alphaCutoff>();
        rows.row<&MaterialAsset::ior>();
    }
    endComponentCard();

    if (beginComponentCard("Base", EditorIcon::None, true)) {
        const ImGuiColorEditFlags color = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf;
        rows.rowWith<&MaterialAsset::albedo>([&](glm::vec4& albedo, const FieldInfo&) {
            return slotFor(&MaterialAsset::albedoTexture, [&] {
                return ImGui::ColorEdit4("##v", glm::value_ptr(albedo), color);
            });
        });
        rows.rowWith<&MaterialAsset::metallic>([&](float& metallic, const FieldInfo& info) {
            return slotFor(&MaterialAsset::metallicTexture, [&] {
                return sliderFloat("##v", &metallic, info.min, info.max, "%.2f");
            });
        });
        rows.rowWith<&MaterialAsset::roughness>([&](float& roughness, const FieldInfo& info) {
            return slotFor(&MaterialAsset::roughnessTexture, [&] {
                return sliderFloat("##v", &roughness, info.min, info.max, "%.2f");
            });
        });
        // A scale does nothing without its map, so it waits for one.
        rows.rowWith<&MaterialAsset::normalScale>([&](float& scale, const FieldInfo& info) {
            return slotFor(&MaterialAsset::normalTexture, [&] {
                ImGui::BeginDisabled(!mat.normalTexture);
                const bool moved = editFloat(info, scale, "scale %.2f", Shown::Plain);
                ImGui::EndDisabled();
                return moved;
            });
        });
        rows.rowWith<&MaterialAsset::ao>([&](float& ao, const FieldInfo& info) {
            return slotFor(&MaterialAsset::aoTexture, [&] {
                return sliderFloat("##v", &ao, info.min, info.max, "%.2f");
            });
        });
        const ImGuiColorEditFlags glow = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;
        rows.rowWith<&MaterialAsset::emission>([&](glm::vec3& emission, const FieldInfo&) {
            return slotFor(&MaterialAsset::emissionTexture, [&] {
                return ImGui::ColorEdit3("##v", glm::value_ptr(emission), glow);
            });
        });
        // A multiplier on black is black, so the strength appears with the color.
        if (mat.emission.r > 0.0f || mat.emission.g > 0.0f || mat.emission.b > 0.0f) {
            rows.row<&MaterialAsset::emissiveStrength>();
        }
    }
    endComponentCard();

    // Packed maps stand in for the separate ones; the card starts folded when neither is bound.
    const bool packed = mat.metallicRoughnessTexture || mat.aoMetallicRoughnessTexture;
    if (beginComponentCard("Packed Maps", EditorIcon::None, packed)) {
        changed |= mapOnly(&MaterialAsset::metallicRoughnessTexture);
        changed |= mapOnly(&MaterialAsset::aoMetallicRoughnessTexture);
    }
    endComponentCard();

    if (beginComponentCard("UV", EditorIcon::None, !defaultUVs(mat))) {
        rows.row<&MaterialAsset::uvScale>();
        rows.row<&MaterialAsset::uvOffset>("%.3f");
        rows.row<&MaterialAsset::boxMapping>("%.2f m");
    }
    endComponentCard();

    for (const Feature& feature : FEATURES) {
        if (!feature.active(mat)) continue;
        bool remove = false;
        if (beginComponentCard(feature.name, EditorIcon::None, true, "##FeatureMenu")) {
            feature.body(rows);
            if (feature.mask) changed |= mapOnly(feature.mask);
        }
        if (ImGui::BeginPopup("##FeatureMenu")) {
            if (ImGui::MenuItem("Remove Feature")) remove = true;
            ImGui::EndPopup();
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
    return changed || rows.changed();
}

void MaterialEditor::openTexturePicker(TextureHandle MaterialAsset::* member, TextureUsage usage) {
    FilePicker::Options options;
    options.title      = "Pick Texture";
    options.root       = Roots::assets();
    options.recursive  = true;
    options.extensions.assign(IMAGE_EXTENSIONS.begin(), IMAGE_EXTENSIONS.end());
    options.hint = std::string("Read as ") + Reflect::enumName(usage);
    m_texturePicker.open(options);

    m_pendingSlot         = member;
    m_pendingTextureUsage = usage;
}

void MaterialEditor::serviceTexturePicker(EditorContext& ec) {
    std::string picked;
    if (!m_texturePicker.draw(picked) || !m_pendingSlot) return;

    ResourceManager& resources = ec.frame.resources;
    const TextureHandle bound  = loadTexture(picked, resources, m_pendingTextureUsage, true);
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

void MaterialEditor::openPbrFolder() {
    FilePicker::Options options;
    options.title = "Load PBR Folder";
    options.root  = Roots::assets();
    options.kind  = FilePicker::Kind::Directories;
    m_pbrFolderPicker.open(options);
}

void MaterialEditor::servicePbrFolder(EditorContext& ec) {
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
            ec.state.selection.active(),
            "Assign Material"
        );
        mesh->material = built;
    }
    ec.state.openMaterial(built);
}

void MaterialEditor::draw(EditorContext& ec) {
    EditorState& state = ec.state;
    ResourceManager& resources = ec.frame.resources;

    // Drawn only while the material lives (AssetEditors), so there is always one to edit.
    const Mesh* selMesh = selectedMesh(ec);
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
            MaterialAsset& mat = resources.edit(m_material);
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
        if (foldRow("Preview", m_open)) {
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

} // namespace Vkm::Engine
