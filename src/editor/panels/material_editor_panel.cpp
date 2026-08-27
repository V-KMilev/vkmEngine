#include "panels/material_editor_panel.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ecs/component/render/decal.h"
#include "framework/editor_common.h"
#include "framework/editor_commands.h"
#include "framework/editor_actions.h"
#include "framework/material_preview_session.h"
#include "generator/mesh_generators.h"
#include "generator/texture_generators.h"
#include "io/project_paths.h"
#include "loader/material_loaders.h"
#include "loader/texture_loaders.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_system.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {

// Fold everything that changes the live preview image into one version stamp
// so MaterialPreviewSession re-bakes only when something actually changed,
// instead of re-rendering the preview every idle frame.
uint64_t previewVersion(uint64_t materialVersion, uint32_t shapeId,
                        int shape, float yaw, float pitch, float distance,
                        int background, float lightYaw) {
    auto floatBits = [](float f) {
        uint32_t b;
        std::memcpy(&b, &f, sizeof(b));
        return static_cast<uint64_t>(b);
    };
    uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
    for (uint64_t v : { materialVersion, static_cast<uint64_t>(shapeId),
                        static_cast<uint64_t>(shape),
                        floatBits(yaw), floatBits(pitch), floatBits(distance),
                        static_cast<uint64_t>(background), floatBits(lightYaw) }) {
        h = (h ^ v) * 1099511628211ull;
    }
    return h;
}

/// What the preview draws the material on. The last one is the selection's own mesh.
struct PreviewShape {
    const char* id;
    const char* label;
    EditorIcon  icon;
};

constexpr PreviewShape SHAPES[] = {
    { "shSphere", "Sphere",      EditorIcon::Sphere },
    { "shCube",   "Cube",        EditorIcon::Cube   },
    { "shPlane",  "Plane",       EditorIcon::Plane  },
    { "shMesh",   "As selected", EditorIcon::Mesh   },
};
constexpr int SHAPE_ENTITY = 3;

constexpr const char* BACKGROUNDS[] = { "Dark", "Grey", "Sky" };

/**
 * @brief Property row for a colour: a full-width swatch that opens the picker.
 *
 * Every other row in this panel fills its width, and a colour is the one value
 * here that is judged by eye rather than typed - so it gets the room, instead
 * of four float boxes nobody sets a colour with. The swatch's own read-out
 * stands in for the numbers unless the row has something to say instead.
 *
 * @param label Row label.
 * @param v The colour, 3 or 4 floats depending on @p alpha.
 * @param alpha Whether the fourth component is edited.
 * @param flags Colour-edit flags for both the swatch and the picker.
 * @param tooltip Hover text; replaces the swatch's numeric read-out.
 * @return Whether the colour changed this frame.
 */
