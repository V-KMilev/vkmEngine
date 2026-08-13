#include "panels/inspector_panel.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ecs/component/animation.h"
#include "ecs/component/collider.h"
#include "ecs/component/decal.h"
#include "ecs/component/particle_emitter.h"
#include "ecs/component/irradiance_volume.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/transform.h"
#include "ecs/component/ui_canvas.h"
#include "ecs/component/ui_element.h"
#include "ecs/component/ui_image.h"
#include "ecs/component/ui_text.h"
#include "ecs/component/ui_button.h"
#include "ecs/environment.h"
#include "framework/editor_actions.h"
#include "framework/editor_commands.h"
#include "framework/editor_common.h"
#include "generator/light_generators.h"
#include "io/project_paths.h"
#include "resource/resource_manager.h"
#include "system/physics/collider_fit.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"
#include "core/math/bounds.h"

namespace Engine {

namespace {
// Generic reflected-field -> ImGui inspector. The editor only sees a Behavior*,
// so a behavior's authored fields are edited through this visitor (the same
// bridge serialization uses). One DragFloat/Checkbox/etc. per field type;
// `changed` is set the frame any field is edited.
class BehaviorFieldInspector : public BehaviorFieldVisitor {
    public:
        bool changed = false;

        void field(const char* name, float& v) override {
            drawPropertyLabel(name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat(widgetId(name), &v, 0.1f)) changed = true;
        }
        void field(const char* name, int& v) override {
            drawPropertyLabel(name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragInt(widgetId(name), &v)) changed = true;
        }
        void field(const char* name, bool& v) override {
            drawPropertyLabel(name);
            if (ImGui::Checkbox(widgetId(name), &v)) changed = true;
        }
        void field(const char* name, glm::vec3& v) override {
            drawPropertyLabel(name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat3(widgetId(name), glm::value_ptr(v), 0.1f)) changed = true;
        }

        void enumField(const char* name, int& index, const char* const* names, std::size_t count) override {
            drawPropertyLabel(name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo(widgetId(name), &index, names, static_cast<int>(count))) changed = true;
        }

        // Nested struct: a collapsing tree node. When open it pushes an ID scope,
        // so sub-fields with the same name as a sibling struct's don't collide;
        // endStruct/TreePop runs only on the open path (beginStruct returned true).
        bool beginStruct(const char* name) override {
            return ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen);
        }
        void endStruct() override { ImGui::TreePop(); }

    private:
        // Hidden-label id for the widget; uniqueness across behaviors comes from
        // the per-behavior PushID in drawScriptSection.
        const char* widgetId(const char* name) {
            snprintf(m_id, sizeof(m_id), "##%s", name);
            return m_id;
        }
        char m_id[80] = {};
};

// Asset-reference combo: pick which loaded asset of type Asset a handle points
// at. Snapshots the asset list so ImGuiListClipper can window thousands of rows
// fluidly. Returns true if the selection changed. Used by the Mesh card's
// mesh + material pickers.
template <typename Asset, typename Handle>
bool pickAsset(const char* comboId, const char* label, ResourceManager& resources, Handle& currentHandle) {
    const std::string cur = (currentHandle && resources.isAlive(currentHandle))
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

// Shared scaffold for a removable, value-edited component card. Owns the
// remove affordance, the begin/end card pair, the get<T> + `before` snapshot,
// and the two undo pushes (ComponentEditCommand when a field changed, then
// RemoveComponentCommand if the card's remove button was pressed). `drawFields`
// receives the live component and returns whether any field was edited this
// frame. Behavior-identical to the hand-written sections it replaces: the edit
// push happens before endComponentCard, the remove push after.
template <typename T, typename DrawFields>
void editComponentCard(Scene& scene, EditorState& state, EntityId id,
                       const char* title, const ImVec4& accent,
                       const char* editLabel, const char* removeLabel,
                       DrawFields drawFields) {
    bool remove = false;
    const bool open = beginComponentCard(title, accent, true, &remove);
    if (open) {
        auto& component = scene.get<T>(id);
        const T before = component;  // pre-edit value for the undo command
        const bool changed = drawFields(component);
        if (changed) {
            state.commands.push(std::make_unique<ComponentEditCommand<T>>(id, before, component, editLabel));
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        // Snapshot before removal so undo can restore the exact component.
        T snap = scene.get<T>(id);
        scene.remove<T>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<T>>(id, snap, removeLabel));
        state.markSceneDirty();
    }
}
}

void InspectorPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    // Multi-selection: the cards below edit the ACTIVE entity; the banner
    // keeps the set visible (batch edits act via Delete/Duplicate/gizmo).
    if (state.selection.size() > 1) {
        ImGui::TextColored(EditorStyle::ACCENT, "%zu entities selected",
                           state.selection.size());
        ImGui::TextDisabled("Editing the active entity below.");
        ImGui::Separator();
    }

    const bool haveEntity = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
    if (!haveEntity) {
        // The World node (scene-global settings) is selected instead of an entity.
        if (state.worldSelected) drawWorldInspector(ec);
        else                     drawEmptySelectionState(ec);
        return;
    }

    Scene& scene = ctx.scene;
    EntityId id  = state.selectedEntity;

