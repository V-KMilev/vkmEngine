#include "panels/inspector_panel.h"
#include "framework/editor_common.h"

#include "generator/light_generators.h"
#include "loader/texture_loaders.h"
#include "loader/material_loaders.h"

#include <cstdio>
#include <cctype>
#include <string>
#include <filesystem>
#include <system_error>

namespace Engine {

namespace {
    bool isImageExt(std::string ext) {
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
            || ext == ".tga" || ext == ".bmp";
    }

    // One material texture-slot row: shows the bound texture, a Set button
    // (modal that lists every image under assets/ recursively) and Clear.
    // Returns true if the binding changed.
    bool textureSlot(ResourceManager& res, const char* label,
                     TextureHandle& slot, bool srgb) {
        ImGui::PushID(label);
        drawPropertyLabel(label);

        std::string cur = "(none)";
        if (slot) {
            const auto& t = res.get(slot);
            cur = !t.filePath.empty() ? t.filePath : t.name;
        }
        ImGui::TextUnformatted(cur.c_str());
        ImGui::SameLine();

        bool changed = false;
        char pop[80];
        snprintf(pop, sizeof(pop), "Pick##%s", label);
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

    // "Load PBR Folder" modal: lists the immediate sub-folders of assets/.
    // On pick, writes the absolute folder path to @p out and returns true.
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
}

void InspectorPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;
    // Panel header
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
    ImGui::TextUnformatted("Inspector");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) {
        ImGui::TextDisabled("No entity selected");
        return;
    }

    auto& scene = ctx.scene;
    EntityId id = state.selectedEntity;

    // Name editing
    {
        if (!scene.has<Name>(id)) {
            char fallback[64];
            getEntityDisplayName(scene, id, fallback, sizeof(fallback));
            scene.add(Entity{id}, Name(fallback));
        }
        auto& name = scene.get<Name>(id);
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##Name", name.value, sizeof(name.value));
        ImGui::SameLine();
        ImGui::TextDisabled("#%u", id.index);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (scene.has<Transform>(id))  drawTransformSection(scene, id);
    if (scene.has<Mesh>(id))       drawMeshSection(scene, ctx.resources, id);
    if (scene.has<Light>(id))      drawLightSection(scene, id);
    if (scene.has<Camera>(id))     drawCameraSection(scene, id);
    if (scene.has<Animation>(id))  drawAnimationSection(scene, id);
    if (scene.has<Hierarchy>(id))  drawHierarchySection(scene, state, id);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAddComponentMenu(scene, id);
}

void InspectorPanel::drawAddComponentMenu(Scene& scene, EntityId id) {
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
        ImGui::OpenPopup("##AddComp");
    }

    if (ImGui::BeginPopup("##AddComp")) {
        if (!scene.has<Mesh>(id) && ImGui::MenuItem("Mesh")) {
            scene.add(Entity{id}, Mesh{});
        }
        if (!scene.has<Light>(id) && ImGui::MenuItem("Light")) {
            scene.add(Entity{id}, generatePointLight());
        }
        if (!scene.has<Camera>(id) && ImGui::MenuItem("Camera")) {
            Camera cam;
            cam.active = false;
            scene.add(Entity{id}, cam);
        }
        if (!scene.has<Animation>(id) && ImGui::MenuItem("Animation")) {
            scene.add(Entity{id}, Animation{});
        }
        ImGui::EndPopup();
    }
}