bool colorRow(const char* label, float* v, bool alpha, ImGuiColorEditFlags flags,
              const char* tooltip = nullptr) {
    drawPropertyLabel(label);
    ImGui::PushID(label);

    const ImVec4 col(v[0], v[1], v[2], alpha ? v[3] : 1.0f);
    const ImGuiColorEditFlags buttonFlags = tooltip ? (flags | ImGuiColorEditFlags_NoTooltip)
                                                    : flags;
    if (ImGui::ColorButton("##swatch", col, buttonFlags,
                           ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
        ImGui::OpenPopup("##pick");
    }
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);

    bool changed = false;
    if (ImGui::BeginPopup("##pick")) {
        changed = alpha ? ImGui::ColorPicker4("##picker", v, flags)
                        : ImGui::ColorPicker3("##picker", v, flags);
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

/**
 * @brief One texture slot as the map grid draws it.
 *
 * `core` is what separates the six maps every PBR material is authored with
 * from the five that answer a question most materials never ask: a core slot
 * always has a tile, the rest earn one by being bound or by being picked out
 * of the add tile's menu.
 */
struct MapDesc {
    const char* label;
    TextureHandle MaterialAsset::* member;
    bool        srgb;
    bool        core;
    const char* hint;
};

constexpr MapDesc MAPS[] = {
    { "Albedo",   &MaterialAsset::albedoTexture,   true,  true,
      "Base colour. Its alpha is what AlphaMask cuts and Transparent blends on" },
    { "Normal",   &MaterialAsset::normalTexture,   false, true,
      "Tangent-space normal map; Normal Scale below sets how far it bends the surface" },
    { "Roughness",&MaterialAsset::roughnessTexture,false, true,
      "Roughness from the red channel, multiplied by the Roughness slider" },
    { "Metallic", &MaterialAsset::metallicTexture, false, true,
      "Metalness from the red channel, multiplied by the Metallic slider" },
    { "AO",       &MaterialAsset::aoTexture,       false, true,
      "Ambient occlusion from the red channel; darkens indirect light only" },
    { "Emission", &MaterialAsset::emissionTexture, true,  true,
      "Emissive colour, multiplied by the Emission tint" },

    { "Metal+Rough", &MaterialAsset::metallicRoughnessTexture, false, false,
      "glTF packing: roughness in green, metallic in blue. Bound, it is used "
      "instead of the separate Roughness and Metallic maps" },
    { "AO+M+R",      &MaterialAsset::aoMetallicRoughnessTexture, false, false,
      "AO in red, roughness in green, metallic in blue. Bound, it is used "
      "instead of the three separate maps" },
    { "Height",      &MaterialAsset::heightTexture, false, false,
      "Height field for parallax; needs a Height Scale above 0 to do anything" },
    { "Clearcoat",   &MaterialAsset::clearcoatTexture, false, false,
      "Clearcoat strength mask; needs the Clearcoat feature" },
    { "Transmission",&MaterialAsset::transmissionTexture, false, false,
      "Transmission mask; needs the Transmission feature" },
};

/**
 * @brief One secondary lobe, and everything the panel needs to offer it.
 *
 * A material uses two or three of these at most, so they are cards only while
 * they are on: `active` decides that from the asset itself rather than from a
 * flag nothing serializes, `enable` gives the lobe a value it can be seen at,
 * and `disable` puts every field it owns back where an unused lobe leaves them
 * - the texture included, or the card it turned off would come straight back.
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
    bool changed = propSlider("Transmission", &m.transmission, 0.0f, 1.0f, "%.2f",
        "How much light refracts through instead of scattering off");
    changed |= propDrag("Thickness", &m.thicknessFactor, 0.01f, 0.0f, 100.0f, "%.3f",
        "Volume thickness in metres. 0 = thin-walled, and nothing is absorbed");
    changed |= propDrag("Absorb Dist.", &m.attenuationDistance, 0.01f, 0.0001f, 1000.0f, "%.3f",
        "Path length at which white light becomes the absorption colour");
    changed |= colorRow("Absorb Tint", glm::value_ptr(m.attenuationColor), false,
        ImGuiColorEditFlags_Float,
        "What white light turns into over one absorption distance (white = clear)");
    return changed;
}

bool clearcoatBody(MaterialAsset& m) {
    bool changed = propSlider("Strength", &m.clearcoat, 0.0f, 1.0f, "%.2f");
    changed |= propSlider("Roughness", &m.clearcoatRoughness, 0.0f, 1.0f, "%.2f",
        "Roughness of the coat alone; the layer under it keeps its own");
    return changed;
}

bool anisotropyBody(MaterialAsset& m) {
    bool changed = propSlider("Strength", &m.anisotropy, 0.0f, 1.0f, "%.2f");
    changed |= propDrag3("Direction", glm::value_ptr(m.anisotropyDirection),
        0.01f, -1.0f, 1.0f, "%.2f", "Which way the grain runs, in tangent space");
    return changed;
}

bool subsurfaceBody(MaterialAsset& m) {
    bool changed = propSlider("Strength", &m.subsurface, 0.0f, 1.0f, "%.2f");
    changed |= colorRow("Tint", glm::value_ptr(m.subsurfaceColor), false,
        ImGuiColorEditFlags_Float, "Colour light picks up on its way back out");
    return changed;
}

bool sheenBody(MaterialAsset& m) {
    bool changed = colorRow("Colour", glm::value_ptr(m.sheenColor), false,
        ImGuiColorEditFlags_Float,
        "Black is no sheen at all, which is how this feature switches off");
    changed |= propSlider("Roughness", &m.sheenRoughness, 0.0f, 1.0f, "%.2f");
    return changed;
}

constexpr Feature FEATURES[] = {
    {
        "Transmission", &EditorStyle::Accent::MatGlass,
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
        "Clearcoat", &EditorStyle::Accent::MatCoat,
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
        "Anisotropy", &EditorStyle::Accent::MatAniso,
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
        "Subsurface", &EditorStyle::Accent::MatSSS,
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
        "Sheen", &EditorStyle::Accent::MatSheen,
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

/// The selected entity's Mesh, or null when the selection has none.
Mesh* selectedMesh(EditorContext& ec) {
    const EntityId id = ec.state.selectedEntity;
    Scene& scene = ec.frame.scene;
    if (!id || !scene.isAlive(id) || !scene.has<Mesh>(id)) return nullptr;
    return &scene.get<Mesh>(id);
}

/// What fills a slot: the bound texture's file name, or where it came from.
std::string slotDetail(const ResourceManager& resources, const TextureHandle& slot) {
    if (!slot) return "(none)";
    const auto& texture = resources.get(slot);
    // The generators mark their output with a "procedural:" pseudo-path rather
    // than a file, and "which file is in this slot" is not a question a
    // generated texture has an answer to.
    if (texture.filePath.empty() || texture.filePath.rfind("procedural:", 0) == 0) {
        return "(generated)";
    }
    std::string file = std::filesystem::path(texture.filePath).filename().string();
    return file.empty() ? texture.filePath : file;
}

} // namespace

MaterialHandle MaterialEditorPanel::resolveTarget(EditorContext& ec) {
    EditorState&     state     = ec.state;
    ResourceManager& resources = ec.frame.resources;
    const Mesh*      selMesh   = selectedMesh(ec);

    // A chosen target outlives the material it names: the Asset Browser can
    // delete it, and every get() / edit() below is unguarded. Drop it rather
    // than fall through with a dead handle in state.
    if (state.materialEditorTarget && !resources.isAlive(state.materialEditorTarget)) {
        state.materialEditorTarget = {};
    }

    // A material chosen by hand outranks the selection, but only until another
    // entity carrying one is picked; see docs/reference/editor.md, "Which
    // material the tab edits".
    if (state.selectedEntity != m_lastSelection) {
        m_lastSelection = state.selectedEntity;
        if (selMesh && selMesh->material) state.materialEditorTarget = {};
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
    if (!ImGui::BeginCombo("##ChooseMat", preview, ImGuiComboFlags_HeightLarge)) return;

    // Type-to-narrow, focused on open: the same affordance Add Component has,
    // and a project can hold more materials than one list is worth scrolling.
    if (ImGui::IsWindowAppearing()) {
        m_chooserFilter[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##matFilter", "Search...", m_chooserFilter, sizeof(m_chooserFilter),
                             ImGuiInputTextFlags_EscapeClearsAll);
    ImGui::Separator();

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
            const bool current = target && handle.id() == target.id();
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

    // Who else draws with this material. Edits reach every one of them, so the
    // blast radius is stated beside the name rather than discovered afterwards.
    std::vector<EntityId> users;
    scene.forEach<Mesh>([&](EntityId id, const Mesh& mesh) {
        if (mesh.material && mesh.material.id() == target.id()) users.push_back(id);
    });
    scene.forEach<Decal>([&](EntityId id, const Decal& decal) {
        if (decal.material && decal.material.id() == target.id()) users.push_back(id);
    });

    char chip[24];
    if (users.empty()) std::snprintf(chip, sizeof(chip), "unused");
    else std::snprintf(chip, sizeof(chip), "%zu user%s", users.size(), users.size() == 1 ? "" : "s");

    const float glyphW = ImGui::GetFrameHeight();
    const float menuW  = ImGui::GetFrameHeight();
    const float chipW  = ImGui::CalcTextSize(chip).x + style.FramePadding.x * 2.0f;
    const float comboW = ImGui::GetContentRegionAvail().x - glyphW - chipW - menuW
                       - style.ItemSpacing.x * 3.0f;

    // The row opens on what kind of thing it is editing, the way the Inspector's
    // identity row does one tab over. The hue is the one the Asset Browser's
    // Materials rail and every material tile already wear.
    inlineIcon(EditorIcon::Material, glyphW,
               ImGui::GetColorU32(EditorStyle::Accent::MatBase));
    ImGui::SameLine();
    drawChooser(ec, target, comboW);

    ImGui::SameLine();
    ImGui::BeginDisabled(users.empty());
    if (ImGui::Button(chip, ImVec2(chipW, 0.0f))) {
        state.selectEntity(users[0]);
        for (size_t i = 1; i < users.size(); ++i) state.addToSelection(users[i]);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", users.empty() ? "Nothing in this project uses it"
                                              : "Select every entity using this material");
    }

    // One button for everything done to the material rather than to its
    // parameters. None of the four is a daily act, and four buttons across a
    // column this narrow left no room for the name they act on.
    ImGui::SameLine();
    if (ImGui::Button("...", ImVec2(menuW, 0.0f))) ImGui::OpenPopup("##matActions");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Material actions");

    if (!ImGui::BeginPopup("##matActions")) return;

    const MaterialAsset& current = resources.get(target);
    sectionLabel(current.name().empty() ? "(unnamed)" : current.name().c_str());
    ImGui::Separator();

    if (ImGui::MenuItem("Duplicate")) {
        if (MaterialHandle copy = EditorActions::duplicateMaterial(resources, state, target,
                                                                   selectedMesh(ec))) {
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
    if (ImGui::MenuItem("Load PBR Folder...")) m_requestPbrFolder = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Build a material from a folder of maps named the usual way");
    }
    ImGui::EndPopup();
}

MeshHandle MaterialEditorPanel::previewMesh(ResourceManager& resources,
                                            const MeshHandle& entityMesh) {
    if (m_shape == SHAPE_ENTITY && entityMesh) return entityMesh;

    // Looked up every call (findByName is O(1)). Cached handles would go stale
    // across a scene load, which swaps the whole ResourceManager and drops every
    // hidden asset with it; a lazy lookup re-registers them.
    auto getOrAdd = [&](const char* name, auto&& make) {
        MeshHandle h = resources.findByName<MeshAsset>(name);
        if (!h) h = resources.addPrivate(make(), name);
        return h;
    };
    switch (m_shape) {
        case 1:  return getOrAdd("mesh:preview_cube",   [] { return generateCube(); });
        case 2:  return getOrAdd("mesh:preview_plane",  [] { return generatePlane(2.0f, 2.0f, 1, 1); });
        default: return getOrAdd("mesh:preview_sphere", [] { return generateSphere(); });
    }
}

void MaterialEditorPanel::drawPreview(EditorContext& ec, MaterialHandle target,
                                      MeshHandle entityMesh) {
    ResourceManager& resources = ec.frame.resources;

    // Capped against the height as well as the width, so a wide detached window
    // gets a preview worth detaching for while a short one does not push every
    // parameter below the fold to get it.
    const float avail  = ImGui::GetContentRegionAvail().x;
    const float room   = ImGui::GetContentRegionAvail().y * 0.55f;
    const float face   = std::clamp(std::min(avail, room),
                                    EditorStyle::px(140.0f), EditorStyle::px(460.0f));
    const float inset = (avail - face) * 0.5f;

    const MeshHandle shape = previewMesh(resources, entityMesh);
    uint32_t tex = 0;
    if (shape) {
        // live = true uses a dedicated key, so the pane survives the Asset
        // Browser baking thumbnails into the shared target later this same
        // frame (ImGui samples textures at Render).
        PreviewRequest req;
        req.key         = 0ull;
        req.mesh        = shape;
        req.material    = target;
        req.yawDeg      = m_yaw;
        req.pitchDeg    = m_pitch;
        req.distance    = m_distance;
        req.background  = static_cast<PreviewBackground>(m_background);
        req.lightYawDeg = m_lightYaw;
        const uint64_t version = previewVersion(resources.get(target).version(), shape.id(),
                                                m_shape, m_yaw, m_pitch, m_distance,
                                                m_background, m_lightYaw);
        tex = ec.materialPreviews.texture(resources, req, version, /*live*/ true);
    }

    if (!tex) {
        ImGui::TextDisabled("(preview unavailable)");
        return;
    }

    if (inset > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inset);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList*  dl     = ImGui::GetWindowDrawList();

    ImGui::Image(imTexture(tex), ImVec2(face, face), ImVec2(0, 1), ImVec2(1, 0));
    dl->AddRect(origin, ImVec2(origin.x + face, origin.y + face),
                ImGui::GetColorU32(ImGuiCol_Border), EditorStyle::px(3.0f));

    // A transparent hit-target over the picture, so the orbit drag owns the
    // active id and never moves the window. It covers the whole face, so the
    // controls drawn on top of it have to answer the pointer first.
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

    // The view controls ride the picture, the way the Asset Browser puts a
    // sound's transport on its tile: they belong to what they change, and the
    // column keeps its rows for the material.
    const float height = ImGui::GetFrameHeight();
    const float pad    = EditorStyle::px(6.0f);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + face - height - pad));
    ImGui::BeginGroup();
    for (int i = 0; i < IM_ARRAYSIZE(SHAPES); ++i) {
        if (i > 0) ImGui::SameLine(0.0f, EditorStyle::px(3.0f));
        const bool usable = i != SHAPE_ENTITY || !!entityMesh;
        const char* tip = (i == SHAPE_ENTITY && !entityMesh)
                        ? "Select an entity with a mesh to preview on it"
                        : SHAPES[i].label;
        if (iconButton(SHAPES[i].id, SHAPES[i].icon, m_shape == i, usable, tip, height)) {
            m_shape = i;
        }
    }
    ImGui::EndGroup();

    const char* backdrop = BACKGROUNDS[m_background];
    const float backW    = ImGui::CalcTextSize(backdrop).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + face - backW - pad,
                                     origin.y + face - height - pad));
    if (ImGui::Button(backdrop, ImVec2(backW, height))) {
        m_background = (m_background + 1) % IM_ARRAYSIZE(BACKGROUNDS);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Backdrop the preview is lit against");

    ImGui::SetCursorScreenPos(below);
    if (inset > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inset);
    ImGui::SetNextItemWidth(face);
    ImGui::SliderFloat("##light", &m_lightYaw, 0.0f, 360.0f, "Light %.0f deg");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Swing the key light around the material");
    }
}