    drawIdentityHeader(scene, state, id);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (scene.has<Transform>(id))  drawTransformSection(scene, state, id);
    if (scene.has<Mesh>(id))       drawMeshSection(scene, ctx.resources, state, id);
    if (scene.has<Light>(id))      drawLightSection(scene, state, id);
    if (scene.has<Rigidbody>(id))  drawRigidbodySection(scene, state, id);
    if (scene.has<Collider>(id))   drawColliderSection(scene, ctx.resources, state, id);
    if (scene.has<Camera>(id))     drawCameraSection(scene, state, id);
    if (scene.has<ReflectionProbe>(id)) drawReflectionProbeSection(scene, state, id);
    if (scene.has<Decal>(id))          drawDecalSection(scene, ctx.resources, state, id);
    if (scene.has<ParticleEmitter>(id)) drawParticleSection(scene, state, id);
    if (scene.has<IrradianceVolume>(id)) drawIrradianceVolumeSection(scene, state, id);
    if (scene.has<Animation>(id))  drawAnimationSection(scene, state, id);
    if (scene.has<ScriptComponent>(id)) drawScriptSection(scene, state, id);
    if (scene.has<UICanvas>(id))   drawUICanvasSection(scene, state, id);
    if (scene.has<UIElement>(id))  drawUIElementSection(scene, state, id);
    if (scene.has<UIImage>(id))    drawUIImageSection(scene, state, id);
    if (scene.has<UIText>(id))     drawUITextSection(scene, state, id);
    if (scene.has<UIButton>(id))   drawUIButtonSection(scene, state, id);
    if (scene.has<Hierarchy>(id))  drawHierarchySection(scene, state, id);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAddComponentMenu(scene, state, id);
}

void InspectorPanel::drawEmptySelectionState(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    // Centered empty-state with a large neutral glyph + two-line hint and a
    // quick "create entity" affordance, so a fresh user has somewhere to go
    // from a blank panel.
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float glyphSize = EditorStyle::px(56.0f);
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
    const float btnW = EditorStyle::px(180.0f);
    ImGui::SetCursorPosX((region.x - btnW) * 0.5f);
    if (ImGui::Button("+  Create Entity", ImVec2(btnW, 0.0f)))
        ImGui::OpenPopup("##EmptyCreate");
    if (ImGui::BeginPopup("##EmptyCreate")) {
        EditorActions::drawCreateEntityMenu(ctx.scene, ctx.resources, state);
        ImGui::EndPopup();
    }
}

void InspectorPanel::drawIdentityHeader(Scene& scene, EditorState& state, EntityId id) {
    // Type badge, then the id, then the name bar filling the rest of the row:
    // [icon] #41 [name...............]. Naming is opt-in - the inspector never
    // adds Name during draw, only on explicit user action, so a glance at an
    // entity doesn't mutate the scene.
    const float ih = ImGui::GetFrameHeight();
    inlineIcon(entityIconKind(scene, id), ih, ImGui::GetColorU32(EditorStyle::ACCENT));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("#%u", id.index);
    ImGui::SameLine();

    if (scene.has<Name>(id)) {
        auto& name = scene.get<Name>(id);
        const Name before = name;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Name", name.value, sizeof(name.value))) {
            // Route through the command stack like every other inspector edit:
            // tryMerge coalesces the keystroke stream into one undo step, and
            // markSceneDirty stops the rename from being silently lost on close
            // (it used to do neither).
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
            Name n = makeName(fallback);
            scene.add(Entity{id}, n);
            state.commands.push(std::make_unique<AddComponentCommand<Name>>(id, n, "Add Name"));
            state.markSceneDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a Name component to rename this entity");
    }
}

void InspectorPanel::drawUICanvasSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<UICanvas>(scene, state, id, "UI Canvas", EditorStyle::Accent::UI, "Edit UI Canvas", "Remove UI Canvas",
        [&](UICanvas& c) {
            bool changed = false;
            changed |= propEnumCombo("Scale Mode", c.scaleMode);
            changed |= propDrag("Reference Height", &c.referenceHeight, 1.0f, 1.0f, 8192.0f, "%.0f",
                                "Authoring height; ScaleWithHeight scales the layout against it.");
            changed |= propDragInt("Sort Order", &c.sortOrder, 1.0f, -1000, 1000,
                                   "Higher draws on top across canvases.");
            changed |= propCheckbox("Visible", &c.visible);
            return changed;
        });
}

void InspectorPanel::drawUIElementSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<UIElement>(scene, state, id, "UI Element", EditorStyle::Accent::UI, "Edit UI Element", "Remove UI Element",
        [&](UIElement& e) {
            bool changed = false;
            changed |= propRow("Anchor", "Parent anchor point, 0..1 (top-left to bottom-right).",
                [&] { return ImGui::DragFloat2("##v", glm::value_ptr(e.anchor), 0.005f, 0.0f, 1.0f, "%.3f"); });
            changed |= propRow("Pivot", "Element pivot, 0..1; the point placed at the anchor.",
                [&] { return ImGui::DragFloat2("##v", glm::value_ptr(e.pivot), 0.005f, 0.0f, 1.0f, "%.3f"); });
            changed |= propRow("Position", "Offset from the anchor, in reference pixels.",
                [&] { return ImGui::DragFloat2("##v", glm::value_ptr(e.position), 0.5f, 0.0f, 0.0f, "%.1f"); });
            changed |= propRow("Size", "Element size, in reference pixels.",
                [&] { return ImGui::DragFloat2("##v", glm::value_ptr(e.size), 0.5f, 0.0f, 8192.0f, "%.1f"); });
            changed |= propCheckbox("Visible", &e.visible, "Hides this element and its whole subtree.");
            return changed;
        });
}