void InspectorPanel::drawTransformSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
    if (!open) return;

    auto& t = scene.get<Transform>(id);
    bool changed = false;
    changed |= drawVec3Control("Position", glm::value_ptr(t.position), 0.0f, 0.1f);

    // Re-seed the Euler cache only when the rotation changed outside this
    // widget (selection change, gizmo, scene load). While the field is being
    // edited, m_eulerDeg stays the source of truth, so typing 90 in one axis
    // does not make the others snap to +/-180 via gimbal-lock decomposition.
    const glm::quat cached = glm::quat(glm::radians(m_eulerDeg));
    if (!(m_eulerFor == id) || glm::abs(glm::dot(cached, t.rotation)) < 0.9999f) {
        m_eulerDeg = glm::degrees(glm::eulerAngles(t.rotation));
        m_eulerFor = id;
    }
    if (drawVec3Control("Rotation", glm::value_ptr(m_eulerDeg), 0.0f, 0.5f)) {
        t.rotation = glm::normalize(glm::quat(glm::radians(m_eulerDeg)));
        changed = true;
    }

    changed |= drawVec3Control("Scale", glm::value_ptr(t.scale), 1.0f, 0.01f);

    if (changed) HierarchyOperations::markDirty(scene, id);

    if (scene.has<Hierarchy>(id) && scene.get<Hierarchy>(id).parent) {
        glm::mat4 worldMat = HierarchyOperations::computeWorldMatrix(scene, id);
        glm::vec3 worldPos(worldMat[3]);
        ImGui::TextDisabled("  World: (%.1f, %.1f, %.1f)", worldPos.x, worldPos.y, worldPos.z);
    }

    ImGui::Spacing();
}