bool MaterialEditorPanel::mapTile(ResourceManager& resources, EditorRenderHooks* backend,
                                  MaterialHandle owner, MaterialAsset& mat, const char* label,
                                  TextureHandle MaterialAsset::* member, bool srgb,
                                  const char* hint, float face) {
    TextureHandle& slot = mat.*member;
    bool changed = false;

    ImGui::PushID(label);
    ImGui::BeginGroup();

    // The GPU mirror the renderer samples; 0 is not-resident-yet or an empty
    // slot, and the live preview syncs textures so a bound slot resolves within
    // a frame. Loaded textures are flipped at decode, so the UVs unflip.
    const GpuTextureId texId = (slot && backend) ? backend->textureId(slot) : 0;
    const ImVec2       faceMin = ImGui::GetCursorScreenPos();
    ImDrawList*        dl      = ImGui::GetWindowDrawList();

    // No frame around the face: an ImageButton insets its picture by
    // FramePadding and a sized Button does not, so a bound tile and an empty one
    // would stand at different heights and lose their shared baseline.
    const ImVec4 inert = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, inert);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, inert);
    const bool clicked = texId
        ? ImGui::ImageButton("##face", imTexture(texId), ImVec2(face, face),
                             ImVec2(0, 1), ImVec2(1, 0))
        : ImGui::Button("##face", ImVec2(face, face));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    const ImVec2 faceMax = ImGui::GetItemRectMax();

    if (ImGui::IsItemHovered()) {
        dl->AddRect(faceMin, faceMax, ImGui::GetColorU32(EditorStyle::Accent::MatTexture),
                    0.0f, 0, EditorStyle::px(2.0f));
        ImGui::BeginTooltip();
        sectionLabel(label);
        if (texId) {
            const float big = EditorStyle::px(128.0f);
            ImGui::Image(imTexture(texId), ImVec2(big, big), ImVec2(0, 1), ImVec2(1, 0));
            const auto& t = resources.get(slot);
            ImGui::TextDisabled("%ux%u%s", t.params.width, t.params.height, t.srgb ? "  sRGB" : "");
            if (!t.filePath.empty()) ImGui::TextDisabled("%s", t.filePath.c_str());
        }
        ImGui::PushTextWrapPos(EditorStyle::px(320.0f));
        ImGui::TextDisabled("%s", hint);
        ImGui::TextDisabled("%s", slot ? "Click to replace, right-click for more"
                                       : "Click to bind a texture, right-click for more");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (clicked) openTexturePicker(owner, member, srgb);

    if (ImGui::BeginPopupContextItem("##slot")) {
        sectionLabel(label);
        ImGui::Separator();
        if (ImGui::MenuItem(slot ? "Replace..." : "Bind Texture...")) {
            openTexturePicker(owner, member, srgb);
        }
        ImGui::BeginDisabled(!slot);
        if (ImGui::MenuItem("Clear")) {
            slot    = TextureHandle{};
            changed = true;
        }
        ImGui::EndDisabled();

        // The generators are laid out rather than nested in a submenu: there
        // are five of them, they are one click each, and a row of buttons shows
        // what is on offer where a submenu would have hidden it behind a hover.
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

        ImGui::ColorEdit4("##genCol", glm::value_ptr(m_genColor),
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf
                        | ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        if (ImGui::Button("Solid")) bind(createSolidColorTexture(m_genColor, resources, srgb));
        ImGui::EndPopup();
    }

    if (!texId) {
        // An empty slot wears the kind's glyph faintly, the way the Asset
        // Browser's untaken thumbnails do.
        ImVec4 glyph = EditorStyle::Accent::MatTexture;
        glyph.w = slot ? 0.30f : 0.16f;
        drawEditorIcon(dl, EditorIcon::Texture,
                       ImVec2((faceMin.x + faceMax.x) * 0.5f, (faceMin.y + faceMax.y) * 0.5f),
                       face * 0.22f, ImGui::GetColorU32(glyph));
    }

    clippedLine(label, face, /*dim*/ false);
    clippedLine(slotDetail(resources, slot).c_str(), face, /*dim*/ true);
    ImGui::EndGroup();

    // Drawn after the group so it lies over the face's left edge rather than
    // under it. How solid it is answers what a slot is asked: is anything in it.
    ImVec4 strip = EditorStyle::Accent::MatTexture;
    if (!slot) strip.w *= 0.30f;
    dl->AddRectFilled(faceMin, ImVec2(faceMin.x + EditorStyle::px(3.0f), faceMax.y),
                      ImGui::GetColorU32(strip));

    ImGui::PopID();
    return changed;
}

bool MaterialEditorPanel::drawMaps(ResourceManager& resources, EditorRenderHooks* backend,
                                   MaterialHandle target, MaterialAsset& mat) {
    // One tile size, and as many columns as the panel is wide enough for - the
    // Asset Browser's grid rule. Stretching three tiles across a widened panel
    // would put a map's thumbnail at the size of the preview above it.
    const float face = EditorStyle::px(84.0f);
    // The gap is the grid's own, not the window's item spacing, because a tile
    // is a thing and the space between things has to read as separation rather
    // than as the distance between two rows of a form.
    const float gap  = EditorStyle::px(8.0f);
    const float step = face + gap;
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
        // Asked before the tile rather than after it: a SameLine left pending
        // by the last tile of a short row would put whatever the card draws
        // next - a scale row, or nothing at all - up beside it.
        if (shown % columns != 0) ImGui::SameLine(0.0f, gap);
        else if (shown > 0)         ImGui::Dummy(ImVec2(0.0f, gap - ImGui::GetStyle().ItemSpacing.y));
        changed |= mapTile(resources, backend, target, mat, map.label, map.member,
                           map.srgb, map.hint, face);
        ++shown;
    }

    if (hidden == 0) return changed;
    if (shown % columns != 0) ImGui::SameLine(0.0f, gap);
    else if (shown > 0)       ImGui::Dummy(ImVec2(0.0f, gap - ImGui::GetStyle().ItemSpacing.y));

    // The five packed and secondary slots are a tile only once they hold
    // something. Until then they are behind one tile, in the shape of the tiles
    // beside it, rather than five empty squares nobody fills.
    ImGui::PushID("addMap");
    ImGui::BeginGroup();
    const ImVec2 faceMin = ImGui::GetCursorScreenPos();

    // Answers the pointer the way the tiles beside it do - an accent border,
    // not a filled button - so one square in the grid does not light up in a
    // different colour from the rest.
    const ImVec4 inert = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, inert);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, inert);
    if (ImGui::Button("##addFace", ImVec2(face, face))) ImGui::OpenPopup("##addMapMenu");
    ImGui::PopStyleColor(2);

    const ImVec2 faceMax = ImGui::GetItemRectMax();
    const bool   hovered = ImGui::IsItemHovered();
    ImDrawList*  dl      = ImGui::GetWindowDrawList();
    if (hovered) {
        dl->AddRect(faceMin, faceMax, ImGui::GetColorU32(EditorStyle::Accent::MatTexture),
                    0.0f, 0, EditorStyle::px(2.0f));
        ImGui::SetTooltip("Packed and secondary maps");
    }

    ImVec4 glyph = EditorStyle::Accent::MatTexture;
    glyph.w = hovered ? 0.90f : 0.45f;
    drawEditorIcon(dl, EditorIcon::Plus,
                   ImVec2((faceMin.x + faceMax.x) * 0.5f, (faceMin.y + faceMax.y) * 0.5f),
                   face * 0.22f, ImGui::GetColorU32(glyph));

    if (ImGui::BeginPopup("##addMapMenu")) {
        sectionLabel("Add Map");
        ImGui::Separator();
        for (const MapDesc& map : MAPS) {
            if (map.core || (mat.*(map.member))) continue;
            if (ImGui::MenuItem(map.label)) openTexturePicker(target, map.member, map.srgb);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", map.hint);
        }
        ImGui::EndPopup();
    }

    clippedLine("Add map", face, /*dim*/ true);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