void InspectorPanel::drawUIImageSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<UIImage>(scene, state, id, "UI Image", EditorStyle::Accent::UI, "Edit UI Image", "Remove UI Image",
        [&](UIImage& i) {
            return propColor4("Color", glm::value_ptr(i.color));
        });
}

void InspectorPanel::drawUITextSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<UIText>(scene, state, id, "UI Text", EditorStyle::Accent::UI, "Edit UI Text", "Remove UI Text",
        [&](UIText& t) {
            bool changed = false;
            changed |= propString("Text", t.text);
            changed |= propString("Font", t.font, "Baked SDF font asset name (e.g. ui:roboto).");
            changed |= propDrag("Size", &t.pixelSize, 0.5f, 1.0f, 512.0f, "%.0f", "Text height in reference pixels.");
            changed |= propEnumCombo("Align", t.align);
            changed |= propEnumCombo("V Align", t.valign);
            changed |= propColor4("Color", glm::value_ptr(t.color));
            return changed;
        });
}

void InspectorPanel::drawUIButtonSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<UIButton>(scene, state, id, "UI Button", EditorStyle::Accent::UI, "Edit UI Button", "Remove UI Button",
        [&](UIButton& b) {
            bool changed = false;
            changed |= propString("Event Id", b.eventId, "Identifier the UIClickEvent carries when this button fires.");
            changed |= propCheckbox("Interactable", &b.interactable);
            changed |= propColor4("Normal", glm::value_ptr(b.normalColor));
            changed |= propColor4("Hover", glm::value_ptr(b.hoverColor));
            changed |= propColor4("Pressed", glm::value_ptr(b.pressedColor));
            changed |= propColor4("Disabled", glm::value_ptr(b.disabledColor));
            return changed;
        });
}

