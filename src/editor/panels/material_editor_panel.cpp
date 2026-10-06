#include "panels/material_editor_panel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include "core/fnv1a.h"
#include "ecs/component/render/decal.h"
#include "core/system.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "resource/asset/material_asset.h"
#include "resource/asset_source_kind.h"
#include "resource/resource_manager.h"
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
#include "import/material_loaders.h"
#include "import/texture_loaders.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_system.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {

// Everything that changes the preview image, in one stamp, so MaterialPreviewSession
// re-bakes only on a change.
uint64_t previewVersion(
    uint64_t materialVersion,
    uint32_t shapeId,
    int shape,
    float yaw,
    float pitch,
    float distance,
    int background,
    float lightYaw
) {
    auto floatBits = [](float f) {
        uint32_t b;
        std::memcpy(&b, &f, sizeof(b));
        return static_cast<uint64_t>(b);
    };
    uint64_t h = FNV1A_OFFSET_BASIS;
    const uint64_t fields[] = {
        materialVersion,
        static_cast<uint64_t>(shapeId),
        static_cast<uint64_t>(shape),
        floatBits(yaw),
        floatBits(pitch),
        floatBits(distance),
        static_cast<uint64_t>(background),
        floatBits(lightYaw)
    };
    for (uint64_t v : fields) {
        h = fnv1a64Bytes(&v, sizeof(v), h);
    }
    return h;
}

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
 * @brief One texture slot as the map grid draws it.
 *
 * A `core` slot always has a tile; the rest get one when bound or picked from the add
 * tile's menu.
 */
struct MapDesc {
    const char* label;
    TextureHandle MaterialAsset::* member;
    TextureUsage usage;
    bool        core;
    const char* hint;
};

constexpr MapDesc MAPS[] = {
    {
        "Albedo",
        &MaterialAsset::albedoTexture,
        TextureUsage::Color,
        true,
        "Base colour. Its alpha is what AlphaMask cuts and Transparent blends on"
    },
    {
        "Normal",
        &MaterialAsset::normalTexture,
        TextureUsage::Normal,
        true,
        "Tangent-space normal map; Normal Scale below sets how far it bends the surface"
    },
    {
        "Roughness",
        &MaterialAsset::roughnessTexture,
        TextureUsage::Data,
        true,
        "Roughness from the red channel, multiplied by the Roughness slider"
    },
    {
        "Metallic",
        &MaterialAsset::metallicTexture,
        TextureUsage::Data,
        true,
        "Metalness from the red channel, multiplied by the Metallic slider"
    },
    {
        "AO",
        &MaterialAsset::aoTexture,
        TextureUsage::Data,
        true,
        "Ambient occlusion from the red channel; darkens indirect light only"
    },
    {
        "Emission",
        &MaterialAsset::emissionTexture,
        TextureUsage::Color,
        true,
        "Emissive colour, multiplied by the Emission tint"
    },
    {
        "Metal+Rough",
        &MaterialAsset::metallicRoughnessTexture,
        TextureUsage::Data,
        false,
        "glTF packing: roughness in green, metallic in blue. Bound, it is used instead of the separate "
        "Roughness and Metallic maps"
    },
    {
        "AO+M+R",
        &MaterialAsset::aoMetallicRoughnessTexture,
        TextureUsage::Data,
        false,
        "AO in red, roughness in green, metallic in blue. Bound, it is used instead of the three separate "
        "maps"
    },
    {
        "Height",
        &MaterialAsset::heightTexture,
        TextureUsage::Data,
        false,
        "Height field for parallax, white where the surface is high; needs a Height Scale above 0 to do "
        "anything"
    },
    {
        "Clearcoat",
        &MaterialAsset::clearcoatTexture,
        TextureUsage::Data,
        false,
        "Clearcoat strength mask; needs the Clearcoat feature"
    },
    {
        "Transmission",
        &MaterialAsset::transmissionTexture,
        TextureUsage::Data,
        false,
        "Transmission mask; needs the Transmission feature"
    },
};
// Order and wording are editorial (see VKM_MATERIAL_MAPS); a slot with no tile is unreachable.
static_assert(std::size(MAPS) == MATERIAL_MAP_COUNT, "every material map needs a tile in the Maps grid");

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
        "Absorb Dist.",
        &m.attenuationDistance,
        0.01f,
        0.0001f,
        1000.0f,
        "%.3f",
        "Path length at which white light becomes the absorption colour"
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
        "Colour light picks up on its way back out"
    );
    return changed;
}

