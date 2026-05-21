#include "panels/material_editor.h"
#include "framework/editor_common.h"

#include "loader/texture_loaders.h"
#include "loader/material_loaders.h"
#include "system/render/render_system.h"
#include "generator/mesh_generators.h"

#include <cstdint>
#include <cstdio>
#include <cctype>
#include <functional>
#include <string>
#include <utility>
#include <vector>
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

    // The full PBR + texture editor body, grouped into accent cards (same
    // widget language as the Inspector). Free-function helper - composed
    // from MaterialEditorPanel::drawMaterialBody below.
    bool drawMaterialBodyImpl(ResourceManager& resources, MaterialAsset& mat,
                              const std::function<bool(const char*, TextureHandle&, bool)>& slot) {
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
            changed |= slot("Albedo",    mat.albedoTexture,    true);
            changed |= slot("Normal",    mat.normalTexture,    false);
            changed |= slot("Roughness", mat.roughnessTexture, false);
            changed |= slot("Metallic",  mat.metallicTexture,  false);
            changed |= slot("AO",        mat.aoTexture,        false);
            changed |= slot("Emission",  mat.emissionTexture,  true);
            changed |= slot("Height",    mat.heightTexture,    false);
            changed |= slot("Clearcoat", mat.clearcoatTexture, false);
            changed |= slot("Transmission",        mat.transmissionTexture,        false);
            changed |= slot("Metallic+Roughness",  mat.metallicRoughnessTexture,   false);
            changed |= slot("AO+Metallic+Roughness", mat.aoMetallicRoughnessTexture, false);
        }
        endComponentCard();

        return changed;
    }
}  // namespace

MeshHandle MaterialEditorPanel::previewMesh(ResourceManager& resources,
                                            const MeshHandle& entityMesh) {
    if (m_primitive == 3 && entityMesh) return entityMesh;

    // Look up each preview every call (findByName is O(1)). Caching the
    // handles in a flag-gated block would leave them stale across a scene
    // load - SceneSerializer swaps the whole ResourceManager, dropping
    // every editor-internal asset along with it. Lazy lookup re-registers
    // automatically on the next preview after a load.
    auto getOrAdd = [&](const char* name, auto&& make) {
        MeshHandle h = resources.findByName<MeshAsset>(name);
        if (!h) h = resources.addInternal(make(), name);
        return h;
    };
    switch (m_primitive) {
        case 1:  return getOrAdd("mesh:preview_cube",   [] { return generateCube(); });
        case 2:  return getOrAdd("mesh:preview_plane",  [] { return generatePlane(2.0f, 2.0f, 1, 1); });
        default: return getOrAdd("mesh:preview_sphere", [] { return generateSphere(); });
    }
}

bool MaterialEditorPanel::textureSlot(ResourceManager& res, const char* label,
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

    // File name only, frame-aligned, with Set/Clear pinned to the right so
    // a long path can never shove them off or overlap them.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float setW  = ImGui::CalcTextSize("Set").x   + st.FramePadding.x * 2.0f;
    const float clrW  = ImGui::CalcTextSize("Clear").x + st.FramePadding.x * 2.0f;
    const float btnsX = ImGui::GetContentRegionMax().x - setW - clrW - st.ItemSpacing.x;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(cur.c_str());
    ImGui::SameLine();
    if (ImGui::GetCursorPosX() < btnsX) ImGui::SetCursorPosX(btnsX);
    if (ImGui::SmallButton("Set")) {
        // Configure the panel-owned picker for this slot, then open it.
        // Only one slot's picker is active at a time (single popup).
        const std::filesystem::path appRoot = APP_ROOT_DIR;
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
        m_pendingTexture     = &slot;
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
        const std::filesystem::path appRoot = APP_ROOT_DIR;
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

bool MaterialEditorPanel::drawMaterialBody(ResourceManager& resources, MaterialAsset& mat) {
    return drawMaterialBodyImpl(resources, mat,
        [&](const char* label, TextureHandle& slot, bool srgb) {
            return textureSlot(resources, label, slot, srgb);
        });
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
        uint32_t tex = shape
            ? ec.renderSystem.materialPreviewTexture(
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

        // Resolve the texture picker outside the slot row so it survives
        // the slot's PushID scope and matches the slot's pending target.
        std::string pickedTex;
        if (m_texturePicker.draw(pickedTex) && m_pendingTexture) {
            const std::string abs = (std::filesystem::path(APP_ROOT_DIR) / pickedTex).string();
            TextureHandle h = loadTexture(abs, resources, m_pendingTextureSrgb, true);
            if (h) {
                *m_pendingTexture = h;
                resources.commit(target);
            }
            m_pendingTexture = nullptr;
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace Engine