void InspectorPanel::drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id) {
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    const bool clicked = ImGui::Button("+  Add Component", ImVec2(-1, 0));
    ImGui::PopStyleColor(2);
    if (clicked) ImGui::OpenPopup("##AddComp");

    if (ImGui::BeginPopup("##AddComp")) {
        sectionLabel("Add Component");
        // Type-to-narrow: 16+ component types no longer fit one eyeful.
        // Focused on open, like the Hierarchy filter.
        static char s_componentFilter[48] = {};
        if (ImGui::IsWindowAppearing()) {
            s_componentFilter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(EditorStyle::px(200.0f));
        ImGui::InputTextWithHint("##compFilter", "Search...",
                                 s_componentFilter, sizeof(s_componentFilter));
        ImGui::Separator();
        // Each add routes through AddComponentCommand so undo can drop the
        // component the user just added. The component value captured in
        // the command is the same one we add to the scene. The generic lambda
        // collapses the otherwise-identical menu items; the component type is
        // deduced from the prototype value.
        auto addItem = [&](const char* label, auto value, const char* addLabel) {
            using T = decltype(value);
            if (!matchesFilter(label, s_componentFilter)) return;
            if (!scene.has<T>(id) && ImGui::MenuItem(label)) {
                scene.add(Entity{id}, value);
                state.commands.push(std::make_unique<AddComponentCommand<T>>(id, std::move(value), addLabel));
                state.markSceneDirty();
            }
        };

        addItem("Mesh", Mesh{}, "Add Mesh");
        addItem("Light", generatePointLight(), "Add Light");
        addItem("Rigidbody", Rigidbody{}, "Add Rigidbody");
        addItem("Collider", Collider{}, "Add Collider");
        Camera cam;
        cam.active = false;
        addItem("Camera", cam, "Add Camera");
        addItem("Reflection Probe", ReflectionProbe{}, "Add Reflection Probe");
        addItem("Decal", Decal{}, "Add Decal");
        addItem("Particle Emitter", ParticleEmitter{}, "Add Particle Emitter");
        addItem("Irradiance Volume", IrradianceVolume{}, "Add Irradiance Volume");
        addItem("Animation", Animation{}, "Add Animation");

        if (s_componentFilter[0] == '\0') {
            ImGui::Separator();
            sectionLabel("UI");
        }
        addItem("UI Canvas", UICanvas{}, "Add UI Canvas");
        addItem("UI Element", UIElement{}, "Add UI Element");
        addItem("UI Image", UIImage{}, "Add UI Image");
        addItem("UI Text", UIText{}, "Add UI Text");
        addItem("UI Button", UIButton{}, "Add UI Button");

        // ScriptComponent is move-only, so it can't ride the (value-copying)
        // AddComponentCommand - add it live, like the World/Physics edits.
        if (matchesFilter("Script", s_componentFilter)
            && !scene.has<ScriptComponent>(id) && ImGui::MenuItem("Script")) {
            scene.add(Entity{id}, ScriptComponent{});
            state.markSceneDirty();
        }
        ImGui::EndPopup();
    }
}

void InspectorPanel::drawTransformSection(Scene& scene, EditorState& state, EntityId id) {
    // Transform is intrinsic - no remove affordance.
    const bool open = beginComponentCard("Transform", EditorStyle::Accent::Transform, true);
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
    editComponentCard<Mesh>(scene, state, id, "Mesh", EditorStyle::Accent::Mesh, "Edit Mesh", "Remove Mesh",
                            [&](Mesh& mesh) {
        bool changed = false;

        changed |= propCheckbox("Visible", &mesh.visible);
        changed |= propCheckbox("Cast Shadow", &mesh.castShadows);

        if (mesh.mesh && resources.isAlive(mesh.mesh)) {
            const auto& asset = resources.get(mesh.mesh);
            ImGui::TextDisabled("%zu verts, %zu tris",
                asset.vertices.size(), asset.indices.size() / 3);
            if (Math::hasValidBounds(asset.boundsMin, asset.boundsMax)) {
                glm::vec3 ext = asset.boundsMax - asset.boundsMin;
                ImGui::TextDisabled("Bounds: %.1f x %.1f x %.1f", ext.x, ext.y, ext.z);
            }
        }

        ImGui::Spacing();

        // Asset pickers: swap which loaded mesh / material this component uses.
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

        return changed;
    });
}

void InspectorPanel::drawLightSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<Light>(scene, state, id, "Light", EditorStyle::Accent::Light, "Edit Light", "Remove Light",
                             [&](Light& light) {
        bool changed = false;

        changed |= propEnumCombo("Type", light.type);
        changed |= propColor3("Color", glm::value_ptr(light.color));

        // Intensity is unbounded on the upper end (HDR scenes routinely need
        // values in the hundreds for sun, thousands for studio lights). The
        // drag range only clamps soft; users can type any value.
        changed |= propDrag("Intensity", &light.intensity, 0.5f, 0.0f, 100000.0f, "%.2f");

        if (light.type != LightType::Directional)
            changed |= propDrag("Radius", &light.radius, 0.5f, 0.1f, 1000.0f, "%.1f");

        if (light.type == LightType::Spot) {
            if (propAngleDrag("Inner Cone", &light.innerConeAngle, 0.5f, 0.0f, 90.0f)) {
                changed = true;
            }
            if (propAngleDrag("Outer Cone", &light.outerConeAngle, 0.5f, 0.0f, 90.0f)) {
                changed = true;
            }
        }

        if (light.type == LightType::Rect) {
            changed |= propDrag("Width", &light.areaWidth, 0.05f, 0.01f, 100.0f, "%.2f");
            changed |= propDrag("Height", &light.areaHeight, 0.05f, 0.01f, 100.0f, "%.2f");
            changed |= propCheckbox("Two-sided", &light.twoSided);
        }
        if (light.type == LightType::Disk) {
            changed |= propDrag("Disk Radius", &light.areaRadius, 0.05f, 0.01f, 100.0f, "%.2f");
            changed |= propCheckbox("Two-sided", &light.twoSided);
        }
        if (light.type == LightType::Rect || light.type == LightType::Disk) {
        }

        changed |= propCheckbox("Shadows", &light.castShadows);
        if (light.castShadows) {
            changed |= propDrag("Shadow Bias", &light.shadowBias, 0.0005f, 0.0f, 0.1f, "%.4f");
            if (light.type == LightType::Directional)
                changed |= propDrag("Shadow Distance", &light.shadowDistance, 1.0f, 1.0f, 1000.0f, "%.1f");
        }
        changed |= propCheckbox("Enabled", &light.enabled);

        return changed;
    });
}

