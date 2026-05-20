#include "panels/material_editor.h"
#include "framework/editor_common.h"

#include "loader/texture_loaders.h"
#include "loader/material_loaders.h"
#include "system/render/render_system.h"
#include "generator/mesh_generators.h"

#include <cstdint>
#include <cstdio>
#include <cctype>
#include <string>
#include <filesystem>
#include <system_error>
#include <algorithm>

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

    bool isImageExt(std::string ext) {
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
            || ext == ".tga" || ext == ".bmp";
    }

    // One material texture-slot row: bound texture name + Set (recursive
    // assets/ image picker, sRGB-correct) + Clear. Returns true on change.
    bool textureSlot(ResourceManager& res, const char* label,
                     TextureHandle& slot, bool srgb) {
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
        char pop[80];
        snprintf(pop, sizeof(pop), "Pick##%s", label);

        // File name only, frame-aligned, with Set/Clear pinned to the right so
        // a long path can never shove them off or overlap them.
        const ImGuiStyle& st = ImGui::GetStyle();
        const float setW  = ImGui::CalcTextSize("Set").x   + st.FramePadding.x * 2.0f;
        const float clrW  = ImGui::CalcTextSize("Clear").x + st.FramePadding.x * 2.0f;
        const float btnsX = ImGui::GetContentRegionMax().x - setW - clrW
                          - st.ItemSpacing.x;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(cur.c_str());
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() < btnsX) ImGui::SetCursorPosX(btnsX);
        if (ImGui::SmallButton("Set")) ImGui::OpenPopup(pop);
        ImGui::SameLine();
        if (slot) {
            if (ImGui::SmallButton("Clear")) { slot = TextureHandle{}; changed = true; }
        } else {
            ImGui::BeginDisabled();
            ImGui::SmallButton("Clear");
            ImGui::EndDisabled();
        }

        if (ImGui::BeginPopupModal(pop, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::filesystem::path root =
                std::filesystem::path(APP_ROOT_DIR) / "assets";
            ImGui::TextDisabled("%s  (sRGB: %s)", root.string().c_str(),
                srgb ? "yes" : "no");
            ImGui::Separator();
            std::error_code ec;
            int shown = 0;
            for (const auto& e :
                    std::filesystem::recursive_directory_iterator(root, ec)) {
                if (!e.is_regular_file()) continue;
                if (!isImageExt(e.path().extension().string())) continue;
                std::error_code rel_ec;
                const std::string rel = std::filesystem::relative(
                    e.path(), std::filesystem::path(APP_ROOT_DIR), rel_ec)
                    .generic_string();
                if (ImGui::Selectable(rel.c_str())) {
                    TextureHandle h = loadTexture(e.path().string(), res, srgb, true);
                    if (h) { slot = h; changed = true; }
                    ImGui::CloseCurrentPopup();
                }
                if (++shown > 4000) break;  // safety cap
            }
            if (shown == 0) ImGui::TextDisabled("(no images under assets/)");
            ImGui::Separator();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    }

    // "Load PBR Folder" modal: immediate sub-folders of assets/. On pick
    // writes the absolute folder path to @p out and returns true.
    bool pbrFolderBrowse(std::string& out) {
        bool picked = false;
        if (ImGui::SmallButton("Load PBR Folder...")) ImGui::OpenPopup("PBRFolder");
        if (ImGui::BeginPopupModal("PBRFolder", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::filesystem::path root =
                std::filesystem::path(APP_ROOT_DIR) / "assets";
            ImGui::TextDisabled("%s", root.string().c_str());
            ImGui::Separator();
            std::error_code ec;
            int shown = 0;
            for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
                if (!e.is_directory()) continue;
                const std::string name = e.path().filename().string();
                if (ImGui::Selectable(name.c_str())) {
                    out = e.path().string();
                    picked = true;
                    ImGui::CloseCurrentPopup();
                }
                ++shown;
            }
            if (shown == 0) ImGui::TextDisabled("(no sub-folders in assets/)");
            ImGui::Separator();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return picked;
    }

    // The full PBR + texture editor body, grouped into accent cards (same
    // widget language as the Inspector). Returns true if anything changed.
    bool drawMaterialBody(ResourceManager& resources, MaterialAsset& mat) {
        bool changed = false;

        if (beginComponentCard("Base", ACC_BASE, true)) {
            drawPropertyLabel("Type");
            // Order must match the MaterialType enum value (Opaque=0,
            // Transparent=1, Unlit=2, AlphaMask=3).
            const char* matTypes[] = {"Opaque", "Transparent", "Unlit", "AlphaMask"};
            int matTypeIdx = static_cast<int>(mat.type);
            if (ImGui::Combo("##MatType", &matTypeIdx, matTypes, IM_ARRAYSIZE(matTypes))) {
                mat.type = static_cast<MaterialType>(matTypeIdx);
                // Picking AlphaMask in the editor should turn on the discard
                // path even if the asset shipped with cutoff = 0 (off).
                if (mat.type == MaterialType::AlphaMask && mat.alphaCutoff <= 0.0f) {
                    mat.alphaCutoff = 0.5f;
                }
                changed = true;
            }

            drawPropertyLabel("Albedo");
            changed |= ImGui::ColorEdit4("##Albedo", glm::value_ptr(mat.albedo),
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf);

            drawPropertyLabel("Metallic");
            changed |= ImGui::SliderFloat("##Metallic", &mat.metallic, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Roughness");
            changed |= ImGui::SliderFloat("##Roughness", &mat.roughness, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Emission");
            changed |= ImGui::ColorEdit3("##Emission", glm::value_ptr(mat.emission),
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

            drawPropertyLabel("AO");
            changed |= ImGui::SliderFloat("##AO", &mat.ao, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Alpha");
            changed |= ImGui::SliderFloat("##Alpha", &mat.alpha, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Alpha Cutoff");
            changed |= ImGui::SliderFloat("##AlphaCutoff", &mat.alphaCutoff, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = off. >0 enables AlphaMask: fragments with albedo.a < cutoff are discarded (foliage/leaves)");
        }
        endComponentCard();

        if (beginComponentCard("Surface", ACC_SURF, false)) {
            drawPropertyLabel("IOR");
            changed |= ImGui::DragFloat("##IOR", &mat.ior, 0.01f, 1.0f, 3.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("1.0 air, 1.33 water, 1.5 glass, 2.4 diamond");

            drawPropertyLabel("Transmission");
            changed |= ImGui::SliderFloat("##Trans", &mat.transmission, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Normal Scale");
            changed |= ImGui::DragFloat("##NScale", &mat.normalScale, 0.01f, 0.0f, 5.0f, "%.2f");

            drawPropertyLabel("Height Scale");
            changed |= ImGui::DragFloat("##HScale", &mat.heightScale, 0.001f, 0.0f, 0.5f, "%.3f");
        }
        endComponentCard();

        if (beginComponentCard("Clearcoat", ACC_COAT, false)) {
            drawPropertyLabel("Strength");
            changed |= ImGui::SliderFloat("##CC", &mat.clearcoat, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Roughness");
            changed |= ImGui::SliderFloat("##CCR", &mat.clearcoatRoughness, 0.0f, 1.0f, "%.2f");
        }
        endComponentCard();

        if (beginComponentCard("Anisotropy", ACC_ANISO, false)) {
            drawPropertyLabel("Strength");
            changed |= ImGui::SliderFloat("##Aniso", &mat.anisotropy, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Direction");
            changed |= ImGui::DragFloat3("##AnisoDir",
                glm::value_ptr(mat.anisotropyDirection), 0.01f, -1.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Tangent-space anisotropy direction");
        }
        endComponentCard();

        if (beginComponentCard("Subsurface", ACC_SSS, false)) {
            drawPropertyLabel("Strength");
            changed |= ImGui::SliderFloat("##SSS", &mat.subsurface, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Color");
            changed |= ImGui::ColorEdit3("##SSSCol", glm::value_ptr(mat.subsurfaceColor),
                ImGuiColorEditFlags_Float);
        }
        endComponentCard();

        if (beginComponentCard("Sheen", ACC_SHEEN, false)) {
            drawPropertyLabel("Color");
            changed |= ImGui::ColorEdit3("##SheenCol", glm::value_ptr(mat.sheenColor),
                ImGuiColorEditFlags_Float);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Black = no sheen (fabric / cloth rim term)");

            drawPropertyLabel("Roughness");
            changed |= ImGui::SliderFloat("##SheenR", &mat.sheenRoughness,
                0.0f, 1.0f, "%.2f");
        }
        endComponentCard();

        if (beginComponentCard("Volume", ACC_VOL, false)) {
            drawPropertyLabel("Thickness");
            changed |= ImGui::DragFloat("##Thick", &mat.thicknessFactor,
                0.01f, 0.0f, 100.0f, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Volume thickness in metres. 0 = thin-walled (no absorption)");

            drawPropertyLabel("Atten. Distance");
            changed |= ImGui::DragFloat("##AttD", &mat.attenuationDistance,
                0.01f, 0.0001f, 1000.0f, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Path length (m) at which white light becomes attenuation color");

            drawPropertyLabel("Atten. Color");
            changed |= ImGui::ColorEdit3("##AttC", glm::value_ptr(mat.attenuationColor),
                ImGuiColorEditFlags_Float);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Transmittance after one attenuation distance (white = clear)");
        }
        endComponentCard();

        if (beginComponentCard("Textures", ACC_TEX, false)) {
            changed |= textureSlot(resources, "Albedo",    mat.albedoTexture,    true);
            changed |= textureSlot(resources, "Normal",    mat.normalTexture,    false);
            changed |= textureSlot(resources, "Roughness", mat.roughnessTexture, false);
            changed |= textureSlot(resources, "Metallic",  mat.metallicTexture,  false);
            changed |= textureSlot(resources, "AO",        mat.aoTexture,        false);
            changed |= textureSlot(resources, "Emission",  mat.emissionTexture,  true);
            changed |= textureSlot(resources, "Height",    mat.heightTexture,    false);
            changed |= textureSlot(resources, "Clearcoat", mat.clearcoatTexture, false);
            changed |= textureSlot(resources, "Transmission",
                mat.transmissionTexture, false);
            changed |= textureSlot(resources, "Metallic+Roughness",
                mat.metallicRoughnessTexture, false);
            changed |= textureSlot(resources, "AO+Metallic+Roughness",
                mat.aoMetallicRoughnessTexture, false);
        }
        endComponentCard();

        return changed;
    }
}  // namespace

MeshHandle MaterialEditorPanel::previewMesh(ResourceManager& resources,
                                            const MeshHandle& entityMesh) {
    if (m_primitive == 3 && entityMesh) return entityMesh;

    if (!m_primsReady) {
        m_sphere = resources.add(generateSphere(),                "mesh:preview_sphere");
        m_cube   = resources.add(generateCube(),                  "mesh:preview_cube");
        m_plane  = resources.add(generatePlane(2.0f, 2.0f, 1, 1), "mesh:preview_plane");
        m_primsReady = true;
    }
    switch (m_primitive) {
        case 1:  return m_cube;
        case 2:  return m_plane;
        default: return m_sphere;   // 0 = sphere; 3 with no entity mesh falls back
    }
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
            resources.forEachOfType<MaterialAsset>([&](MaterialHandle h, const MaterialAsset& a) {
                ImGui::PushID(static_cast<int>(h.id()));
                if (ImGui::Selectable(a.name.empty() ? "(unnamed)" : a.name.c_str()))
                    state.materialEditorTarget = h;
                ImGui::PopID();
            });
            ImGui::EndCombo();
        }
        ImGui::End();
        return;
    }

    // ===== Left pane: studio preview + view controls =====
    const float PREVIEW_SIZE = 320.0f;
    const float PANE_WIDTH   = PREVIEW_SIZE + 36.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::BeginChild("##mePreview", ImVec2(PANE_WIDTH, 0), ImGuiChildFlags_Borders);
    {
        const MeshHandle entityMesh =
            (selMesh && selMesh->mesh) ? selMesh->mesh : MeshHandle{};
        const MeshHandle shape = previewMesh(resources, entityMesh);

        // live=true: re-render every frame and snapshot to a dedicated key so
        // it survives the Asset Browser baking thumbnails into the shared
        // target later this same frame (ImGui samples textures at Render).
        uint32_t tex = (ec.renderSystem && shape)
            ? ec.renderSystem->materialPreviewTexture(
                  resources, target, shape, m_yaw, m_pitch, m_distance,
                  /*key*/ 0ull, /*version*/ 0ull, /*live*/ true)
            : 0u;

        if (tex) {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(tex)),
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

    // ===== Right pane: identity + parameter cards =====
    ImGui::BeginChild("##meParams", ImVec2(0, 0), ImGuiChildFlags_Borders);
    {
        const MaterialAsset& cur = resources.get(target);
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
        ImGui::TextUnformatted(cur.name.empty() ? "(unnamed material)"
                                                : cur.name.c_str());
        ImGui::PopStyleColor();

        if (ImGui::SmallButton("Duplicate")) {
            MaterialAsset copy = cur;          // value copy of params + tex refs
            copy.version = 1;
            copy.name    = (cur.name.empty() ? std::string("material")
                                             : cur.name) + " copy";
            MaterialHandle nh = resources.add(std::move(copy));
            if (nh) {
                if (selMesh) selMesh->material = nh;   // reassign the entity
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
                if (selMesh) selMesh->material = h;
                state.materialEditorTarget = h;
                target = h;
            }
        }

        ImGui::Separator();

        // Live edit (shared by handle; commit bumps version -> preview +
        // viewport refresh next frame).
        auto& mat = resources.edit(target);
        if (drawMaterialBody(resources, mat)) resources.commit(target);
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace Engine
