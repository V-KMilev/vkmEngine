#include "panels/material_editor_panel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "framework/editor_common.h"
#include "framework/material_preview_session.h"
#include "framework/editor_actions.h"
#include "loader/texture_loaders.h"
#include "loader/material_loaders.h"
#include "system/render/render_system.h"
#include "generator/mesh_generators.h"
#include "generator/texture_generators.h"
#include "io/project_paths.h"

namespace Engine {

namespace {
// Card accents - keep the Material Editor in the same visual language as
// the Inspector (left accent strip + guide line per group).
const ImVec4 ACC_BASE   = ImVec4(0.90f, 0.55f, 0.25f, 1.0f);  // warm
const ImVec4 ACC_SURF   = ImVec4(0.28f, 0.74f, 0.74f, 1.0f);  // teal
const ImVec4 ACC_COAT   = ImVec4(0.45f, 0.62f, 0.92f, 1.0f);  // light blue
const ImVec4 ACC_ANISO  = ImVec4(0.64f, 0.44f, 0.86f, 1.0f);  // purple
const ImVec4 ACC_SSS    = ImVec4(0.88f, 0.45f, 0.55f, 1.0f);  // pink
const ImVec4 ACC_SHEEN  = ImVec4(1.00f, 0.80f, 0.22f, 1.0f);  // gold
const ImVec4 ACC_VOL    = ImVec4(0.55f, 0.85f, 0.65f, 1.0f);  // mint - glass volume
const ImVec4 ACC_TEX    = EditorStyle::AXIS_Y;                 // green

// Fold everything that changes the live preview image into one version stamp
// so MaterialPreviewSession re-bakes only when something actually changed,
// instead of re-rendering the preview every idle frame.
uint64_t previewVersion(uint64_t materialVersion, uint32_t shapeId,
                        int primitive, float yaw, float pitch, float distance) {
    auto floatBits = [](float f) {
        uint32_t b;
        std::memcpy(&b, &f, sizeof(b));
        return static_cast<uint64_t>(b);
    };
    uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
    for (uint64_t v : { materialVersion, static_cast<uint64_t>(shapeId),
                        static_cast<uint64_t>(primitive),
                        floatBits(yaw), floatBits(pitch), floatBits(distance) }) {
        h = (h ^ v) * 1099511628211ull;
    }
    return h;
}

}  // namespace

bool MaterialEditorPanel::drawMaterialBody(
    ResourceManager& resources,
    MaterialHandle target,
    MaterialAsset& mat
) {
    bool changed = false;
    auto slot = [&](const char* label, TextureHandle MaterialAsset::* member, bool srgb) {
        return textureSlot(resources, label, target, mat, member, srgb);
    };

        if (beginComponentCard("Base", ACC_BASE, true)) {
            drawPropertyLabel("Type");
            if (drawEnumCombo("##MatType", mat.type, MATERIAL_TYPE_NAMES, IM_ARRAYSIZE(MATERIAL_TYPE_NAMES))) {
                // Picking AlphaMask in the editor should turn on the discard
                // path even if the asset shipped with cutoff = 0 (off).
                if (mat.type == MaterialType::AlphaMask && mat.alphaCutoff <= 0.0f) {
                    mat.alphaCutoff = 0.5f;
                }
                changed = true;
            }

            changed |= propColor4("Albedo", glm::value_ptr(mat.albedo),
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf);

            changed |= propSlider("Metallic", &mat.metallic, 0.0f, 1.0f, "%.2f");

            changed |= propSlider("Roughness", &mat.roughness, 0.0f, 1.0f, "%.2f");

            changed |= propColor3("Emission", glm::value_ptr(mat.emission),
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

            changed |= propSlider("AO", &mat.ao, 0.0f, 1.0f, "%.2f");

            changed |= propDrag("Emissive Strength", &mat.emissiveStrength, 0.05f, 0.0f, 64.0f, "%.2f",
                "HDR multiplier on emission (drives bloom)");

            changed |= propSlider("Alpha Cutoff", &mat.alphaCutoff, 0.0f, 1.0f, "%.2f",
                "AlphaMask type only: fragments with albedo.a < cutoff are discarded (foliage/leaves)");
        }
        endComponentCard();

        if (beginComponentCard("Surface", ACC_SURF, false)) {
            changed |= propDrag("IOR", &mat.ior, 0.01f, 1.0f, 3.0f, "%.2f",
                "1.0 air, 1.33 water, 1.5 glass, 2.4 diamond");

            changed |= propSlider("Transmission", &mat.transmission, 0.0f, 1.0f, "%.2f");

            changed |= propDrag("Normal Scale", &mat.normalScale, 0.01f, 0.0f, 5.0f, "%.2f");

            changed |= propDrag("Height Scale", &mat.heightScale, 0.001f, 0.0f, 0.5f, "%.3f");
        }
        endComponentCard();

        if (beginComponentCard("Clearcoat", ACC_COAT, false)) {
            changed |= propSlider("Strength", &mat.clearcoat, 0.0f, 1.0f, "%.2f");

            changed |= propSlider("Roughness", &mat.clearcoatRoughness, 0.0f, 1.0f, "%.2f");
        }
        endComponentCard();

        if (beginComponentCard("Anisotropy", ACC_ANISO, false)) {
            changed |= propSlider("Strength", &mat.anisotropy, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Direction");
            changed |= ImGui::DragFloat3("##AnisoDir",
                glm::value_ptr(mat.anisotropyDirection), 0.01f, -1.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Tangent-space anisotropy direction");
        }
        endComponentCard();

        if (beginComponentCard("Subsurface", ACC_SSS, false)) {
            changed |= propSlider("Strength", &mat.subsurface, 0.0f, 1.0f, "%.2f");

            changed |= propColor3("Color", glm::value_ptr(mat.subsurfaceColor),
                ImGuiColorEditFlags_Float);
        }
        endComponentCard();

        if (beginComponentCard("Sheen", ACC_SHEEN, false)) {
            changed |= propColor3("Color", glm::value_ptr(mat.sheenColor),
                ImGuiColorEditFlags_Float, "Black = no sheen (fabric / cloth rim term)");

            changed |= propSlider("Roughness", &mat.sheenRoughness, 0.0f, 1.0f, "%.2f");
        }
        endComponentCard();

        if (beginComponentCard("Volume", ACC_VOL, false)) {
            changed |= propDrag("Thickness", &mat.thicknessFactor, 0.01f, 0.0f, 100.0f, "%.3f",
                "Volume thickness in metres. 0 = thin-walled (no absorption)");

            changed |= propDrag("Atten. Distance", &mat.attenuationDistance, 0.01f, 0.0001f, 1000.0f, "%.3f",
                "Path length (m) at which white light becomes attenuation color");

            changed |= propColor3("Atten. Color", glm::value_ptr(mat.attenuationColor),
                ImGuiColorEditFlags_Float,
                "Transmittance after one attenuation distance (white = clear)");
        }
        endComponentCard();

        if (beginComponentCard("Textures", ACC_TEX, false)) {
            // PBR Core: the maps every PBR material is likely to set.
            ImGui::TextDisabled("PBR Core");
            ImGui::Spacing();
            changed |= slot("Albedo",    &MaterialAsset::albedoTexture,    true);
            changed |= slot("Normal",    &MaterialAsset::normalTexture,    false);
            changed |= slot("Roughness", &MaterialAsset::roughnessTexture, false);
            changed |= slot("Metallic",  &MaterialAsset::metallicTexture,  false);
            changed |= slot("AO",        &MaterialAsset::aoTexture,        false);
            changed |= slot("Emission",  &MaterialAsset::emissionTexture,  true);

            // Packed combinations - the loader auto-uses these when present
            // and disregards the separate-channel rows above.
            ImGui::Spacing();
            ImGui::TextDisabled("Packed");
            ImGui::Spacing();
            changed |= slot("Metallic+Roughness",     &MaterialAsset::metallicRoughnessTexture,   false);
            changed |= slot("AO+Metallic+Roughness",  &MaterialAsset::aoMetallicRoughnessTexture, false);

            // Less common: parallax / clearcoat / glass. Pair with the
            // matching scalar in Surface / Clearcoat / Volume cards to take
            // effect.
            ImGui::Spacing();
            ImGui::TextDisabled("Advanced");
            ImGui::Spacing();
            changed |= slot("Height",       &MaterialAsset::heightTexture,       false);
            changed |= slot("Clearcoat",    &MaterialAsset::clearcoatTexture,    false);
            changed |= slot("Transmission", &MaterialAsset::transmissionTexture, false);
        }
        endComponentCard();

    return changed;
}

MeshHandle MaterialEditorPanel::previewMesh(
    ResourceManager& resources,
    const MeshHandle& entityMesh
) {
    if (m_primitive == 3 && entityMesh) return entityMesh;

    // Look up each preview every call (findByName is O(1)). Caching the
    // handles in a flag-gated block would leave them stale across a scene
    // load - SceneSerializer swaps the whole ResourceManager, dropping
    // every hidden asset along with it. Lazy lookup re-registers
    // automatically on the next preview after a load.
    auto getOrAdd = [&](const char* name, auto&& make) {
        MeshHandle h = resources.findByName<MeshAsset>(name);
        if (!h) h = resources.addPrivate(make(), name);
        return h;
    };
    switch (m_primitive) {
        case 1:  return getOrAdd("mesh:preview_cube",   [] { return generateCube(); });
        case 2:  return getOrAdd("mesh:preview_plane",  [] { return generatePlane(2.0f, 2.0f, 1, 1); });
        default: return getOrAdd("mesh:preview_sphere", [] { return generateSphere(); });
    }
}

bool MaterialEditorPanel::textureSlot(
    ResourceManager& res,
    const char* label,
    MaterialHandle owner,
    MaterialAsset& mat,
    TextureHandle MaterialAsset::* member,
    bool srgb
) {
    TextureHandle& slot = mat.*member;
    ImGui::PushID(label);
    drawPropertyLabel(label);

    std::string cur = "(none)";
    if (slot) {
        const auto& t = res.get(slot);
        const std::string& p = !t.filePath.empty() ? t.filePath : t.name;
        cur = std::filesystem::path(p).filename().string();
        if (cur.empty()) cur = p;
    }

    bool changed = false;

    // File name only, frame-aligned, with Gen/Set/Clear pinned to the right so
    // a long path can never shove them off or overlap them.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float genW  = ImGui::CalcTextSize("Gen").x   + st.FramePadding.x * 2.0f;
    const float setW  = ImGui::CalcTextSize("Set").x   + st.FramePadding.x * 2.0f;
    const float clrW  = ImGui::CalcTextSize("Clear").x + st.FramePadding.x * 2.0f;
    const float btnsX = ImGui::GetContentRegionMax().x - genW - setW - clrW
                      - st.ItemSpacing.x * 2.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(cur.c_str());
    ImGui::SameLine();
    if (ImGui::GetCursorPosX() < btnsX) ImGui::SetCursorPosX(btnsX);
    // Generate a procedural texture (built-in flats or a solid color) for this
    // slot - the half of the texture pipeline that had no editor surface.
    if (ImGui::SmallButton("Gen")) ImGui::OpenPopup("##genTex");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Generate a procedural texture for this slot");
    if (ImGui::BeginPopup("##genTex")) {
        ImGui::TextDisabled("Procedural");
        if (ImGui::MenuItem("White"))  { slot = generateWhiteTexture(res);  changed = true; }
        if (ImGui::MenuItem("Black"))  { slot = generateBlackTexture(res);  changed = true; }
        if (ImGui::MenuItem("Normal")) { slot = generateNormalTexture(res); changed = true; }
        if (ImGui::MenuItem("Gray"))   { slot = generateGrayTexture(res);   changed = true; }
        ImGui::Separator();
        ImGui::TextDisabled("Solid color");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::ColorEdit4("##genCol", glm::value_ptr(m_genColor),
            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf);
        if (ImGui::Button("Create Solid")) {
            slot = createSolidColorTexture(m_genColor, res, srgb);
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Set")) {
        // Configure the panel-owned picker for this slot, then open it.
        // Only one slot's picker is active at a time (single popup).
        const std::filesystem::path appRoot = ProjectPaths::root();
        m_texturePicker.options.popupId    = "PickTexture";
        m_texturePicker.options.title      = "Pick Texture";
        m_texturePicker.options.root       = appRoot / "assets";
        m_texturePicker.options.recursive  = true;
        m_texturePicker.options.kind       = AssetPicker::Kind::Files;
        m_texturePicker.options.extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
        m_texturePicker.options.maxResults = 4000;
        m_texturePicker.options.relativeTo = appRoot;
        m_texturePicker.options.hint       = srgb ? "sRGB: yes" : "sRGB: no";
        m_texturePicker.open();
        // Identify the slot by owner handle + member pointer (not &slot) so the
        // deferred resolution re-resolves through the live handle, immune to a
        // sparse-set reallocation while the picker popup is open.
        m_pendingMaterial    = owner;
        m_pendingSlot        = member;
        m_pendingTextureSrgb = srgb;
    }
    ImGui::SameLine();
    if (slot) {
        if (ImGui::SmallButton("Clear")) { slot = TextureHandle{}; changed = true; }
    } else {
        ImGui::BeginDisabled();
        ImGui::SmallButton("Clear");
        ImGui::EndDisabled();
    }

    ImGui::PopID();
    return changed;
}

bool MaterialEditorPanel::pbrFolderBrowse(std::string& outFolder) {
    if (ImGui::SmallButton("Load PBR Folder...")) {
        const std::filesystem::path appRoot = ProjectPaths::root();
        m_pbrFolderPicker.options.popupId   = "PBRFolder";
        m_pbrFolderPicker.options.title     = "Load PBR Folder";
        m_pbrFolderPicker.options.root      = appRoot / "assets";
        m_pbrFolderPicker.options.recursive = false;
        m_pbrFolderPicker.options.kind      = AssetPicker::Kind::Directories;
        m_pbrFolderPicker.options.extensions.clear();
        m_pbrFolderPicker.options.relativeTo.clear();  // return absolute path
        m_pbrFolderPicker.options.hint.clear();
        m_pbrFolderPicker.open();
    }
    return m_pbrFolderPicker.draw(outFolder);
}

void MaterialEditorPanel::draw(EditorContext& ec) {
    EditorState&     state     = ec.state;
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    ImGui::SetNextWindowSize(ImVec2(760, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material Editor", &state.showMaterialEditor)) {
        ImGui::End();
        return;
    }

    // Resolve which material to edit: explicit target, else the selected
    // entity's mesh material.
    Mesh* selMesh = nullptr;
    if (state.selectedEntity && scene.isAlive(state.selectedEntity)
            && scene.has<Mesh>(state.selectedEntity)) {
        selMesh = &scene.get<Mesh>(state.selectedEntity);
    }
    MaterialHandle target = state.materialEditorTarget;
    if (!target && selMesh && selMesh->material) target = selMesh->material;

    if (!target) {
        ImGui::Spacing();
        ImGui::TextDisabled("No material selected.");
        ImGui::TextWrapped("Select an entity with a material, or pick one below.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##PickMat", "(choose a material)")) {
            // Snapshot once so ImGuiListClipper can window the visible rows.
            std::vector<std::pair<MaterialHandle, const MaterialAsset*>> rows;
            resources.forEachOfType<MaterialAsset>([&](MaterialHandle h, const MaterialAsset& a) {
                if (a.hidden) return;  // editor helpers (e.g. thumbnail neutral) are not user-facing
                rows.emplace_back(h, &a);
            });
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto& [h, a] = rows[i];
                    ImGui::PushID(static_cast<int>(h.id()));
                    if (ImGui::Selectable(a->name.empty() ? "(unnamed)" : a->name.c_str()))
                        state.materialEditorTarget = h;
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::End();
        return;
    }

    // Left pane: studio preview + view controls.
    const float PREVIEW_SIZE = 320.0f;
    const float PANE_WIDTH   = PREVIEW_SIZE + 36.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::BeginChild("##mePreview", ImVec2(PANE_WIDTH, 0), ImGuiChildFlags_Borders);
    {
        const MeshHandle entityMesh =
            (selMesh && selMesh->mesh) ? selMesh->mesh : MeshHandle{};
        const MeshHandle shape = previewMesh(resources, entityMesh);

        // live=true uses a dedicated key so the pane survives the Asset
        // Browser baking thumbnails into the shared target later this same
        // frame (ImGui samples textures at Render). The version stamp gates
        // re-rendering on actual change so an idle Material Editor doesn't
        // re-render the preview every frame.
        const uint64_t version = shape
            ? previewVersion(resources.get(target).version, shape.id(),
                             m_primitive, m_yaw, m_pitch, m_distance)
            : 0ull;
        uint32_t tex = shape
            ? ec.materialPreviews.texture(
                  resources, target, shape, m_yaw, m_pitch, m_distance,
                  /*key*/ 0ull, version, /*live*/ true)
            : 0u;

        if (tex) {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Image(imTexture(tex),
                ImVec2(PREVIEW_SIZE, PREVIEW_SIZE), ImVec2(0, 1), ImVec2(1, 0));
            // Thin frame around the studio render.
            ImGui::GetWindowDrawList()->AddRect(
                origin, ImVec2(origin.x + PREVIEW_SIZE, origin.y + PREVIEW_SIZE),
                IM_COL32(255, 255, 255, 36), 3.0f);
            // Transparent hit-target so the orbit drag owns the active id and
            // never moves the window (see editor ConfigWindowsMoveFromTitleBar).
            ImGui::SetCursorScreenPos(origin);
            ImGui::InvisibleButton("##orbit", ImVec2(PREVIEW_SIZE, PREVIEW_SIZE));
            if (ImGui::IsItemActive()) {
                const ImVec2 d = ImGui::GetIO().MouseDelta;
                m_yaw   -= d.x * 0.4f;
                m_pitch  = std::clamp(m_pitch + d.y * 0.4f, -85.0f, 85.0f);
            }
            if (ImGui::IsItemHovered()) {
                const float w = ImGui::GetIO().MouseWheel;
                if (w != 0.0f) m_distance = std::clamp(m_distance - w * 0.25f, 0.6f, 12.0f);
            }
        } else {
            ImGui::TextDisabled("(preview unavailable)");
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Preview Shape");
        ImGui::SetNextItemWidth(-1);
        const char* prims[] = {"Sphere", "Cube", "Plane", "Entity Mesh"};
        ImGui::Combo("##PreviewPrim", &m_primitive, prims, IM_ARRAYSIZE(prims));
        if (ImGui::Button("Reset View", ImVec2(-1, 0))) {
            m_yaw = 35.0f; m_pitch = 20.0f; m_distance = 3.0f;
        }
        ImGui::Spacing();
        ImGui::TextDisabled("drag = orbit");
        ImGui::TextDisabled("wheel = zoom");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine();

    // Right pane: identity + parameter cards.
    ImGui::BeginChild("##meParams", ImVec2(0, 0), ImGuiChildFlags_Borders);
    {
        const MaterialAsset& cur = resources.get(target);
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
        ImGui::TextUnformatted(cur.name.empty() ? "(unnamed material)"
                                                : cur.name.c_str());
        ImGui::PopStyleColor();

        if (ImGui::SmallButton("New")) {
            if (MaterialHandle nh = EditorActions::createNewMaterial(resources, state)) {
                state.materialEditorTarget = nh;
                target = nh;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create a blank PBR material and edit it here");
        ImGui::SameLine();

        if (ImGui::SmallButton("Duplicate")) {
            if (MaterialHandle nh = EditorActions::duplicateMaterial(resources, state, target, selMesh)) {
                state.materialEditorTarget = nh;
                target = nh;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Fork this material so edits don't affect other users");
        ImGui::SameLine();

        std::string pbrFolder;
        if (pbrFolderBrowse(pbrFolder)) {
            MaterialHandle h = loadMaterialFromFolder(pbrFolder, resources);
            if (h) {
                if (selMesh) {
                    selMesh->material = h;
                    state.markSceneDirty();
                }
                state.materialEditorTarget = h;
                target = h;
            }
        }

        ImGui::Separator();

        // Live edit (shared by handle; commit bumps version -> preview +
        // viewport refresh next frame). Materials are scene assets - any
        // edit is unsaved work.
        auto& mat = resources.edit(target);
        if (drawMaterialBody(resources, target, mat)) {
            resources.commit(target);
            state.markSceneDirty();
        }

        // Resolve the texture picker outside the slot row so it survives the
        // slot's PushID scope. Re-resolve the slot through the stored handle +
        // member rather than a cached pointer: the material may have been
        // reallocated (New/Duplicate) or deleted while the picker was open.
        std::string pickedTex;
        if (m_texturePicker.draw(pickedTex) && m_pendingSlot) {
            if (resources.isAlive(m_pendingMaterial)) {
                const std::string abs = (ProjectPaths::root() / pickedTex).string();
                TextureHandle h = loadTexture(abs, resources, m_pendingTextureSrgb, true);
                if (h) {
                    resources.edit(m_pendingMaterial).*m_pendingSlot = h;
                    resources.commit(m_pendingMaterial);
                    state.markSceneDirty();
                }
            }
            m_pendingSlot = nullptr;
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace Engine