void InspectorPanel::drawWorldInspector(EditorContext& ec) {
    EditorState& state = ec.state;
    Environment& env   = ec.frame.scene.environment();

    // Identity header for the scene's World node.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("World");
    ImGui::SameLine();
    ImGui::TextDisabled(" Scene-global settings");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Every card edits the live Environment; `before` is snapshotted per card
    // so each edit lands as one undoable, mergeable command.
    const bool open = beginComponentCard("Environment", EditorStyle::Accent::Env, true);
    if (open) {
        bool changed = false;
        const Environment before = env;

        // Skybox HDR: browse assets/envs via the shared cached AssetPicker
        // instead of a bespoke per-open directory scan. The picker returns the
        // path relative to the project root, so the stored string stays
        // "assets/envs/<file>.hdr" - exactly what the combo wrote and what the
        // IBL baker loads relative to the working dir.
        drawPropertyLabel("Skybox HDR");
        ImGui::TextUnformatted(env.hdrPath.empty() ? "(none)" : env.hdrPath.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse...")) {
            const std::filesystem::path appRoot = ProjectPaths::root();
            m_envPicker.options.popupId    = "PickEnvHdr";
            m_envPicker.options.title      = "Pick Environment HDR";
            m_envPicker.options.root       = appRoot / "assets" / "envs";
            m_envPicker.options.recursive  = false;
            m_envPicker.options.kind       = AssetPicker::Kind::Files;
            m_envPicker.options.extensions = {".hdr"};
            m_envPicker.options.relativeTo = appRoot;
            m_envPicker.options.hint.clear();
            m_envPicker.open();
        }
        std::string pickedHdr;
        if (m_envPicker.draw(pickedHdr)) {
            // Normalize to forward slashes so the stored reference matches the
            // combo's generic_string() format across platforms.
            std::string rel = std::filesystem::path(pickedHdr).generic_string();
            if (rel != env.hdrPath) {
                env.hdrPath = rel;
                changed = true;
            }
        }

        changed |= propCheckbox("Show Skybox", &env.showSkybox);
        changed |= propSlider("Brightness", &env.intensity, 0.0f, 3.0f, "%.2f",
                              "Indirect (IBL) strength. Swapping the HDR re-bakes the IBL (a brief hitch).");

        if (changed) {
            state.commands.push(std::make_unique<EnvironmentEditCommand>(before, env, "Edit Environment"));
            state.markSceneDirty();
        }
    }
    endComponentCard();

    // Procedural sky: bakes a Rayleigh + Mie atmosphere into the IBL in place of
    // the HDR. Edits are live - the backend re-bakes when a value (or the sun)
    // changes - so a slow drag re-bakes each frame (a brief hitch, as noted).
    if (beginComponentCard("Procedural Sky", EditorStyle::Accent::Env, true)) {
        bool changed = false;
        const Environment before = env;

        changed |= propCheckbox("Enabled", &env.proceduralSky,
                                "Bakes a Rayleigh+Mie atmosphere instead of the HDR; the sun follows the scene's directional light");

        ImGui::BeginDisabled(!env.proceduralSky);
        changed |= propSlider("Sun Intensity", &env.skySunIntensity, 0.0f, 60.0f, "%.1f");
        changed |= propSlider("Rayleigh", &env.skyRayleigh, 0.0f, 4.0f, "%.2f");
        changed |= propSlider("Mie", &env.skyMie, 0.0f, 4.0f, "%.2f");
        changed |= propSlider("Mie Asymmetry", &env.skyMieG, 0.0f, 0.99f, "%.2f");
        changed |= propSlider("Sun Disc Size", &env.skySunAngularRadius, 0.002f, 0.1f, "%.3f");
        changed |= propSlider("Sun Disc Intensity", &env.skySunDiscIntensity, 0.0f, 60.0f, "%.1f");
        ImGui::EndDisabled();

        if (changed) {
            state.commands.push(std::make_unique<EnvironmentEditCommand>(before, env, "Edit Procedural Sky"));
            state.markSceneDirty();
        }
    }
    endComponentCard();

    // Volumetric fog: a froxel compute scatters the scene lights through a
    // height-falloff medium and applies it to the frame.
    if (beginComponentCard("Volumetric Fog", EditorStyle::Accent::Env, true)) {
        bool changed = false;
        const Environment before = env;

        changed |= propCheckbox("Enabled", &env.fogEnabled,
                                "Froxel fog: scatters the scene lights (incl. local lights) through a height-falloff medium");

        ImGui::BeginDisabled(!env.fogEnabled);
        changed |= propSlider("Density", &env.fogDensity, 0.0f, 0.3f, "%.3f");
        changed |= propDrag("Height", &env.fogHeight, 0.2f, -100.0f, 1000.0f, "%.1f");
        changed |= propSlider("Height Falloff", &env.fogHeightFalloff, 0.0f, 1.0f, "%.3f");
        changed |= propSlider("Anisotropy", &env.fogAnisotropy, -0.95f, 0.95f, "%.2f");
        changed |= propColor3("Albedo", glm::value_ptr(env.fogAlbedo));

        // Froxel grid dimensions: raise them when point-light shafts look blocky
        // (fog compute cost scales with X*Y*Z). Defaults 160x90x64.
        changed |= propDragU32("Froxels X", &env.fogResolutionX, 1.0f, 16u, 512u,
                               "Screen-horizontal froxels. More = sharper light shafts.");
        changed |= propDragU32("Froxels Y", &env.fogResolutionY, 1.0f, 16u, 512u);
        changed |= propDragU32("Froxels Z", &env.fogResolutionZ, 1.0f, 16u, 512u,
                               "Depth slices. More = smoother fog falloff with distance.");
        ImGui::EndDisabled();

        if (changed) {
            state.commands.push(std::make_unique<EnvironmentEditCommand>(before, env, "Edit Volumetric Fog"));
            state.markSceneDirty();
        }
    }
    endComponentCard();

    // Physics world parameters - also scene-global Environment state, read by
    // PhysicsSystem each fixed step.
    if (beginComponentCard("Physics", EditorStyle::Accent::Physics, true)) {
        bool changed = false;
        const Environment before = env;

        changed |= propDrag3("Gravity", glm::value_ptr(env.gravity), 0.05f, -50.0f, 50.0f, "%.2f");
        changed |= propDragInt("Solver Iterations", &env.solverIterations, 0.1f, 1, 32);

        if (changed) {
            state.commands.push(std::make_unique<EnvironmentEditCommand>(before, env, "Edit Physics"));
            state.markSceneDirty();
        }
    }
    endComponentCard();
}

