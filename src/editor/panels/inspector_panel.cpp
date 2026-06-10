#include "panels/inspector_panel.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ecs/component/animation.h"
#include "ecs/component/collider.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/transform.h"
#include "framework/editor_commands.h"
#include "framework/editor_common.h"
#include "generator/light_generators.h"
#include "framework/editor_actions.h"
#include "io/project_paths.h"            // ProjectPaths::root for the probe HDR browse
#include "resource/resource_manager.h"
#include "system/physics/collider_fit.h"
#include "system/visibility/bounds_utils.h"

namespace Engine {

namespace {
// Per-component accent colors - the left strip / guide line that lets the
// eye group a card at a glance (Transform blue, Mesh green, ...).
const ImVec4 ACCENT_TRANSFORM = EditorStyle::AXIS_Z;
const ImVec4 ACCENT_MESH      = EditorStyle::AXIS_Y;
const ImVec4 ACCENT_LOD       = ImVec4(0.90f, 0.49f, 0.13f, 1.0f);
const ImVec4 ACCENT_LIGHT     = ImVec4(1.00f, 0.80f, 0.22f, 1.0f);
const ImVec4 ACCENT_CAMERA    = ImVec4(0.30f, 0.78f, 0.80f, 1.0f);
const ImVec4 ACCENT_ANIM      = ImVec4(0.64f, 0.44f, 0.86f, 1.0f);
const ImVec4 ACCENT_HIERARCHY = ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
const ImVec4 ACCENT_PHYSICS   = ImVec4(0.36f, 0.78f, 0.45f, 1.0f);
const ImVec4 ACCENT_COLLIDER  = ImVec4(0.25f, 0.65f, 0.40f, 1.0f);

// Asset-reference combo: pick which loaded asset of type Asset a handle points
// at. Snapshots the asset list so ImGuiListClipper can window thousands of rows
// fluidly. Returns true if the selection changed. Shared by the Mesh + LOD cards.
template <typename Asset, typename Handle>
bool pickAsset(const char* comboId, const char* label, ResourceManager& resources, Handle& currentHandle) {
    const std::string cur = currentHandle
        ? resources.get(currentHandle).name : std::string("(none)");
    drawPropertyLabel(label);
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo(comboId, cur.empty() ? "(unnamed)" : cur.c_str()))
        return false;

    std::vector<std::pair<Handle, const Asset*>> rows;
    resources.forEachOfType<Asset>([&](Handle h, const Asset& a) {
        if (a.hidden) return;
        rows.emplace_back(h, &a);
    });