bool sheenBody(MaterialAsset& m) {
    bool changed = propColor3(
        "Colour",
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
 * What replacing the tab's material may write to: a pinned material need not be the
 * selection's.
 *
 * @param ec Holds the selection.
 * @param material The material the tab shows.
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
    const std::string kind = texture.hasSource() ? texture.sourceJson().value("kind", std::string{}) : "";
    if (kind == AssetSourceKind::SOLID) return "(generated)";
    // An imported texture is named by its file.
    const std::string file = std::filesystem::path(texture.name()).filename().string();
    return file.empty() ? texture.name() : file;
}

} // namespace

MaterialHandle MaterialEditorPanel::resolveTarget(EditorContext& ec) {
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;
    const Mesh*      selMesh   = selectedMesh(ec);

    // The asset can be deleted under a chosen target, and get() / edit() below are unguarded.
    if (state.materialEditorTarget && !resources.isAlive(state.materialEditorTarget)) {
        state.materialEditorTarget = {};
    }

    // A hand-picked material holds until another entity with one is selected; see
    // docs/reference/editor.md, "Which material the tab edits".
    if (state.materialEditorTarget && state.selectedEntity != state.materialPinnedAt
        && selMesh && selMesh->material) {
        state.materialEditorTarget = {};
        state.materialPinnedAt     = {};
    }

    if (state.materialEditorTarget) return state.materialEditorTarget;
    return (selMesh && selMesh->material) ? selMesh->material : MaterialHandle{};
}