void InspectorPanel::drawReflectionProbeSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<ReflectionProbe>(scene, state, id, "Reflection Probe", EditorStyle::Accent::Probe,
                                       "Edit Reflection Probe", "Remove Reflection Probe",
                                       [&](ReflectionProbe& probe) {
        bool changed = false;

        // Box half-extents: the influence + parallax-correction box. Should
        // roughly match the surrounding walls of the region the probe represents.
        changed |= propDrag3("Box Size", glm::value_ptr(probe.halfExtents), 0.1f, 0.1f, 1000.0f, "%.1f");
        changed |= propSlider("Falloff", &probe.falloff, 0.0f, 1.0f, "%.2f");
        changed |= propDrag("Intensity", &probe.intensity, 0.02f, 0.0f, 8.0f, "%.2f");

        // Capture resolution: the cube face size the probe bakes at (sharper
        // reflections cost more VRAM + bake time). The probe cubes share one
        // GPU array, so the highest resolution among all probes drives them all.
        static const char*    RES_LABELS[] = {"128", "256", "512", "1024"};
        static const uint32_t RES_VALUES[] = {128u, 256u, 512u, 1024u};
        changed |= propValueCombo("Resolution", RES_LABELS, RES_VALUES, 4, &probe.resolution,
                                  "Cube face size for the bake. Shared across probes: the "
                                  "largest wins. Changing it re-bakes every probe.");

        ImGui::Spacing();
        // Box / falloff / intensity are runtime blend params (no re-bake). Moving
        // the probe re-bakes automatically; Rebake forces it after the scene
        // changed (sun moved, geometry edited) by bumping the version.
        changed |= rebakeButton(probe.bakeVersion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Captures the scene from the entity's Transform position");

        return changed;
    });
}

void InspectorPanel::drawDecalSection(Scene& scene, ResourceManager& resources,
                                     EditorState& state, EntityId id) {
    editComponentCard<Decal>(scene, state, id, "Decal", EditorStyle::Accent::Mesh, "Edit Decal", "Remove Decal",
                             [&](Decal& decal) {
        bool changed = false;

        // The projected material: its albedo (with alpha) is what lands on the surface.
        changed |= pickAsset<MaterialAsset>("##DecalMatPick", "Material", resources, decal.material);
        changed |= propSlider("Angle Fade", &decal.angleFade, 0.0f, 1.0f, "%.2f",
                              "Fade where the surface turns away from the projector (projects along -Z; the Transform's scale is the box)");
        changed |= propSlider("Opacity", &decal.opacity, 0.0f, 1.0f, "%.2f");

        return changed;
    });
}

void InspectorPanel::drawParticleSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<ParticleEmitter>(scene, state, id, "Particle Emitter", EditorStyle::Accent::Light,
                                       "Edit Particle Emitter", "Remove Particle Emitter",
                                       [&](ParticleEmitter& e) {
        bool changed = false;

        changed |= propCheckbox("Emitting", &e.emitting);
        changed |= propDrag("Rate", &e.rate, 0.5f, 0.0f, 2000.0f, "%.1f");
        changed |= propDrag("Lifetime", &e.lifetime, 0.05f, 0.01f, 60.0f, "%.2f");

        changed |= propDragU32("Max Particles", &e.maxParticles, 1.0f, 1u, 20000u);

        ImGui::Spacing();
        changed |= propDrag3("Velocity", glm::value_ptr(e.velocity), 0.05f, -100.0f, 100.0f, "%.2f");
        changed |= propDrag("Spread", &e.spread, 0.02f, 0.0f, 50.0f, "%.2f");
        changed |= propDrag3("Acceleration", glm::value_ptr(e.acceleration), 0.05f, -100.0f, 100.0f, "%.2f");

        ImGui::Spacing();
        changed |= propColor4("Start Color", glm::value_ptr(e.startColor));
        changed |= propColor4("End Color", glm::value_ptr(e.endColor));
        changed |= propDrag("Start Size", &e.startSize, 0.005f, 0.0f, 20.0f, "%.3f");
        changed |= propDrag("End Size", &e.endSize, 0.005f, 0.0f, 20.0f, "%.3f");
        changed |= propSlider("Softness", &e.softness, 0.0f, 1.0f, "%.2f",
                              "Edge falloff: 1 = soft blob, 0 = hard-edged crisp disc");
        changed |= propCheckbox("Additive", &e.additive,
                                "Additive blend suits sparks/fire; alpha blend suits smoke");

        ImGui::TextDisabled("Live: %d particle(s).",
                            static_cast<int>(e.particles.size()));

        return changed;
    });
}

void InspectorPanel::drawIrradianceVolumeSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<IrradianceVolume>(scene, state, id, "Irradiance Volume", EditorStyle::Accent::Probe,
                                        "Edit Irradiance Volume", "Remove Irradiance Volume",
                                        [&](IrradianceVolume& v) {
        bool changed = false;

        changed |= propDrag3("Box Size", glm::value_ptr(v.halfExtents), 0.1f, 0.1f, 1000.0f, "%.1f");

        changed |= propDragU32("Probes X", &v.resolutionX, 0.1f, 1u, 64u);
        changed |= propDragU32("Probes Y", &v.resolutionY, 0.1f, 1u, 64u);
        changed |= propDragU32("Probes Z", &v.resolutionZ, 0.1f, 1u, 64u);

        changed |= propDrag("Intensity", &v.intensity, 0.02f, 0.0f, 8.0f, "%.2f");

        ImGui::Spacing();
        changed |= rebakeButton(v.bakeVersion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Each probe is a scene capture at bake time -\nthis costs bake time, not frame time");
        ImGui::TextDisabled("%u probes.",
                            v.resolutionX * v.resolutionY * v.resolutionZ);

        return changed;
    });
}