void InspectorPanel::drawMeshSection(Scene& scene, ResourceManager& resources, EntityId id) {
    bool open = ImGui::CollapsingHeader("Mesh##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Mesh", id.index)) { scene.remove<Mesh>(Entity{id}); return; }

    if (!open) return;
    auto& mesh = scene.get<Mesh>(id);

    drawPropertyLabel("Visible");    ImGui::Checkbox("##MeshVis", &mesh.visible);
    drawPropertyLabel("Cast Shadow");    ImGui::Checkbox("##MeshShad", &mesh.castShadows);

    // Mesh info
    if (mesh.mesh) {
        const auto& asset = resources.get(mesh.mesh);
        ImGui::TextDisabled("  %zu verts, %zu tris",
            asset.vertices.size(), asset.indices.size() / 3);
        if (hasValidBounds(asset.boundsMin, asset.boundsMax)) {
            glm::vec3 ext = asset.boundsMax - asset.boundsMin;
            ImGui::TextDisabled("  Bounds: %.1f x %.1f x %.1f", ext.x, ext.y, ext.z);
        }
    }

    ImGui::Spacing();

    // Asset pickers: swap which loaded mesh / material this component uses.
    {
        const std::string mn = mesh.mesh
            ? resources.get(mesh.mesh).name : std::string("(none)");
        drawPropertyLabel("Mesh Asset");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##MeshPick", mn.empty() ? "(unnamed)" : mn.c_str())) {
            resources.forEachOfType<MeshAsset>([&](MeshHandle h, const MeshAsset& a) {
                ImGui::PushID(static_cast<int>(h.id()));
                const bool sel = mesh.mesh && mesh.mesh.id() == h.id();
                if (ImGui::Selectable(a.name.empty() ? "(unnamed)" : a.name.c_str(), sel))
                    mesh.mesh = h;
                ImGui::PopID();
            });
            ImGui::EndCombo();
        }

        const std::string mtn = mesh.material
            ? resources.get(mesh.material).name : std::string("(none)");
        drawPropertyLabel("Material Asset");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##MatPick", mtn.empty() ? "(unnamed)" : mtn.c_str())) {
            resources.forEachOfType<MaterialAsset>([&](MaterialHandle h, const MaterialAsset& a) {
                ImGui::PushID(static_cast<int>(h.id()));
                const bool sel = mesh.material && mesh.material.id() == h.id();
                if (ImGui::Selectable(a.name.empty() ? "(unnamed)" : a.name.c_str(), sel))
                    mesh.material = h;
                ImGui::PopID();
            });
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();

    // Material properties
    if (mesh.material) {
        bool matOpen = ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen);
        if (matOpen) {
            // Replace this slot's whole material from an auto-discovered
            // PBR texture folder (Color/Normal/Roughness/... by name).
            std::string pbrFolder;
            if (pbrFolderBrowse(pbrFolder)) {
                MaterialHandle h = loadMaterialFromFolder(pbrFolder, resources);
                if (h) mesh.material = h;
            }

            auto& mat = resources.edit(mesh.material);
            bool changed = false;

            // Material type
            drawPropertyLabel("Type");
            const char* matTypes[] = {"Opaque", "Transparent", "Unlit"};
            int matTypeIdx = static_cast<int>(mat.type);
            if (ImGui::Combo("##MatType", &matTypeIdx, matTypes, IM_ARRAYSIZE(matTypes))) {
                mat.type = static_cast<MaterialType>(matTypeIdx);
                changed = true;
            }

            // Base PBR
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

            // Surface
            if (ImGui::TreeNode("Surface##Mat")) {
                drawPropertyLabel("IOR");
                changed |= ImGui::DragFloat("##IOR", &mat.ior, 0.01f, 1.0f, 3.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("1.0 air, 1.33 water, 1.5 glass, 2.4 diamond");

                drawPropertyLabel("Transmission");
                changed |= ImGui::SliderFloat("##Trans", &mat.transmission, 0.0f, 1.0f, "%.2f");

                drawPropertyLabel("Normal Scale");
                changed |= ImGui::DragFloat("##NScale", &mat.normalScale, 0.01f, 0.0f, 5.0f, "%.2f");

                drawPropertyLabel("Height Scale");
                changed |= ImGui::DragFloat("##HScale", &mat.heightScale, 0.001f, 0.0f, 0.5f, "%.3f");

                ImGui::TreePop();
            }

            // Clearcoat
            if (ImGui::TreeNode("Clearcoat##Mat")) {
                drawPropertyLabel("Strength");
                changed |= ImGui::SliderFloat("##CC", &mat.clearcoat, 0.0f, 1.0f, "%.2f");

                drawPropertyLabel("Roughness");
                changed |= ImGui::SliderFloat("##CCR", &mat.clearcoatRoughness, 0.0f, 1.0f, "%.2f");

                ImGui::TreePop();
            }

            // Anisotropy
            if (ImGui::TreeNode("Anisotropy##Mat")) {
                drawPropertyLabel("Strength");
                changed |= ImGui::SliderFloat("##Aniso", &mat.anisotropy, 0.0f, 1.0f, "%.2f");

                drawPropertyLabel("Direction");
                changed |= ImGui::DragFloat3("##AnisoDir",
                    glm::value_ptr(mat.anisotropyDirection), 0.01f, -1.0f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Tangent-space anisotropy direction");

                ImGui::TreePop();
            }

            // Subsurface
            if (ImGui::TreeNode("Subsurface##Mat")) {
                drawPropertyLabel("Strength");
                changed |= ImGui::SliderFloat("##SSS", &mat.subsurface, 0.0f, 1.0f, "%.2f");

                drawPropertyLabel("Color");
                changed |= ImGui::ColorEdit3("##SSSCol", glm::value_ptr(mat.subsurfaceColor),
                    ImGuiColorEditFlags_Float);

                ImGui::TreePop();
            }

            // Sheen / cloth (Charlie). A black sheen colour disables it.
            if (ImGui::TreeNode("Sheen##Mat")) {
                drawPropertyLabel("Color");
                changed |= ImGui::ColorEdit3("##SheenCol", glm::value_ptr(mat.sheenColor),
                    ImGuiColorEditFlags_Float);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Black = no sheen (fabric / cloth rim term)");

                drawPropertyLabel("Roughness");
                changed |= ImGui::SliderFloat("##SheenR", &mat.sheenRoughness,
                    0.0f, 1.0f, "%.2f");

                ImGui::TreePop();
            }

            // Texture maps. sRGB for colour inputs (albedo/emission),
            // linear for data maps.
            if (ImGui::TreeNode("Textures##Mat")) {
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
                ImGui::TreePop();
            }

            if (changed) resources.commit(mesh.material);
            ImGui::TreePop();
        }
    }
    ImGui::Spacing();
}