void MaterialEditorPanel::drawChooser(EditorContext& ec, MaterialHandle target, float width) {
    ResourceManager& resources = ec.frame.resources;

    const char* preview = "(choose a material)";
    if (target) {
        const std::string& name = resources.get(target).name();
        preview = name.empty() ? "(unnamed)" : name.c_str();
    }

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
            const bool current = handle == target;
            if (ImGui::Selectable(asset->name().empty() ? "(unnamed)" : asset->name().c_str(), current)) {
                ec.state.openMaterial(handle);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndCombo();
}

void MaterialEditorPanel::drawIdentityRow(EditorContext& ec, MaterialHandle target) {
    EditorState&      state     = ec.state;
    ResourceManager&  resources = ec.frame.resources;
    Scene&            scene     = ec.frame.scene;
    const ImGuiStyle& style     = ImGui::GetStyle();

    // Users of this material: counted each frame, gathered only on the click that selects them.
    const auto forEachUser = [&](auto&& visit) {
        scene.forEach<Mesh>([&](EntityId id, const Mesh& mesh) {
            if (mesh.material == target) visit(id);
        });
        scene.forEach<Decal>([&](EntityId id, const Decal& decal) {
            if (decal.material == target) visit(id);
        });
    };
    size_t userCount = 0;
    forEachUser([&](EntityId) { ++userCount; });

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
    drawChooser(ec, target, comboW);

    ImGui::SameLine();
    ImGui::BeginDisabled(userCount == 0);
    if (ImGui::Button(chip, ImVec2(chipW, 0.0f))) {
        state.deselect();
        forEachUser([&](EntityId id) { state.addToSelection(id); });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const char* chipTooltip = userCount == 0
            ? "Nothing in this project uses it"
            : "Select every entity using this material";
        ImGui::SetTooltip("%s", chipTooltip);
    }

    ImGui::SameLine();
    if (ImGui::Button("...", ImVec2(menuW, 0.0f))) ImGui::OpenPopup("##matActions");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Material actions");

    if (!ImGui::BeginPopup("##matActions")) return;

    const MaterialAsset& current = resources.get(target);
    sectionLabel(current.name().empty() ? "(unnamed)" : current.name().c_str());
    ImGui::Separator();

    if (ImGui::MenuItem("Duplicate")) {
        if (MaterialHandle copy = EditorActions::duplicateMaterial(resources, target)) {
            // Through the scope: it pushes the step and records a prefab instance's override.
            if (selectedMeshDrawing(ec, target)) {
                EditScope<Mesh> mesh(scene, resources, state, state.selectedEntity, "Duplicate Material");
                mesh->material = copy;
            }
            state.openMaterial(copy);
        }
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

void MaterialEditorPanel::drawPreview(EditorContext& ec, MaterialHandle target, MeshHandle entityMesh) {
    ResourceManager& resources = ec.frame.resources;

    const float avail = ImGui::GetContentRegionAvail().x;
    const float room  = ImGui::GetContentRegionAvail().y * 0.55f;
    const float face  = std::clamp(std::min(avail, room), EditorStyle::px(140.0f), EditorStyle::px(460.0f));
    const float inset = (avail - face) * 0.5f;

    const MeshHandle shape = previewMesh(resources, entityMesh);
    uint32_t tex = 0;
    if (shape) {
        // Key 0, which previewKey never makes, so later thumbnails this frame cannot overwrite it
        // before ImGui samples at Render.
        PreviewRequest req;
        req.key         = 0ull;
        req.mesh        = shape;
        req.material    = target;
        req.yawDeg      = m_yaw;
        req.pitchDeg    = m_pitch;
        req.distance    = m_distance;
        req.background  = m_background;
        req.lightYawDeg = m_lightYaw;
        const uint64_t version = previewVersion(
            resources.get(target).version(),
            shape.id(),
            m_shape,
            m_yaw,
            m_pitch,
            m_distance,
            static_cast<int>(m_background),
            m_lightYaw
        );
        const bool live = true;
        tex = ec.materialPreviews.texture(resources, req, version, live);
    }

    if (!tex) {
        ImGui::TextDisabled("(preview unavailable)");
        return;
    }

    if (inset > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inset);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList*  dl     = ImGui::GetWindowDrawList();

    ImGui::Image(imTexture(tex), ImVec2(face, face), ImVec2(0, 1), ImVec2(1, 0));
    dl->AddRect(
        origin,
        ImVec2(origin.x + face, origin.y + face),
        ImGui::GetColorU32(ImGuiCol_Border),
        EditorStyle::px(3.0f)
    );

    // The orbit drag owns the active id, so the window never moves; controls over it answer first.
    ImGui::SetCursorScreenPos(origin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##orbit", ImVec2(face, face));
    const ImVec2 below = ImGui::GetCursorScreenPos();

    if (ImGui::IsItemActive()) {
        const ImVec2 drag = ImGui::GetIO().MouseDelta;
        m_yaw   -= drag.x * 0.4f;
        m_pitch  = std::clamp(m_pitch + drag.y * 0.4f, -85.0f, 85.0f);
    }
    if (ImGui::IsItemHovered()) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) m_distance = std::clamp(m_distance - wheel * 0.25f, 0.6f, 12.0f);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_yaw      = 35.0f;
            m_pitch    = 20.0f;
            m_distance = 3.0f;
        }
    }
    // Delayed, so it never flashes up mid-orbit.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive()) {
        ImGui::SetTooltip("Drag to orbit, scroll to zoom, double-click to reset");
    }

    const float height = ImGui::GetFrameHeight();
    const float pad    = EditorStyle::px(6.0f);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + face - height - pad));
    ImGui::BeginGroup();
    for (int i = 0; i < static_cast<int>(std::size(SHAPES)); ++i) {
        if (i > 0) ImGui::SameLine(0.0f, EditorStyle::px(3.0f));
        const bool usable = SHAPES[i].mesh || !!entityMesh;
        const char* tip = (!SHAPES[i].mesh && !entityMesh)
            ? "Select an entity with a mesh to preview on it"
            : SHAPES[i].label;
        if (iconButton(SHAPES[i].id, SHAPES[i].icon, m_shape == i, usable, tip, height)) {
            m_shape = i;
        }
    }
    ImGui::EndGroup();

    // The enum's own names, so the strip matches the renderer.
    const char* backdrop = Reflect::enumName(m_background);
    const float backW    = ImGui::CalcTextSize(backdrop).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + face - backW - pad, origin.y + face - height - pad));
    if (ImGui::Button(backdrop, ImVec2(backW, height))) {
        const int next = (static_cast<int>(m_background) + 1) % static_cast<int>(PreviewBackground::Count);
        m_background = static_cast<PreviewBackground>(next);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Backdrop the preview is lit against");

    ImGui::SetCursorScreenPos(below);
    if (inset > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inset);
    ImGui::SetNextItemWidth(face);
    sliderFloat("##light", &m_lightYaw, 0.0f, 360.0f, "Light %.0f deg", 0);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Swing the key light around the material");
    }
}