void InspectorPanel::drawRigidbodySection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<Rigidbody>(scene, state, id, "Rigidbody", EditorStyle::Accent::Physics,
                                 "Edit Rigidbody", "Remove Rigidbody",
                                 [&](Rigidbody& rb) {
        bool changed = false;

        changed |= propDrag("Mass", &rb.mass, 0.1f, 0.0f, 1000.0f, "%.2f");
        changed |= propCheckbox("Static", &rb.isStatic);
        changed |= propCheckbox("Kinematic", &rb.isKinematic);
        changed |= propDrag("Gravity Scale", &rb.gravityScale, 0.05f, 0.0f, 10.0f, "%.2f");
        changed |= propSlider("Restitution", &rb.restitution, 0.0f, 1.0f, "%.2f");
        changed |= propSlider("Friction", &rb.friction, 0.0f, 2.0f, "%.2f");
        changed |= propSlider("Linear Damping", &rb.linearDamping, 0.0f, 1.0f, "%.3f");
        changed |= propSlider("Angular Damping", &rb.angularDamping, 0.0f, 1.0f, "%.3f");

        changed |= drawVec3Control("Velocity", glm::value_ptr(rb.linearVelocity), 0.0f, 0.1f);
        changed |= drawVec3Control("Angular Vel", glm::value_ptr(rb.angularVelocity), 0.0f, 0.1f);
        changed |= propCheckbox("Freeze Rotation", &rb.freezeRotation,
                                "Translation only: contacts never torque the body (character controllers)");
        changed |= propCheckbox("Can Sleep", &rb.canSleep,
                                "Uncheck for script-driven bodies that must stay responsive at rest");

        if (changed) {
            // Wake the body so the edit (especially velocity) survives the next
            // tick - otherwise PhysicsSystem zeroes a sleeping body's velocity.
            // Applied to the live component before the card pushes the undo
            // command, so the wake is captured in the command's "after" value.
            rb.sleeping = false;
            rb.sleepTimer = 0.0f;
        }
        return changed;
    });
}

void InspectorPanel::drawColliderSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id) {
    editComponentCard<Collider>(scene, state, id, "Collider", EditorStyle::Accent::Collider,
                                "Edit Collider", "Remove Collider",
                                [&](Collider& col) {
        bool changed = false;

        // A collider is a set of boxes. A single box is editable here; a
        // mesh-fitted compound shows its box count (rebuild it via Fit to Mesh).
        changed |= propCheckbox("Enabled", &col.enabled,
                                "Disabled colliders are inert: no broadphase entry, no contacts");

        if (col.parts.size() == 1) {
            changed |= drawVec3Control("Center",
                glm::value_ptr(col.parts[0].center), 0.0f, 0.05f);
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
            if (Math::hasValidBounds(asset.boundsMin, asset.boundsMax)) {
                ImGui::Spacing();
                propSliderInt("Detail", &state.colliderFitDetail, 1, COLLIDER_FIT_MAX_DETAIL,
                    "1 = one box; higher = a tighter box compound (more boxes = heavier)");
                if (ImGui::Button("Fit to Mesh", ImVec2(-1.0f, 0.0f))) {
                    const glm::vec3 scale = scene.has<Transform>(id)
                        ? scene.get<Transform>(id).scale : glm::vec3(1.0f);
                    col.parts = fitBoxesToMesh(asset, state.colliderFitDetail, scale);
                    changed = true;
                }
            }
        }

        changed |= propCheckbox("Trigger", &col.isTrigger);

        return changed;
    });
}

void InspectorPanel::drawCameraSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<Camera>(scene, state, id, "Camera", EditorStyle::Accent::Camera, "Edit Camera", "Remove Camera",
                              [&](Camera& cam) {
        bool changed = false;

        changed |= propEnumCombo("Projection", cam.projection);

        if (cam.projection == ProjectionType::Perspective) {
            changed |= propAngleSlider("FOV", &cam.fovY, 10.0f, 170.0f);
        } else {
            changed |= propDrag("Ortho Height", &cam.orthoHeight, 0.1f, 0.1f, 1000.0f);
        }

        // Aspect: <= 0 tracks the viewport (the default); manual pins a ratio.
        bool autoAspect = cam.aspect <= 0.0f;
        if (propCheckbox("Auto Aspect", &autoAspect, "Derive the aspect ratio from the viewport each frame")) {
            cam.aspect = autoAspect ? 0.0f : 16.0f / 9.0f;
            changed = true;
        }
        if (!autoAspect)
            changed |= propDrag("Aspect", &cam.aspect, 0.01f, 0.1f, 10.0f, "%.3f");

        changed |= propDrag("Near Clip", &cam.zNear, 0.01f, 0.001f, cam.zFar, "%.3f");
        changed |= propDrag("Far Clip", &cam.zFar, 1.0f, cam.zNear, 100000.0f, "%.0f");
        changed |= propDrag("Exposure", &cam.exposure, 0.01f, 0.0f, 10.0f, "%.2f");

        // Depth of field: amount 0 disables the blur pass entirely.
        changed |= propDrag("Focus Distance", &cam.focusDistance, 0.1f, 0.01f, 10000.0f, "%.2f");
        changed |= propSlider("DoF Amount", &cam.dofAmount, 0.0f, 1.0f, "%.2f");
        changed |= propCheckbox("Active", &cam.active);

        if (ImGui::Button("Set as Main Camera", ImVec2(-1, 0))) {
            EditorActions::setActiveCamera(scene, state, id, "Set Main Camera");
        }

        return changed;
    });
}