    bool picked = false;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& [h, a] = rows[i];
            ImGui::PushID(static_cast<int>(h.id()));
            const bool sel = currentHandle && currentHandle.id() == h.id();
            if (ImGui::Selectable(a->name.empty() ? "(unnamed)" : a->name.c_str(), sel)) {
                currentHandle = h;
                picked = true;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndCombo();
    return picked;
}
}

void InspectorPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    drawPanelTitle("Inspector");

    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) {
        // Centered empty-state with a large neutral glyph + two-line hint and
        // a quick "create entity" affordance, so a fresh user has somewhere
        // to go from a blank panel.
        const ImVec2 region = ImGui::GetContentRegionAvail();
        const float glyphSize = 56.0f;
        const float lineH     = ImGui::GetTextLineHeightWithSpacing();
        const float blockH    = glyphSize + lineH * 2.0f + ImGui::GetFrameHeight() + 24.0f;
        ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (region.y - blockH) * 0.35f)));

        const ImVec2 cur = ImGui::GetCursorScreenPos();
        const float iconCx = cur.x + region.x * 0.5f;
        ImGui::Dummy(ImVec2(0.0f, glyphSize));
        drawEditorIcon(ImGui::GetWindowDrawList(), EditorIcon::Select,
            ImVec2(iconCx, cur.y + glyphSize * 0.5f), glyphSize * 0.40f,
            ImGui::GetColorU32(ImGuiCol_TextDisabled));

        ImGui::Spacing();
        const char* line1 = "No entity selected";
        const char* line2 = "Pick one in the Hierarchy, or click in the viewport.";
        ImGui::SetCursorPosX((region.x - ImGui::CalcTextSize(line1).x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
        ImGui::TextUnformatted(line1);
        ImGui::PopStyleColor();
        ImGui::SetCursorPosX((region.x - ImGui::CalcTextSize(line2).x) * 0.5f);
        ImGui::TextDisabled("%s", line2);

        ImGui::Spacing();
        const float btnW = 180.0f;
        ImGui::SetCursorPosX((region.x - btnW) * 0.5f);
        if (ImGui::Button("+  Create Entity", ImVec2(btnW, 0.0f)))
            ImGui::OpenPopup("##EmptyCreate");
        if (ImGui::BeginPopup("##EmptyCreate")) {
            EditorActions::drawCreateEntityMenu(ctx.scene, ctx.resources, state);
            ImGui::EndPopup();
        }
        return;
    }

    auto& scene = ctx.scene;
    EntityId id = state.selectedEntity;

    // Identity header: type badge + name (or "Add name" affordance) + id.
    // Naming is opt-in - the inspector never adds Name during draw, only on
    // explicit user action, so a glance at an entity doesn't mutate the scene.
    {
        const float ih = ImGui::GetFrameHeight();
        inlineIcon(entityIconKind(scene, id), ih,
                   ImGui::GetColorU32(EditorStyle::ACCENT));
        ImGui::SameLine();

        if (scene.has<Name>(id)) {
            auto& name = scene.get<Name>(id);
            const Name before = name;
            ImGui::SetNextItemWidth(-46.0f);
            if (ImGui::InputText("##Name", name.value, sizeof(name.value))) {
                // Route through the command stack like every other inspector
                // edit: tryMerge coalesces the keystroke stream into one undo
                // step, and markSceneDirty stops the rename from being silently
                // lost on close (it used to do neither).
                state.commands.push(std::make_unique<ComponentEditCommand<Name>>(
                    id, before, name, "Rename"));
                state.markSceneDirty();
            }
        } else {
            char fallback[64];
            getEntityDisplayName(scene, id, fallback, sizeof(fallback));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(fallback);
            ImGui::SameLine();
            if (ImGui::SmallButton("+##addname")) {
                Name n(fallback);
                scene.add(Entity{id}, n);
                state.commands.push(std::make_unique<AddComponentCommand<Name>>(id, n, "Add Name"));
                state.markSceneDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a Name component to rename this entity");
            ImGui::SameLine(0.0f, ImGui::GetContentRegionAvail().x - 46.0f);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("#%u", id.index);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (scene.has<Transform>(id))  drawTransformSection(scene, state, id);
    if (scene.has<Mesh>(id))       drawMeshSection(scene, ctx.resources, state, id);
    if (scene.has<Light>(id))      drawLightSection(scene, state, id);
    if (scene.has<Rigidbody>(id))  drawRigidbodySection(scene, state, id);
    if (scene.has<Collider>(id))   drawColliderSection(scene, ctx.resources, state, id);
    if (scene.has<Camera>(id))     drawCameraSection(scene, state, id);
    if (scene.has<Animation>(id))  drawAnimationSection(scene, state, id);
    if (scene.has<Hierarchy>(id))  drawHierarchySection(scene, state, id);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAddComponentMenu(scene, state, id);
}

void InspectorPanel::drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id) {
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    const bool clicked = ImGui::Button("+  Add Component", ImVec2(-1, 0));
    ImGui::PopStyleColor(2);
    if (clicked) ImGui::OpenPopup("##AddComp");

    if (ImGui::BeginPopup("##AddComp")) {
        ImGui::TextDisabled("Add Component");
        ImGui::Separator();
        // Each add routes through AddComponentCommand so undo can drop the
        // component the user just added. The component value captured in
        // the command is the same one we add to the scene.
        if (!scene.has<Mesh>(id) && ImGui::MenuItem("Mesh")) {
            Mesh m{};
            scene.add(Entity{id}, m);
            state.commands.push(std::make_unique<AddComponentCommand<Mesh>>(id, m, "Add Mesh"));
            state.markSceneDirty();
        }
        if (!scene.has<Light>(id) && ImGui::MenuItem("Light")) {
            Light l = generatePointLight();
            scene.add(Entity{id}, l);
            state.commands.push(std::make_unique<AddComponentCommand<Light>>(id, l, "Add Light"));
            state.markSceneDirty();
        }
        if (!scene.has<Rigidbody>(id) && ImGui::MenuItem("Rigidbody")) {
            Rigidbody rb{};
            scene.add(Entity{id}, rb);
            state.commands.push(std::make_unique<AddComponentCommand<Rigidbody>>(id, rb, "Add Rigidbody"));
            state.markSceneDirty();
        }
        if (!scene.has<Collider>(id) && ImGui::MenuItem("Collider")) {
            Collider col{};
            scene.add(Entity{id}, col);
            state.commands.push(std::make_unique<AddComponentCommand<Collider>>(id, col, "Add Collider"));
            state.markSceneDirty();
        }
        if (!scene.has<Camera>(id) && ImGui::MenuItem("Camera")) {
            Camera cam;
            cam.active = false;
            scene.add(Entity{id}, cam);
            state.commands.push(std::make_unique<AddComponentCommand<Camera>>(id, cam, "Add Camera"));
            state.markSceneDirty();
        }
        if (!scene.has<Animation>(id) && ImGui::MenuItem("Animation")) {
            Animation a{};
            scene.add(Entity{id}, a);
            state.commands.push(std::make_unique<AddComponentCommand<Animation>>(
                id, std::move(a), "Add Animation"));
            state.markSceneDirty();
        }
        ImGui::EndPopup();
    }
}

void InspectorPanel::drawTransformSection(Scene& scene, EditorState& state, EntityId id) {
    // Transform is intrinsic - no remove affordance.
    const bool open = beginComponentCard("Transform", ACCENT_TRANSFORM, true);
    if (open) {
        auto& t = scene.get<Transform>(id);
        const Transform before = t;  // pre-edit value for the coalescing undo command
        bool changed = false;
        changed |= drawVec3Control("Position", glm::value_ptr(t.position), 0.0f, 0.1f);

        m_eulerCache.sync(id, t.rotation);
        if (drawVec3Control("Rotation", m_eulerCache.degrees(), 0.0f, 0.5f)) {
            t.rotation = m_eulerCache.toQuat();
            changed = true;
        }

        changed |= drawVec3Control("Scale", glm::value_ptr(t.scale), 1.0f, 0.01f);

        if (changed) {
            // Coalescing command - tryMerge collapses the per-frame drag stream
            // into one undo step, mirroring the gizmo's drag-end push.
            state.commands.push(std::make_unique<TransformChangeCommand>(id, before, t, "Transform"));
            HierarchyOperations::markDirty(scene, id);
            state.markSceneDirty();
        }

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
    const bool open = beginComponentCard("Mesh", ACCENT_MESH, true, &remove);
    if (open) {
        auto& mesh = scene.get<Mesh>(id);
        const Mesh before = mesh;  // pre-edit value for the undo command
        bool changed = false;

        drawPropertyLabel("Visible");      changed |= ImGui::Checkbox("##MeshVis", &mesh.visible);
        drawPropertyLabel("Cast Shadow");  changed |= ImGui::Checkbox("##MeshShad", &mesh.castShadows);

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
        // pickAsset (anon namespace) is shared with the LOD section.
        changed |= pickAsset<MeshAsset>    ("##MeshPick", "Mesh Asset",     resources, mesh.mesh);
        changed |= pickAsset<MaterialAsset>("##MatPick",  "Material Asset", resources, mesh.material);

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
                if (MaterialHandle nh = EditorActions::duplicateMaterial(resources, state, mesh.material, &mesh)) {
                    state.materialEditorTarget = nh;
                    state.showMaterialEditor   = true;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fork the material and edit the copy safely");
        } else {
            ImGui::TextDisabled("No material assigned");
        }

        if (changed) {
            state.commands.push(std::make_unique<ComponentEditCommand<Mesh>>(id, before, mesh, "Edit Mesh"));
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        // Snapshot before removal so undo can restore the exact component.
        Mesh snap = scene.get<Mesh>(id);
        scene.remove<Mesh>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Mesh>>(id, snap, "Remove Mesh"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawLightSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Light", ACCENT_LIGHT, true, &remove);
    if (open) {
        auto& light = scene.get<Light>(id);
        const Light before = light;  // pre-edit value for the coalescing undo command
        bool changed = false;

        drawPropertyLabel("Type");
        int typeIdx = static_cast<int>(light.type);
        if (ImGui::Combo("##LType", &typeIdx, LIGHT_TYPE_NAMES, IM_ARRAYSIZE(LIGHT_TYPE_NAMES))) {
            light.type = static_cast<LightType>(typeIdx);
            changed = true;
        }

        drawPropertyLabel("Color");
        changed |= ImGui::ColorEdit3("##LColor", glm::value_ptr(light.color), ImGuiColorEditFlags_Float);

        // Intensity is unbounded on the upper end (HDR scenes routinely need
        // values in the hundreds for sun, thousands for studio lights). The
        // drag range only clamps soft; users can type any value.
        drawPropertyLabel("Intensity");
        changed |= ImGui::DragFloat("##LIntensity", &light.intensity, 0.5f, 0.0f, 100000.0f, "%.2f");

        if (light.type != LightType::Directional) {
            drawPropertyLabel("Radius");
            changed |= ImGui::DragFloat("##LRadius", &light.radius, 0.5f, 0.1f, 1000.0f, "%.1f");
        }

        if (light.type == LightType::Spot) {
            float innerDeg = glm::degrees(light.innerConeAngle);
            float outerDeg = glm::degrees(light.outerConeAngle);
            drawPropertyLabel("Inner Cone");
            if (ImGui::DragFloat("##InnerC", &innerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg")) {
                light.innerConeAngle = glm::radians(innerDeg);
                changed = true;
            }
            drawPropertyLabel("Outer Cone");
            if (ImGui::DragFloat("##OuterC", &outerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg")) {
                light.outerConeAngle = glm::radians(outerDeg);
                changed = true;
            }
        }

        if (light.type == LightType::Rect) {
            drawPropertyLabel("Width");
            changed |= ImGui::DragFloat("##LRectW", &light.areaWidth, 0.05f, 0.01f, 100.0f, "%.2f");
            drawPropertyLabel("Height");
            changed |= ImGui::DragFloat("##LRectH", &light.areaHeight, 0.05f, 0.01f, 100.0f, "%.2f");
            drawPropertyLabel("Two-sided");
            changed |= ImGui::Checkbox("##LRectTS", &light.twoSided);
        }
        if (light.type == LightType::Disk) {
            drawPropertyLabel("Disk Radius");
            changed |= ImGui::DragFloat("##LDiskR", &light.areaRadius, 0.05f, 0.01f, 100.0f, "%.2f");
            drawPropertyLabel("Two-sided");
            changed |= ImGui::Checkbox("##LDiskTS", &light.twoSided);
        }
        if (light.type == LightType::Rect || light.type == LightType::Disk) {
            ImGui::TextDisabled("LTC Lambertian diffuse + representative-point GGX specular.");
        }

        drawPropertyLabel("Shadows");  changed |= ImGui::Checkbox("##Shad", &light.castShadows);
        if (light.castShadows) {
            drawPropertyLabel("Shadow Bias");
            changed |= ImGui::DragFloat("##ShadBias", &light.shadowBias, 0.0005f, 0.0f, 0.1f, "%.4f");
            if (light.type == LightType::Directional) {
                drawPropertyLabel("Shadow Extent");
                changed |= ImGui::DragFloat("##ShadExt", &light.shadowExtent, 1.0f, 1.0f, 1000.0f, "%.1f");
            }
        }
        drawPropertyLabel("Enabled");  changed |= ImGui::Checkbox("##LEn", &light.enabled);

        if (changed) {
            state.commands.push(std::make_unique<ComponentEditCommand<Light>>(id, before, light, "Edit Light"));
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        Light snap = scene.get<Light>(id);
        scene.remove<Light>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Light>>(id, snap, "Remove Light"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawRigidbodySection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Rigidbody", ACCENT_PHYSICS, true, &remove);
    if (open) {
        auto& rb = scene.get<Rigidbody>(id);
        const Rigidbody before = rb;
        bool changed = false;

        drawPropertyLabel("Mass");
        changed |= ImGui::DragFloat("##RbMass", &rb.mass, 0.1f, 0.0f, 1000.0f, "%.2f");
        drawPropertyLabel("Static");      changed |= ImGui::Checkbox("##RbStatic", &rb.isStatic);
        drawPropertyLabel("Kinematic");   changed |= ImGui::Checkbox("##RbKinematic", &rb.isKinematic);

        drawPropertyLabel("Gravity Scale");
        changed |= ImGui::DragFloat("##RbGrav", &rb.gravityScale, 0.05f, 0.0f, 10.0f, "%.2f");
        drawPropertyLabel("Restitution");
        changed |= ImGui::DragFloat("##RbRest", &rb.restitution, 0.01f, 0.0f, 1.0f, "%.2f");
        drawPropertyLabel("Friction");
        changed |= ImGui::DragFloat("##RbFric", &rb.friction, 0.01f, 0.0f, 2.0f, "%.2f");
        drawPropertyLabel("Linear Damping");
        changed |= ImGui::DragFloat("##RbLinDamp", &rb.linearDamping, 0.005f, 0.0f, 1.0f, "%.3f");
        drawPropertyLabel("Angular Damping");
        changed |= ImGui::DragFloat("##RbAngDamp", &rb.angularDamping, 0.005f, 0.0f, 1.0f, "%.3f");

        changed |= drawVec3Control("Velocity", glm::value_ptr(rb.linearVelocity), 0.0f, 0.1f);

        if (changed) {
            // Wake the body so the edit (especially velocity) survives the next
            // tick - otherwise PhysicsSystem zeroes a sleeping body's velocity.
            rb.sleeping = false;
            rb.sleepTimer = 0.0f;
            state.commands.push(std::make_unique<ComponentEditCommand<Rigidbody>>(id, before, rb, "Edit Rigidbody"));
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        Rigidbody snap = scene.get<Rigidbody>(id);
        scene.remove<Rigidbody>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Rigidbody>>(id, snap, "Remove Rigidbody"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawColliderSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Collider", ACCENT_COLLIDER, true, &remove);
    if (open) {
        auto& col = scene.get<Collider>(id);
        const Collider before = col;
        bool changed = false;

        // A collider is a set of boxes. A single box is editable here; a
        // mesh-fitted compound shows its box count (rebuild it via Fit to Mesh).
        if (col.parts.size() == 1) {
            changed |= drawVec3Control("Half Extents",
                glm::value_ptr(col.parts[0].halfExtents), 0.5f, 0.05f);
        } else {
            ImGui::TextDisabled("%zu boxes (mesh-fitted)", col.parts.size());
        }

        // Fit to Mesh: rebuild the collider from this entity's mesh. Detail 1 is
        // a single box (the scaled bounds); higher detail voxelizes the mesh into
        // a box compound that hugs its shape. The entity scale is baked in - the
        // solver ignores Transform scale.
        if (scene.has<Mesh>(id) && scene.get<Mesh>(id).mesh) {
            const auto& asset = resources.get(scene.get<Mesh>(id).mesh);
            if (hasValidBounds(asset.boundsMin, asset.boundsMax)) {
                ImGui::Spacing();
                drawPropertyLabel("Detail");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderInt("##ColDetail", &state.colliderFitDetail, 1, COLLIDER_FIT_MAX_DETAIL);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("1 = one box; higher = a tighter box compound (more boxes = heavier)");
                if (ImGui::Button("Fit to Mesh", ImVec2(-1.0f, 0.0f))) {
                    const glm::vec3 scale = scene.has<Transform>(id)
                        ? scene.get<Transform>(id).scale : glm::vec3(1.0f);
                    col.parts = fitBoxesToMesh(asset, state.colliderFitDetail, scale);
                    changed = true;
                }
            }
        }

        drawPropertyLabel("Trigger");  changed |= ImGui::Checkbox("##ColTrigger", &col.isTrigger);

        if (changed) {
            state.commands.push(std::make_unique<ComponentEditCommand<Collider>>(id, before, col, "Edit Collider"));
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        Collider snap = scene.get<Collider>(id);
        scene.remove<Collider>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Collider>>(id, snap, "Remove Collider"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawCameraSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Camera", ACCENT_CAMERA, true, &remove);
    if (open) {
        auto& cam = scene.get<Camera>(id);
        const Camera before = cam;  // pre-edit value for the undo command
        bool changed = false;

        drawPropertyLabel("Projection");
        int projIdx = static_cast<int>(cam.projection);
        if (ImGui::Combo("##CProj", &projIdx, PROJECTION_TYPE_NAMES, IM_ARRAYSIZE(PROJECTION_TYPE_NAMES))) {
            cam.projection = static_cast<ProjectionType>(projIdx);
            changed = true;
        }

        if (cam.projection == ProjectionType::Perspective) {
            float fovDeg = glm::degrees(cam.fovY);
            drawPropertyLabel("FOV");
            if (ImGui::SliderFloat("##CFOV", &fovDeg, 10.0f, 170.0f, "%.0f deg")) {
                cam.fovY = glm::radians(fovDeg);
                changed = true;
            }
        } else {
            drawPropertyLabel("Ortho Height");
            changed |= ImGui::DragFloat("##COrthoH", &cam.orthoHeight, 0.1f, 0.1f, 1000.0f);
        }

        drawPropertyLabel("Near Clip"); changed |= ImGui::DragFloat("##CNear", &cam.zNear, 0.01f, 0.001f, cam.zFar, "%.3f");
        drawPropertyLabel("Far Clip");  changed |= ImGui::DragFloat("##CFar", &cam.zFar, 1.0f, cam.zNear, 100000.0f, "%.0f");
        drawPropertyLabel("Exposure");  changed |= ImGui::DragFloat("##CExp", &cam.exposure, 0.01f, 0.0f, 10.0f, "%.2f");
        drawPropertyLabel("Active");    changed |= ImGui::Checkbox("##CAct", &cam.active);

        // "Set as Main" flips active across every camera; record each prior
        // active flag so the multi-entity change is undoable.
        if (ImGui::Button("Set as Main Camera", ImVec2(-1, 0))) {
            std::vector<std::pair<uint32_t, bool>> beforeActive;
            scene.forEach<Camera>([&](EntityId other, Camera& c) {
                beforeActive.emplace_back(other.index, c.active);
                c.active = (other == id);
            });
            state.commands.push(std::make_unique<SetActiveCameraCommand>(
                id, std::move(beforeActive), "Set Main Camera"));
            state.markSceneDirty();
        }

        if (changed) {
            state.commands.push(std::make_unique<ComponentEditCommand<Camera>>(id, before, cam, "Edit Camera"));
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        Camera snap = scene.get<Camera>(id);
        scene.remove<Camera>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Camera>>(id, snap, "Remove Camera"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawAnimationSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Animation", ACCENT_ANIM, true, &remove);
    if (open) {
        auto& anim = scene.get<Animation>(id);
        // Undo snapshot. Only authoring edits (length, keyframes) push a command;
        // play/pause/stop/scrub never set `changed`, so they stay non-undoable.
        // The snapshot does include time/playing, so undoing an authoring edit
        // also restores the scrub position - acceptable since edits are normally
        // made while paused.
        const Animation before = anim;

        const float GAP = 8.0f;
        float ih = ImGui::GetFrameHeight();
        if (iconButton("inspPlay", anim.playing ? EditorIcon::Pause : EditorIcon::Play,
                       anim.playing, true, anim.playing ? "Pause" : "Play", ih))
            anim.playing = !anim.playing;
        ImGui::SameLine(0, GAP);
        if (iconButton("inspStop", EditorIcon::Stop, false, true, "Stop (rewind)", ih)) {
            anim.playing = false;
            anim.time = 0.0f;
        }
        ImGui::SameLine(0, GAP);
        ImGui::Checkbox("Loop", &anim.looping);
        ImGui::SameLine(0, GAP);
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##ASpeed", &anim.speed, 0.005f, 0.0f, 10.0f, "Speed %.2fx");

        bool changed = false;

        // Explicit minimum length holds the clip open past the last keyframe
        // (0 = auto, derived from the keyframes). Folds into `duration` via
        // updateDuration() so the scrubber and playback see it immediately.
        drawPropertyLabel("Length");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##ALength", &anim.length, 0.02f, 0.0f, 100000.0f, "%.2f s  (0 = auto)")) {
            if (anim.length < 0.0f) anim.length = 0.0f;
            anim.updateDuration();
            changed = true;
        }

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

        // Keyframe authoring: "+ Key" snapshots the entity's current Transform
        // value at the current scrub time; each row edits a keyframe's
        // time/value or deletes it. Structural edits (time move / delete) shift
        // indices, so we break and redraw next frame. Rotation keys edit as
        // Euler degrees (re-derived each frame - fine for authoring, no gimbal
        // cache).
        glm::vec3 curPos(0.0f), curScale(1.0f);
        glm::quat curRot(1.0f, 0.0f, 0.0f, 0.0f);
        if (scene.has<Transform>(id)) {
            const Transform& tr = scene.get<Transform>(id);
            curPos = tr.position; curRot = tr.rotation; curScale = tr.scale;
        }

        auto vec3Track = [&](const char* label, AnimationTrack<glm::vec3>& track,
                             const glm::vec3& current) -> bool {
            bool ch = false;
            ImGui::PushID(label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Key")) { track.setKeyframe(anim.time, current); ch = true; }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Key the current value at t = %.2fs", anim.time);
            EasingFunction e = track.getEasing();
            if (drawEasingCombo("##ease", e)) { track.setEasing(e); ch = true; }

            const std::vector<float>     times = track.getTimes();   // copies: safe to mutate track in-loop
            const std::vector<glm::vec3> vals  = track.getValues();
            for (size_t i = 0; i < times.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                bool structural = false;
                float t = times[i];
                ImGui::SetNextItemWidth(64.0f);
                if (ImGui::DragFloat("##t", &t, 0.02f, 0.0f, 100000.0f, "%.2fs")) {
                    track.setKeyframeTime(i, t); ch = true; structural = true;
                }
                ImGui::SameLine();
                glm::vec3 v = vals[i];
                ImGui::SetNextItemWidth(-28.0f);
                if (!structural && ImGui::DragFloat3("##v", glm::value_ptr(v), 0.02f)) {
                    track.setKeyframeValue(i, v); ch = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) { track.removeKeyframe(i); ch = true; structural = true; }
                ImGui::PopID();
                if (structural) break;  // indices shifted - redraw next frame
            }
            ImGui::PopID();
            return ch;
        };

        auto quatTrack = [&](const char* label, AnimationTrack<glm::quat>& track,
                             const glm::quat& current) -> bool {
            bool ch = false;
            ImGui::PushID(label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Key")) { track.setKeyframe(anim.time, current); ch = true; }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Key the current rotation at t = %.2fs", anim.time);
            EasingFunction e = track.getEasing();
            if (drawEasingCombo("##ease", e)) { track.setEasing(e); ch = true; }

            const std::vector<float>     times = track.getTimes();
            const std::vector<glm::quat> vals  = track.getValues();
            for (size_t i = 0; i < times.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                bool structural = false;
                float t = times[i];
                ImGui::SetNextItemWidth(64.0f);
                if (ImGui::DragFloat("##t", &t, 0.02f, 0.0f, 100000.0f, "%.2fs")) {
                    track.setKeyframeTime(i, t); ch = true; structural = true;
                }
                ImGui::SameLine();
                glm::vec3 deg = glm::degrees(glm::eulerAngles(vals[i]));
                ImGui::SetNextItemWidth(-28.0f);
                if (!structural && ImGui::DragFloat3("##v", glm::value_ptr(deg), 0.5f, 0.0f, 0.0f, "%.0f")) {
                    track.setKeyframeValue(i, glm::normalize(glm::quat(glm::radians(deg)))); ch = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) { track.removeKeyframe(i); ch = true; structural = true; }
                ImGui::PopID();
                if (structural) break;
            }
            ImGui::PopID();
            return ch;
        };

        if (ImGui::TreeNode("Keyframes")) {
            bool kch = false;
            kch |= vec3Track("Position", anim.positionTrack, curPos);
            ImGui::Separator();
            kch |= quatTrack("Rotation", anim.rotationTrack, curRot);
            ImGui::Separator();
            kch |= vec3Track("Scale", anim.scaleTrack, curScale);
            if (kch) { anim.updateDuration(); changed = true; }
            ImGui::TreePop();
        }

        if (changed) {
            state.commands.push(std::make_unique<ComponentEditCommand<Animation>>(id, before, anim, "Edit Animation"));
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        Animation snap = scene.get<Animation>(id);
        scene.remove<Animation>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Animation>>(
            id, std::move(snap), "Remove Animation"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawHierarchySection(Scene& scene, EditorState& state, EntityId id) {
    const bool open = beginComponentCard("Hierarchy", ACCENT_HIERARCHY, false);
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
                // Capture the parent before the detach so the reparent is
                // undoable (matches the Hierarchy panel's context-menu action).
                const EntityId oldParent = h.parent;
                HierarchyOperations::removeFromParent(scene, id);
                state.commands.push(std::make_unique<ReparentCommand>(
                    id, oldParent, EntityId{}, "Unparent"));
                EditorActions::commitHierarchyMutation(scene, state, id);
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
