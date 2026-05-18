#include "panels/inspector_panel.h"
#include "framework/editor_common.h"

#include "generator/light_generators.h"

#include <string>

namespace Engine {

namespace {
    // Per-component accent colors - the left strip / guide line that lets the
    // eye group a card at a glance (Transform blue, Mesh green, ...).
    const ImVec4 kAccentTransform = EditorStyle::AXIS_Z;
    const ImVec4 kAccentMesh      = EditorStyle::AXIS_Y;
    const ImVec4 kAccentLight     = ImVec4(1.00f, 0.80f, 0.22f, 1.0f);
    const ImVec4 kAccentCamera    = ImVec4(0.30f, 0.78f, 0.80f, 1.0f);
    const ImVec4 kAccentAnim      = ImVec4(0.64f, 0.44f, 0.86f, 1.0f);
    const ImVec4 kAccentHierarchy = ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
}

void InspectorPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    drawPanelTitle("Inspector");

    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) {
        ImGui::Spacing();
        ImGui::TextDisabled("No entity selected.");
        ImGui::TextDisabled("Pick one in the Hierarchy or the viewport.");
        return;
    }

    auto& scene = ctx.scene;
    EntityId id = state.selectedEntity;

    // --- Identity header: type badge + name + id + component summary ---
    {
        if (!scene.has<Name>(id)) {
            char fallback[64];
            getEntityDisplayName(scene, id, fallback, sizeof(fallback));
            scene.add(Entity{id}, Name(fallback));
        }
        auto& name = scene.get<Name>(id);

        // Type glyph + editable name + id. The component cards below already
        // show what is attached, so no redundant text summary here.
        const float ih = ImGui::GetFrameHeight();
        inlineIcon(entityIconKind(scene, id), ih,
                   ImGui::GetColorU32(EditorStyle::ACCENT));
        ImGui::SameLine();

        ImGui::SetNextItemWidth(-46.0f);
        ImGui::InputText("##Name", name.value, sizeof(name.value));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("#%u", id.index);
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (scene.has<Transform>(id))  drawTransformSection(scene, id);
    if (scene.has<Mesh>(id))       drawMeshSection(scene, ctx.resources, state, id);
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
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    const bool clicked = ImGui::Button("+  Add Component", ImVec2(-1, 0));
    ImGui::PopStyleColor(2);
    if (clicked) ImGui::OpenPopup("##AddComp");

    if (ImGui::BeginPopup("##AddComp")) {
        ImGui::TextDisabled("Add Component");
        ImGui::Separator();
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
    // Transform is intrinsic - no remove affordance.
    const bool open = beginComponentCard("Transform", kAccentTransform, true);
    if (open) {
        auto& t = scene.get<Transform>(id);
        bool changed = false;
        changed |= drawVec3Control("Position", glm::value_ptr(t.position), 0.0f, 0.1f);

        // Re-seed the Euler cache only when the rotation changed outside this
        // widget (selection change, gizmo, scene load). While the field is
        // being edited, m_eulerDeg stays the source of truth, so typing 90 in
        // one axis does not make the others snap to +/-180 via gimbal-lock
        // decomposition.
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
            ImGui::TextDisabled("World: (%.1f, %.1f, %.1f)",
                worldPos.x, worldPos.y, worldPos.z);
        }
    }
    endComponentCard();
}

void InspectorPanel::drawMeshSection(Scene& scene, ResourceManager& resources,
                                     EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Mesh", kAccentMesh, true, &remove);
    if (open) {
        auto& mesh = scene.get<Mesh>(id);

        drawPropertyLabel("Visible");      ImGui::Checkbox("##MeshVis", &mesh.visible);
        drawPropertyLabel("Cast Shadow");  ImGui::Checkbox("##MeshShad", &mesh.castShadows);

        if (mesh.mesh) {
            const auto& asset = resources.get(mesh.mesh);
            ImGui::TextDisabled("%zu verts, %zu tris",
                asset.vertices.size(), asset.indices.size() / 3);
            if (hasValidBounds(asset.boundsMin, asset.boundsMax)) {
                glm::vec3 ext = asset.boundsMax - asset.boundsMin;
                ImGui::TextDisabled("Bounds: %.1f x %.1f x %.1f", ext.x, ext.y, ext.z);
            }
        }

        ImGui::Spacing();

        // Asset pickers: swap which loaded mesh / material this component uses.
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

        ImGui::Spacing();

        // Material - compact reference. Full PBR + texture editing and the
        // live 3D preview live in the Material Editor (Window > Material
        // Editor).
        if (mesh.material) {
            const MaterialAsset& m = resources.get(mesh.material);
            ImGui::TextDisabled("Material: %s",
                m.name.empty() ? "(unnamed)" : m.name.c_str());
            const float bw = (ImGui::GetContentRegionAvail().x
                              - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Edit Material", ImVec2(bw, 0))) {
                state.materialEditorTarget = mesh.material;
                state.showMaterialEditor   = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate", ImVec2(bw, 0))) {
                MaterialAsset copy = m;
                copy.version = 1;
                copy.name = (m.name.empty() ? std::string("material") : m.name) + " copy";
                MaterialHandle nh = resources.add(std::move(copy));
                if (nh) {
                    mesh.material = nh;
                    state.materialEditorTarget = nh;
                    state.showMaterialEditor   = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fork the material and edit the copy safely");
        } else {
            ImGui::TextDisabled("No material assigned");
        }
    }
    endComponentCard();
    if (remove) scene.remove<Mesh>(Entity{id});
}

void InspectorPanel::drawLightSection(Scene& scene, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Light", kAccentLight, true, &remove);
    if (open) {
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
    }
    endComponentCard();
    if (remove) scene.remove<Light>(Entity{id});
}

void InspectorPanel::drawCameraSection(Scene& scene, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Camera", kAccentCamera, true, &remove);
    if (open) {
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
    }
    endComponentCard();
    if (remove) scene.remove<Camera>(Entity{id});
}

void InspectorPanel::drawAnimationSection(Scene& scene, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Animation", kAccentAnim, true, &remove);
    if (open) {
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
    }
    endComponentCard();
    if (remove) scene.remove<Animation>(Entity{id});
}

void InspectorPanel::drawHierarchySection(Scene& scene, EditorState& state, EntityId id) {
    const bool open = beginComponentCard("Hierarchy", kAccentHierarchy, false);
    if (open) {
        const auto& h = scene.get<Hierarchy>(id);

        bool unparented = false;
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
                unparented = true;  // `h` is now stale - skip the rest
            }
        } else {
            ImGui::TextDisabled("Root (no parent). Drag entities in the Hierarchy to parent them.");
        }

        if (!unparented && scene.has<Hierarchy>(id)) {
            const auto& hh = scene.get<Hierarchy>(id);
            if (hh.firstChild) {
                ImGui::TextDisabled("Children:");
                HierarchyOperations::forEachChild(scene, id, [&](EntityId child) {
                    char name[64];
                    getEntityDisplayName(scene, child, name, sizeof(name));
                    char cid[16];
                    snprintf(cid, sizeof(cid), "%u", child.index);
                    if (entitySelectable(cid, state.selectedEntity == child,
                                         entityIconKind(scene, child), name)) {
                        state.selectedEntity = child;
                    }
                });
            }
        }
    }
    endComponentCard();
}

} // namespace Engine