void InspectorPanel::drawAnimationSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Animation", EditorStyle::Accent::Anim, true, &remove);
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
        if (propDrag("Length", &anim.length, 0.02f, 0.0f, 100000.0f, "%.2f s  (0 = auto)")) {
            anim.length = std::max(0.0f, anim.length);  // same clamp as the Bottom panel
            anim.updateDuration();
            changed = true;
        }

        if (anim.duration > 0.0f) {
            ImGui::SetNextItemWidth(-1);
            char timeFmt[32];
            snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", anim.duration);
            ImGui::SliderFloat("##ATime", &anim.time, 0.0f, anim.duration, timeFmt);
        }

        // Read-only keyframe summary. The full editable keyframe editor (add /
        // remove / retime / easing per track) lives in the Bottom panel's track
        // editor; duplicating it here drifted out of sync, so the inspector now
        // shows only a per-track digest and points the user at that editor.
        ImGui::Spacing();
        ImGui::TextUnformatted("Keyframes");
        auto trackSummary = [](const char* label, size_t count, float dur) {
            ImGui::BulletText("%s: %zu key%s, %.2fs", label, count, count == 1 ? "" : "s", dur);
        };
        trackSummary("Position", anim.positionTrack.keyframeCount(), anim.positionTrack.getDuration());
        trackSummary("Rotation", anim.rotationTrack.keyframeCount(), anim.rotationTrack.getDuration());
        trackSummary("Scale",    anim.scaleTrack.keyframeCount(),    anim.scaleTrack.getDuration());
        ImGui::TextDisabled("Edit keyframes in Bottom > Animation.");

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

void InspectorPanel::drawScriptSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Script", EditorStyle::Accent::Script, true, &remove);
    if (open) {
        auto& sc = scene.get<ScriptComponent>(id);

        // Script edits are applied live (no undo command): a behavior list is
        // move-only, so it can't ride the value-copying command stack.
        int removeIndex = -1;
        for (size_t i = 0; i < sc.behaviors.size(); ++i) {
            Behavior* behavior = sc.behaviors[i].get();
            if (!behavior) continue;
            ImGui::PushID(static_cast<int>(i));

            // Behavior header row in the card voice: emphasized name, the
            // remove affordance right-pinned like a card's own x.
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(EditorStyle::HEADER_TEXT, "%s", behavior->typeName());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - EditorStyle::px(14.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            const bool removeThis = ImGui::SmallButton("x##rmbeh");
            ImGui::PopStyleColor(2);
            if (removeThis) removeIndex = static_cast<int>(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove behavior");

            BehaviorFieldInspector inspector;
            behavior->visitFields(inspector);
            if (inspector.changed) state.markSceneDirty();

            ImGui::PopID();
            if (i + 1 < sc.behaviors.size()) ImGui::Separator();
        }

        if (sc.behaviors.empty()) ImGui::TextDisabled("No behaviors attached.");

        ImGui::Spacing();
        if (ImGui::Button("+  Add Behavior", ImVec2(-1, 0))) ImGui::OpenPopup("##AddBehavior");
        if (ImGui::BeginPopup("##AddBehavior")) {
            const std::vector<std::string> names = BehaviorRegistry::get().names();
            if (names.empty()) {
                ImGui::TextDisabled("No behaviors registered.");
            } else {
                // Same type-to-narrow affordance as Add Component.
                static char s_behaviorFilter[48] = {};
                if (ImGui::IsWindowAppearing()) {
                    s_behaviorFilter[0] = '\0';
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::SetNextItemWidth(EditorStyle::px(200.0f));
                ImGui::InputTextWithHint("##behaviorFilter", "Search...",
                                         s_behaviorFilter, sizeof(s_behaviorFilter));
                ImGui::Separator();
                for (const std::string& name : names) {
                    if (!matchesFilter(name.c_str(), s_behaviorFilter)) continue;
                    if (ImGui::MenuItem(name.c_str())) {
                        if (auto behavior = BehaviorRegistry::get().create(name)) {
                            sc.behaviors.push_back(std::move(behavior));
                            state.markSceneDirty();
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (removeIndex >= 0) {
            sc.behaviors.erase(sc.behaviors.begin() + removeIndex);
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        scene.remove<ScriptComponent>(Entity{id});
        state.markSceneDirty();
    }
}

void InspectorPanel::drawHierarchySection(Scene& scene, EditorState& state, EntityId id) {
    const bool open = beginComponentCard("Hierarchy", EditorStyle::Accent::Hierarchy, false);
    if (open) {
        const auto& h = scene.get<Hierarchy>(id);

        bool unparented = false;
        if (h.parent) {
            ImGui::TextDisabled("Parent:");
            ImGui::SameLine();
            char parentName[64];
            getEntityDisplayName(scene, h.parent, parentName, sizeof(parentName));
            if (ImGui::SmallButton(parentName)) {
                state.selectEntity(h.parent);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Unparent")) {
                EditorActions::reparentKeepingWorld(scene, state, id, EntityId{}, "Unparent");
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
                        state.selectEntity(child);
                    }
                });
            }
        }
    }
    endComponentCard();
}

} // namespace Engine