bool MaterialEditorPanel::mapTile(
    ResourceManager& resources,
    EditorRenderHooks* backend,
    MaterialHandle owner,
    MaterialAsset& mat,
    const char* label,
    TextureHandle MaterialAsset::* member,
    TextureUsage usage,
    const char* hint,
    float face
) {
    TextureHandle& slot = mat.*member;
    bool changed = false;

    ImGui::PushID(label);
    ImGui::BeginGroup();

    // 0 when not resident yet or empty; the live preview syncs a bound slot within a frame.
    const GpuTextureId texId = (slot && backend) ? backend->textureId(slot) : 0;

    // An empty slot wears the kind's glyph faintly.
    ImVec4 glyph = EditorStyle::Accent::MAT_TEXTURE;
    glyph.w = slot ? 0.30f : 0.16f;
    ImVec2 faceMin, faceMax;
    const bool clicked = tileFace(
        imTexture(texId),
        face,
        EditorStyle::Accent::MAT_TEXTURE,
        EditorIcon::Texture,
        glyph,
        faceMin,
        faceMax
    );

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        sectionLabel(label);
        if (texId) {
            const float big = EditorStyle::px(128.0f);
            // Loaded textures are flipped at decode, so the UVs unflip.
            ImGui::Image(imTexture(texId), ImVec2(big, big), ImVec2(0, 1), ImVec2(1, 0));
            const auto& t = resources.get(slot);
            ImGui::TextDisabled("%ux%u%s", t.params.width, t.params.height, t.isSrgb() ? "  sRGB" : "");
            ImGui::TextDisabled("%s", t.name().c_str());
        }
        ImGui::PushTextWrapPos(EditorStyle::px(320.0f));
        ImGui::TextDisabled("%s", hint);
        const char* action = slot
            ? "Click to replace, right-click for more"
            : "Click to bind a texture, right-click for more";
        ImGui::TextDisabled("%s", action);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (clicked) openTexturePicker(owner, member, usage);

    if (ImGui::BeginPopupContextItem("##slot")) {
        sectionLabel(label);
        ImGui::Separator();
        if (ImGui::MenuItem(slot ? "Replace..." : "Bind Texture...")) {
            openTexturePicker(owner, member, usage);
        }
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

        const ImGuiColorEditFlags colourFlags = ImGuiColorEditFlags_Float
            | ImGuiColorEditFlags_AlphaPreviewHalf
            | ImGuiColorEditFlags_NoInputs;
        ImGui::ColorEdit4("##genCol", glm::value_ptr(m_genColor), colourFlags);
        ImGui::SameLine();
        if (ImGui::Button("Solid")) bind(createSolidColorTexture(m_genColor, resources, usage));
        ImGui::EndPopup();
    }

    clippedLine(label, face, false);
    clippedLine(slotDetail(resources, slot).c_str(), face, true);
    ImGui::EndGroup();

    ImVec4 strip = EditorStyle::Accent::MAT_TEXTURE;
    if (!slot) strip.w *= 0.30f;
    tileStrip(faceMin, faceMax, strip);

    ImGui::PopID();
    return changed;
}