void InspectorPanel::drawLightSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Light##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Light", id.index)) { scene.remove<Light>(Entity{id}); return; }

    if (!open) return;
    auto& light = scene.get<Light>(id);

    drawPropertyLabel("Type");
    const char* typeNames[] = {"Directional", "Point", "Spot"};
    int typeIdx = static_cast<int>(light.type);
    if (ImGui::Combo("##LType", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames))) {
        light.type = static_cast<LightType>(typeIdx);
    }

    drawPropertyLabel("Color");
    ImGui::ColorEdit3("##LColor", glm::value_ptr(light.color), ImGuiColorEditFlags_Float);

    drawPropertyLabel("Intensity");
    ImGui::DragFloat("##LIntensity", &light.intensity, 0.1f, 0.0f, 100.0f, "%.1f");

    if (light.type != LightType::Directional) {
        drawPropertyLabel("Radius");
        ImGui::DragFloat("##LRadius", &light.radius, 0.5f, 0.1f, 1000.0f, "%.1f");
    }

    if (light.type == LightType::Spot) {
        float innerDeg = glm::degrees(light.innerConeAngle);
        float outerDeg = glm::degrees(light.outerConeAngle);
        drawPropertyLabel("Inner Cone");
        if (ImGui::DragFloat("##InnerC", &innerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg"))
            light.innerConeAngle = glm::radians(innerDeg);
        drawPropertyLabel("Outer Cone");
        if (ImGui::DragFloat("##OuterC", &outerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg"))
            light.outerConeAngle = glm::radians(outerDeg);
    }

    drawPropertyLabel("Shadows");  ImGui::Checkbox("##Shad", &light.castShadows);
    if (light.castShadows) {
        drawPropertyLabel("Shadow Bias");
        ImGui::DragFloat("##ShadBias", &light.shadowBias, 0.0005f, 0.0f, 0.1f, "%.4f");
        if (light.type == LightType::Directional) {
            drawPropertyLabel("Shadow Extent");
            ImGui::DragFloat("##ShadExt", &light.shadowExtent, 1.0f, 1.0f, 1000.0f, "%.1f");
        }
    }
    drawPropertyLabel("Enabled");  ImGui::Checkbox("##LEn", &light.enabled);
    ImGui::Spacing();
}

void InspectorPanel::drawCameraSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Camera##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Camera", id.index)) { scene.remove<Camera>(Entity{id}); return; }

    if (!open) return;
    auto& cam = scene.get<Camera>(id);

    drawPropertyLabel("Projection");
    const char* projNames[] = {"Perspective", "Orthographic"};
    int projIdx = static_cast<int>(cam.projection);
    if (ImGui::Combo("##CProj", &projIdx, projNames, IM_ARRAYSIZE(projNames)))
        cam.projection = static_cast<ProjectionType>(projIdx);

    if (cam.projection == ProjectionType::Perspective) {
        float fovDeg = glm::degrees(cam.fovY);
        drawPropertyLabel("FOV");
        if (ImGui::SliderFloat("##CFOV", &fovDeg, 10.0f, 170.0f, "%.0f deg"))
            cam.fovY = glm::radians(fovDeg);
    } else {
        drawPropertyLabel("Ortho Height");
        ImGui::DragFloat("##COrthoH", &cam.orthoHeight, 0.1f, 0.1f, 1000.0f);
    }

    drawPropertyLabel("Near Clip"); ImGui::DragFloat("##CNear", &cam.zNear, 0.01f, 0.001f, cam.zFar, "%.3f");
    drawPropertyLabel("Far Clip");  ImGui::DragFloat("##CFar", &cam.zFar, 1.0f, cam.zNear, 100000.0f, "%.0f");
    drawPropertyLabel("Exposure");  ImGui::DragFloat("##CExp", &cam.exposure, 0.01f, 0.0f, 10.0f, "%.2f");
    drawPropertyLabel("Active");    ImGui::Checkbox("##CAct", &cam.active);

    if (ImGui::Button("Set as Main Camera", ImVec2(-1, 0))) {
        scene.forEach<Camera>([&](EntityId other, Camera& c) {
            c.active = (other == id);
        });
    }
    ImGui::Spacing();
}

void InspectorPanel::drawAnimationSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Animation##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Animation", id.index)) { scene.remove<Animation>(Entity{id}); return; }

    if (!open) return;
    auto& anim = scene.get<Animation>(id);

    const float kGap = 8.0f;
    float ih = ImGui::GetFrameHeight();
    if (iconButton("inspPlay", anim.playing ? EditorIcon::Pause : EditorIcon::Play,
                   anim.playing, true, anim.playing ? "Pause" : "Play", ih))
        anim.playing = !anim.playing;
    ImGui::SameLine(0, kGap);
    if (iconButton("inspStop", EditorIcon::Stop, false, true, "Stop (rewind)", ih)) {
        anim.playing = false;
        anim.time = 0.0f;
    }
    ImGui::SameLine(0, kGap);
    ImGui::Checkbox("Loop", &anim.looping);
    ImGui::SameLine(0, kGap);
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##ASpeed", &anim.speed, 0.005f, 0.0f, 10.0f, "Speed %.2fx");

    if (anim.duration > 0.0f) {
        ImGui::SetNextItemWidth(-1);
        char timeFmt[32];
        snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", anim.duration);
        ImGui::SliderFloat("##ATime", &anim.time, 0.0f, anim.duration, timeFmt);
    }

    ImGui::TextDisabled("Tracks: %s%s%s | keys %zu/%zu/%zu",
        anim.positionTrack.isEmpty() ? "" : "P ",
        anim.rotationTrack.isEmpty() ? "" : "R ",
        anim.scaleTrack.isEmpty()    ? "" : "S ",
        anim.positionTrack.keyframeCount(),
        anim.rotationTrack.keyframeCount(),
        anim.scaleTrack.keyframeCount());
    ImGui::Spacing();
}

void InspectorPanel::drawHierarchySection(Scene& scene, EditorState& state, EntityId id) {
    if (!ImGui::CollapsingHeader("Hierarchy##Sec")) return;
    const auto& h = scene.get<Hierarchy>(id);

    if (h.parent) {
        ImGui::TextDisabled("Parent:");
        ImGui::SameLine();
        char parentName[64];
        getEntityDisplayName(scene, h.parent, parentName, sizeof(parentName));
        if (ImGui::SmallButton(parentName)) {
            state.selectedEntity = h.parent;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Unparent")) {
            HierarchyOperations::removeFromParent(scene, id);
            HierarchyOperations::markDirty(scene, id);
            state.hierarchyDirty = true;
            return;  // `h` is now stale (component may have been removed)
        }
    } else {
        ImGui::TextDisabled("Root (no parent). Drag entities in the Hierarchy to parent them.");
    }

    if (h.firstChild) {
        ImGui::TextDisabled("Children:");
        HierarchyOperations::forEachChild(scene, id, [&](EntityId child) {
            char icon[8], name[64];
            getEntityIcon(scene, child, icon, sizeof(icon));
            getEntityDisplayName(scene, child, name, sizeof(name));
            char lbl[96];
            snprintf(lbl, sizeof(lbl), "  %s %s", icon, name);
            if (ImGui::Selectable(lbl, state.selectedEntity == child)) {
                state.selectedEntity = child;
            }
        });
    }
    ImGui::Spacing();
}

} // namespace Engine