bool MaterialEditorPanel::drawParameters(ResourceManager& resources, EditorRenderHooks* backend,
                                         MaterialHandle target, MaterialAsset& mat) {
    bool changed = false;

    if (beginComponentCard("Base", EditorStyle::Accent::MatBase, true)) {
        if (propEnumCombo("Type", mat.type)) {
            // Picking AlphaMask in the editor should turn the discard path on
            // even if the asset shipped with cutoff = 0 (off).
            if (mat.type == MaterialType::AlphaMask && mat.alphaCutoff <= 0.0f) {
                mat.alphaCutoff = 0.5f;
            }
            changed = true;
        }
        // The cutoff is the AlphaMask path's one parameter and does nothing on
        // the other three, so it is offered where it acts and nowhere else.
        if (mat.type == MaterialType::AlphaMask) {
            changed |= propSlider("Cutoff", &mat.alphaCutoff, 0.0f, 1.0f, "%.2f",
                "Fragments whose albedo alpha falls below this are dropped (foliage, leaves)");
        }

        changed |= colorRow("Albedo", glm::value_ptr(mat.albedo), true,
            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf);
        changed |= propSlider("Metallic", &mat.metallic, 0.0f, 1.0f, "%.2f");
        changed |= propSlider("Roughness", &mat.roughness, 0.0f, 1.0f, "%.2f");
        changed |= propSlider("AO", &mat.ao, 0.0f, 1.0f, "%.2f",
            "Scales indirect light over the whole material");
        changed |= propDrag("IOR", &mat.ior, 0.01f, 1.0f, 3.0f, "%.2f",
            "How strongly a dielectric reflects head-on. 1.0 air, 1.33 water, "
            "1.5 glass, 2.4 diamond");

        changed |= colorRow("Emission", glm::value_ptr(mat.emission), false,
            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR,
            "Light the material gives off on its own. Black = none");
        // A multiplier on black is black, so the strength appears with the
        // colour that gives it something to multiply.
        if (mat.emission.r > 0.0f || mat.emission.g > 0.0f || mat.emission.b > 0.0f) {
            changed |= propDrag("Strength", &mat.emissiveStrength, 0.05f, 0.0f, 64.0f, "%.2f",
                "HDR multiplier on the emission colour; this is what drives bloom");
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
    ImGui::PushStyleColor(ImGuiCol_Button,        EditorStyle::ACCENT);
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

    // Maps last: base and the lobes above answer what this surface is, so a lobe
    // the button adds belongs under the rows it extends rather than below eleven
    // tiles. The maps answer what drives it, and a grid reads better last.
    ImGui::Spacing();
    if (beginComponentCard("Maps", EditorStyle::Accent::MatTexture, true)) {
        changed |= drawMaps(resources, backend, target, mat);

        // Each scale sits with the map it scales, and only while there is one:
        // both are multipliers on a texture, and neither does anything alone.
        if (mat.normalTexture) {
            ImGui::Spacing();
            changed |= propDrag("Normal Scale", &mat.normalScale, 0.01f, 0.0f, 5.0f, "%.2f",
                "How far the normal map bends the surface. 0 flat, 1 as authored");
        }
        if (mat.heightTexture) {
            ImGui::Spacing();
            changed |= propDrag("Height Scale", &mat.heightScale, 0.001f, 0.0f, 0.5f, "%.3f",
                "Parallax depth the height map displaces by. 0.02 to 0.1 is typical");
        }
    }
    endComponentCard();

    return changed;
}

void MaterialEditorPanel::openTexturePicker(MaterialHandle owner,
                                            TextureHandle MaterialAsset::* member, bool srgb) {
    m_texturePicker.options().popupId    = "PickTexture";
    m_texturePicker.options().title      = "Pick Texture";
    m_texturePicker.options().root       = ProjectPaths::assets();
    m_texturePicker.options().recursive  = true;
    m_texturePicker.options().kind       = AssetPicker::Kind::Files;
    m_texturePicker.options().extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
    m_texturePicker.options().maxResults = 4000;
    m_texturePicker.options().relativeTo = ProjectPaths::projectRoot();
    m_texturePicker.options().hint       = srgb ? "sRGB: yes" : "sRGB: no";
    m_texturePicker.open();

    // The slot is identified by owner handle + member pointer rather than by
    // &slot, so the deferred resolution re-resolves through the live handle and
    // is immune to a sparse-set reallocation while the picker is open.
    m_pendingMaterial    = owner;
    m_pendingSlot        = member;
    m_pendingTextureSrgb = srgb;
}

void MaterialEditorPanel::serviceTexturePicker(EditorContext& ec) {
    std::string picked;
    if (!m_texturePicker.draw(picked) || !m_pendingSlot) return;

    ResourceManager& resources = ec.frame.resources;
    if (resources.isAlive(m_pendingMaterial)) {
        // Asked before the load, because loadTexture decodes and adds without
        // looking: the same file in two slots is one asset - unless they want
        // different colour spaces, which is two textures and must stay two.
        const std::string ref = ProjectPaths::toProjectRelative(picked);
        TextureHandle existing = resources.findByName<TextureAsset>(ref);
        if (existing && resources.get(existing).srgb != m_pendingTextureSrgb) existing = {};

        if (TextureHandle bound = existing
                ? existing
                : loadTexture(picked, resources, m_pendingTextureSrgb, true)) {
            // Binding a map is a material edit like any other, so it takes the
            // same undo step rather than being the one field on the panel that
            // cannot be taken back.
            const MaterialAsset before = resources.get(m_pendingMaterial);
            resources.edit(m_pendingMaterial).*m_pendingSlot = bound;
            resources.commit(m_pendingMaterial);
            ec.state.commands.push(std::make_unique<MaterialEditCommand>(
                resources, m_pendingMaterial, before, resources.get(m_pendingMaterial),
                "Bind Texture"));
            ec.state.markSceneDirty();
        }
    }
    m_pendingSlot = nullptr;
}

void MaterialEditorPanel::servicePbrFolder(EditorContext& ec) {
    if (m_requestPbrFolder) {
        m_pbrFolderPicker.options().popupId    = "PBRFolder";
        m_pbrFolderPicker.options().title      = "Load PBR Folder";
        m_pbrFolderPicker.options().root       = ProjectPaths::assets();
        m_pbrFolderPicker.options().recursive  = false;
        m_pbrFolderPicker.options().kind       = AssetPicker::Kind::Directories;
        m_pbrFolderPicker.options().extensions.clear();
        m_pbrFolderPicker.options().relativeTo = ProjectPaths::projectRoot();
        m_pbrFolderPicker.options().hint.clear();
        m_pbrFolderPicker.open();
        m_requestPbrFolder = false;
    }

    std::string folder;
    if (!m_pbrFolderPicker.draw(folder)) return;

    MaterialHandle built = loadMaterialFromFolder(folder, ec.frame.resources);
    if (!built) return;

    if (Mesh* mesh = selectedMesh(ec)) {
        mesh->material = built;
        ec.state.markSceneDirty();
    }
    ec.state.openMaterial(built);
}

void MaterialEditorPanel::drawEmptyState(EditorContext& ec) {
    // The block the Inspector puts up one tab over, in the same shape: a glyph,
    // what is missing, the two ways out, and the verbs that take them. A panel
    // whose two tabs answer a blank state differently reads as two programs.
    const ImVec2 region    = ImGui::GetContentRegionAvail();
    const float  glyphSize = EditorStyle::px(56.0f);
    const float  lineH     = ImGui::GetTextLineHeightWithSpacing();
    const float  blockH    = glyphSize + lineH * 2.0f + ImGui::GetFrameHeight() * 2.0f
                           + EditorStyle::px(24.0f);
    ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (region.y - blockH) * 0.35f)));

    const ImVec2 cur = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(0.0f, glyphSize));
    drawEditorIcon(ImGui::GetWindowDrawList(), EditorIcon::Material,
                   ImVec2(cur.x + region.x * 0.5f, cur.y + glyphSize * 0.5f),
                   glyphSize * 0.40f, ImGui::GetColorU32(ImGuiCol_TextDisabled));

    // Centring something wider than the panel would start it left of the panel,
    // clipping the head of the line rather than its tail.
    const auto centre = [&](float width) {
        ImGui::SetCursorPosX(std::max(0.0f, (region.x - width) * 0.5f));
    };

    ImGui::Spacing();
    const char* line1 = "No material open";
    const char* line2 = "Pick an entity that has one, or choose one below.";
    centre(ImGui::CalcTextSize(line1).x);
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
    ImGui::TextUnformatted(line1);
    ImGui::PopStyleColor();
    centre(ImGui::CalcTextSize(line2).x);
    ImGui::TextDisabled("%s", line2);

    ImGui::Spacing();
    const float width = EditorStyle::px(180.0f);
    centre(width);
    drawChooser(ec, MaterialHandle{}, width);
    centre(width);
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

        // The parameters scroll and the preview does not, the point being to
        // watch the material while a slider far down the column is dragged. The
        // rows sit looser than a form's: this column is read, not filled in.
        const ImVec2 rowGap(ImGui::GetStyle().ItemSpacing.x, EditorStyle::px(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, rowGap);
        if (ImGui::BeginChild("##meParams", ImVec2(0, 0))) {
            // Live edit; the commit bumps the version, refreshing the preview
            // and the viewport next frame. The pre-edit copy is taken every
            // frame because a widget only reports a change after making it.
            MaterialAsset&      mat    = resources.edit(target);
            const MaterialAsset before = mat;
            if (drawParameters(resources, editorRenderHooks(ec.renderSystem.backend()),
                               target, mat)) {
                resources.commit(target);
                state.commands.push(std::make_unique<MaterialEditCommand>(
                    resources, target, before, mat, "Edit Material"));
                state.markSceneDirty();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // At panel scope, not inside the parameter child: OpenPopup hashes its id
    // against the calling window. Raised whether or not there is a material -
    // one that went away while a picker was open is what would strand it.
    serviceTexturePicker(ec);
    servicePbrFolder(ec);

    if (renameDialog("Rename Material", m_renameOpen, m_renameBuf, sizeof(m_renameBuf))
            && target && resources.isAlive(target)) {
        EditorActions::renameAsset(resources, state, target, m_renameOldName,
                                   m_renameBuf, "Rename Material");
        state.markSceneDirty();
    }
}

} // namespace Vkm::Engine