bool MaterialEditorPanel::drawMaps(
    ResourceManager& resources,
    EditorRenderHooks* backend,
    MaterialHandle target,
    MaterialAsset& mat
) {
    const float face = EditorStyle::px(84.0f);
    const float gap     = EditorStyle::px(8.0f);
    const float step    = face + gap;
    const int   columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + gap) / step));

    bool changed = false;
    int  shown   = 0;
    int  hidden  = 0;
    for (const MapDesc& map : MAPS) {
        const bool bound = !!(mat.*(map.member));
        if (!map.core && !bound) {
            ++hidden;
            continue;
        }
        // Before the tile: a SameLine left pending would pull the card's next row up beside it.
        if (shown % columns != 0) ImGui::SameLine(0.0f, gap);
        else if (shown > 0)       ImGui::Dummy(ImVec2(0.0f, gap - ImGui::GetStyle().ItemSpacing.y));
        changed |= mapTile(resources, backend, target, mat, map.label, map.member, map.usage, map.hint, face);
        ++shown;
    }

    if (hidden == 0) return changed;
    if (shown % columns != 0) ImGui::SameLine(0.0f, gap);
    else if (shown > 0)       ImGui::Dummy(ImVec2(0.0f, gap - ImGui::GetStyle().ItemSpacing.y));

    ImGui::PushID("addMap");
    ImGui::BeginGroup();
    const ImVec2 faceMin = ImGui::GetCursorScreenPos();

    // Hover as the tiles do: an accent border, not a filled button.
    const ImVec4 inert = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, inert);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, inert);
    if (ImGui::Button("##addFace", ImVec2(face, face))) ImGui::OpenPopup("##addMapMenu");
    ImGui::PopStyleColor(2);

    const ImVec2 faceMax = ImGui::GetItemRectMax();
    const bool   hovered = ImGui::IsItemHovered();
    ImDrawList*  dl      = ImGui::GetWindowDrawList();
    if (hovered) {
        dl->AddRect(
            faceMin,
            faceMax,
            ImGui::GetColorU32(EditorStyle::Accent::MAT_TEXTURE),
            0.0f,
            0,
            EditorStyle::px(2.0f)
        );
        ImGui::SetTooltip("Packed and secondary maps");
    }

    ImVec4 glyph = EditorStyle::Accent::MAT_TEXTURE;
    glyph.w = hovered ? 0.90f : 0.45f;
    drawEditorIcon(
        dl,
        EditorIcon::Plus,
        ImVec2((faceMin.x + faceMax.x) * 0.5f, (faceMin.y + faceMax.y) * 0.5f),
        face * 0.22f,
        ImGui::GetColorU32(glyph)
    );

    if (ImGui::BeginPopup("##addMapMenu")) {
        sectionLabel("Add Map");
        ImGui::Separator();
        for (const MapDesc& map : MAPS) {
            if (map.core || (mat.*(map.member))) continue;
            if (ImGui::MenuItem(map.label)) openTexturePicker(target, map.member, map.usage);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", map.hint);
        }
        ImGui::EndPopup();
    }

    clippedLine("Add map", face, true);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

bool MaterialEditorPanel::drawParameters(
    ResourceManager& resources,
    EditorRenderHooks* backend,
    MaterialHandle target,
    MaterialAsset& mat
) {
    bool changed = false;

    if (beginComponentCard("Base", EditorStyle::Accent::MAT_BASE, true)) {
        if (propEnumCombo("Type", mat.type)) {
            // Turn the discard on even if the asset had cutoff = 0 (off).
            if (mat.type == MaterialType::AlphaMask && mat.alphaCutoff <= 0.0f) {
                mat.alphaCutoff = 0.5f;
            }
            changed = true;
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

        changed |= propColor4(
            "Albedo",
            glm::value_ptr(mat.albedo),
            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf
        );
        changed |= propSlider("Metallic", &mat.metallic, 0.0f, 1.0f, "%.2f");
        changed |= propSlider("Roughness", &mat.roughness, 0.0f, 1.0f, "%.2f");
        changed |= propSlider(
            "AO",
            &mat.ao,
            0.0f,
            1.0f,
            "%.2f",
            "Scales indirect light over the whole material"
        );
        const char* iorTooltip =
            "How strongly a dielectric reflects head-on. 1.0 air, 1.33 water, "
            "1.5 glass, 2.4 diamond";
        changed |= propDrag("IOR", &mat.ior, 0.01f, 1.0f, 3.0f, "%.2f", iorTooltip);

        changed |= propColor3(
            "Emission",
            glm::value_ptr(mat.emission),
            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR,
            "Light the material gives off on its own. Black = none"
        );
        // A multiplier on black is black, so the strength appears with the colour.
        if (mat.emission.r > 0.0f || mat.emission.g > 0.0f || mat.emission.b > 0.0f) {
            changed |= propDrag(
                "Strength",
                &mat.emissiveStrength,
                0.05f,
                0.0f,
                64.0f,
                "%.2f",
                "HDR multiplier on the emission colour; this is what drives bloom"
            );
        }
    }
    endComponentCard();

    for (const Feature& feature : FEATURES) {
        if (!feature.active(mat)) continue;
        bool remove = false;
        if (beginComponentCard(feature.name, *feature.accent, true, &remove)) {
            changed |= feature.body(mat);
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

    // Maps last, so an added lobe sits under the rows it extends, not below the grid.
    ImGui::Spacing();
    if (beginComponentCard("Maps", EditorStyle::Accent::MAT_TEXTURE, true)) {
        changed |= drawMaps(resources, backend, target, mat);

        // Each scale shows only with its map: it multiplies the texture.
        if (mat.normalTexture) {
            ImGui::Spacing();
            changed |= propDrag(
                "Normal Scale",
                &mat.normalScale,
                0.01f,
                0.0f,
                5.0f,
                "%.2f",
                "How far the normal map bends the surface. 0 flat, 1 as authored"
            );
        }
        if (mat.heightTexture) {
            ImGui::Spacing();
            changed |= propDrag(
                "Height Scale",
                &mat.heightScale,
                0.001f,
                0.0f,
                0.5f,
                "%.3f",
                "Parallax depth the height map displaces by. 0.02 to 0.1 is typical"
            );
        }
    }
    endComponentCard();

    return changed;
}

void MaterialEditorPanel::openTexturePicker(
    MaterialHandle owner,
    TextureHandle MaterialAsset::* member,
    TextureUsage usage
) {
    AssetPicker::Options options;
    options.title      = "Pick Texture";
    options.root       = ProjectPaths::assets();
    options.recursive  = true;
    options.extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
    options.hint       = std::string("Read as ") + Reflect::enumName(usage);
    m_texturePicker.open(options);

    m_pendingMaterial     = owner;
    m_pendingSlot         = member;
    m_pendingTextureUsage = usage;
}

void MaterialEditorPanel::serviceTexturePicker(EditorContext& ec) {
    std::string picked;
    if (!m_texturePicker.draw(picked) || !m_pendingSlot) return;

    ResourceManager& resources = ec.frame.resources;
    if (resources.isAlive(m_pendingMaterial)) {
        // loadTexture adds without looking: reuse the asset, unless the usage differs.
        const std::string ref = ProjectPaths::toProjectRelative(picked);
        TextureHandle existing = resources.findByName<TextureAsset>(ref);
        if (existing && resources.get(existing).usage() != m_pendingTextureUsage) existing = {};

        const TextureHandle bound = existing
            ? existing
            : loadTexture(picked, resources, m_pendingTextureUsage, true);
        if (bound) {
            const MaterialAsset before = resources.get(m_pendingMaterial);
            resources.edit(m_pendingMaterial).*m_pendingSlot = bound;
            resources.commit(m_pendingMaterial);
            auto bind = std::make_unique<MaterialEditCommand>(
                resources,
                m_pendingMaterial,
                before,
                resources.get(m_pendingMaterial),
                "Bind Texture"
            );
            ec.state.pushStep(std::move(bind));
        }
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

void MaterialEditorPanel::servicePbrFolder(EditorContext& ec, MaterialHandle target) {
    std::string folder;
    if (!m_pbrFolderPicker.draw(folder)) return;

    MaterialHandle built = loadMaterialFromFolder(folder, ec.frame.resources);
    if (!built) return;

    // Through the scope, which records a prefab instance's override.
    if (selectedMeshDrawing(ec, target)) {
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

void MaterialEditorPanel::drawEmptyState(EditorContext& ec) {
    emptyStateHeading(
        EditorIcon::Material,
        "No material open",
        "Pick an entity that has one, or choose one below.",
        2
    );

    const float width = EditorStyle::px(180.0f);
    centreNextItem(width);
    drawChooser(ec, MaterialHandle{}, width);
    centreNextItem(width);
    if (ImGui::Button("+  New Material", ImVec2(width, 0.0f))) {
        if (MaterialHandle fresh = EditorActions::createNewMaterial(ec.frame.resources, ec.state)) {
            ec.state.openMaterial(fresh);
        }
    }
}

void MaterialEditorPanel::draw(EditorContext& ec) {
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;

    const MaterialHandle target = resolveTarget(ec);

    if (!target) {
        drawEmptyState(ec);
    } else {
        const Mesh* selMesh = selectedMesh(ec);

        drawIdentityRow(ec, target);
        ImGui::Spacing();
        ImGui::Spacing();
        drawPreview(ec, target, (selMesh && selMesh->mesh) ? selMesh->mesh : MeshHandle{});
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Only the parameters scroll, so the preview stays visible while dragging.
        const ImVec2 rowGap(ImGui::GetStyle().ItemSpacing.x, EditorStyle::px(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, rowGap);
        if (ImGui::BeginChild("##meParams", ImVec2(0, 0))) {
            // Live edit; the commit bumps the version. Copied every frame: a widget reports a
            // change only after making it.
            MaterialAsset&      mat    = resources.edit(target);
            const MaterialAsset before = mat;
            EditorRenderHooks* hooks = editorRenderHooks(ec.renderSystem.backend());
            if (drawParameters(resources, hooks, target, mat)) {
                resources.commit(target);
                auto edit = std::make_unique<MaterialEditCommand>(
                    resources,
                    target,
                    before,
                    mat,
                    "Edit Material"
                );
                state.pushStep(std::move(edit));
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // At panel scope (OpenPopup hashes against the calling window), and even with no
    // material, or one deleted mid-pick would strand the picker.
    serviceTexturePicker(ec);
    servicePbrFolder(ec, target);

    if (renameDialog("Rename Material", m_renameOpen, m_renameBuf, sizeof(m_renameBuf))
        && target && resources.isAlive(target)) {
        EditorActions::renameAsset(resources, state, target, m_renameOldName, m_renameBuf, "Rename Material");
    }
}

MeshHandle materialPreviewSphere(ResourceManager& resources) {
    return previewMeshFor(resources, SHAPES[0]);
}

} // namespace Vkm::Engine
