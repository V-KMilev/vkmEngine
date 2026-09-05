#include "panels/inspector_panel.h"

#include <algorithm>
#include <climits>
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
#include <misc/cpp/imgui_stdlib.h>

#include "core/clock.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/missing_assets.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/environment.h"
#include "framework/component_edit.h"
#include "framework/editor_actions.h"
#include "framework/editor_commands.h"
#include "framework/editor_common.h"
#include "framework/prefab_overrides.h"
#include "resource/generate/light_generators.h"
#include "resource/generate/lod_generator.h"
#include "io/asset/asset_library.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "resource/resource_manager.h"
#include "resource/asset/font_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "system/audio/audio_system.h"
#include "system/camera/camera_controller_system.h"
#include "system/physics/authoring/collider_fit.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/physics/authoring/mesh_collider.h"
#include "system/physics/authoring/ragdoll_build.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/joint.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"
#include "ui/audition_transport.h"
#include "core/math/bounds.h"
#include "net/wire/schema.h"
#include "net/replication/silence.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief A component's heading, accent and history-entry text, from one row.
 *
 * The three labels are `static inline` strings rather than literals because
 * they are built from the heading; the commands they end up in hold a
 * `const char*`, which is why they have to live as long as the program does.
 */
template <typename T>
struct CardInfo;

#define VKM_INSPECTOR_CARD_INFO(Component, method, title, accent)              \
    template <> struct CardInfo<Component> {                                   \
        static constexpr const char* TITLE = title;                            \
        static const ImVec4& accentColor() { return EditorStyle::Accent::accent; } \
        static inline const std::string ADD    = std::string("Add ")    + title; \
        static inline const std::string EDIT   = std::string("Edit ")   + title; \
        static inline const std::string REMOVE = std::string("Remove ") + title; \
    };
VKM_INSPECTOR_CARDS(VKM_INSPECTOR_CARD_INFO)
#undef VKM_INSPECTOR_CARD_INFO

// The scale between the entity a collider is authored on and the node the art
// actually hangs from. An import puts the mesh on a child with a unit fix-up of
// its own - the lab's character is 1 and its model node 0.01 - and a shape
// fitted with the parent's scale comes out wrong by exactly that factor. Walked
// as local scales rather than decomposed from the relative matrix, so a
// mirrored node keeps its sign; a rotation between the two would make any
// single scale vector an approximation, and there is none in an import chain.
glm::vec3 meshScaleRelativeTo(const Scene& scene, EntityId collider, EntityId meshNode) {
    glm::vec3 scale(1.0f);
    EntityId at = meshNode;
    for (uint32_t step = 0; at && step < HierarchyOperations::MAX_DEPTH; ++step) {
        if (scene.has<Transform>(at)) scale *= scene.get<Transform>(at).scale;
        if (at == collider) break;
        at = scene.has<Hierarchy>(at) ? scene.get<Hierarchy>(at).parent : EntityId{};
    }
    return scale;
}

// How far the Camera card holds the two clip planes apart. They bound each
// other, but merely touching is already degenerate: glm::perspective divides by
// (zFar - zNear) and the cluster pass takes log(zFar / zNear).
constexpr float CLIP_PLANE_SEPARATION = 0.001f;

// Generic reflected-field -> ImGui inspector. The editor only sees a Behavior*,
// so a behavior's authored fields are edited through this visitor (the same
// bridge serialization uses).
class BehaviorFieldInspector : public BehaviorFieldVisitor {
    public:
        // Through propRow like every hand-written card row: it owns the label
        // column, the id scope and the width, so a behavior's fields line up
        // with the component fields above them by construction rather than by
        // two pieces of code agreeing.
        void field(const char* name, float& v) override {
            m_changed |= propRow(name, nullptr,
                [&] { return ImGui::DragFloat("##v", &v, 0.1f); });
        }
        void field(const char* name, int& v) override {
            m_changed |= propRow(name, nullptr, [&] { return ImGui::DragInt("##v", &v); });
        }
        void field(const char* name, bool& v) override {
            m_changed |= propRow(name, nullptr, [&] { return ImGui::Checkbox("##v", &v); });
        }
        void field(const char* name, glm::vec3& v) override {
            m_changed |= propRow(name, nullptr,
                [&] { return ImGui::DragFloat3("##v", glm::value_ptr(v), 0.1f); });
        }
        void field(const char* name, std::string& v) override {
            m_changed |= propRow(name, nullptr, [&] { return ImGui::InputText("##v", &v); });
        }

        void enumField(const char* name, int& index, const char* const* names, std::size_t count) override {
            m_changed |= propRow(name, nullptr,
                [&] { return ImGui::Combo("##v", &index, names, static_cast<int>(count)); });
        }

        // Asset reference: a combo over what the project's library holds of that
        // kind. Deliberately the library and not what is loaded, which is what
        // pickAsset lists: the name chosen here goes into the scene's assets
        // block on save, which is what makes the asset load in the first place.
        void assetField(const char* name, std::string& assetName, AssetType type) override {
            drawPropertyLabel(name);
            ImGui::PushID(name);
            if (ImGui::BeginCombo("##v", assetName.empty() ? "(none)" : assetName.c_str())) {
                // Built inside the combo, like pickAsset's: closed, it costs
                // nothing; open, it is the library as it stands this frame.
                static char s_assetFilter[48] = {};
                popupSearchField("##assetFilter", s_assetFilter, sizeof(s_assetFilter));

                if (ImGui::Selectable("(none)", assetName.empty())) {
                    assetName.clear();
                    m_changed = true;
                }
                for (const std::string& candidate : AssetLibrary::get().namesOf(type)) {
                    if (!matchesFilter(candidate.c_str(), s_assetFilter)) continue;
                    if (ImGui::Selectable(candidate.c_str(), candidate == assetName)) {
                        assetName = candidate;
                        m_changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            // The list offers only names the library has, so one that is missing
            // from it was deleted under the field or came from another project.
            // It will fail the next load; saying so here is earlier than that.
            if (!assetName.empty() && !AssetLibrary::get().find(type, assetName)) {
                ImGui::TextColored(EditorStyle::DANGER, "Not in this project's library.");
            }
            ImGui::PopID();
        }

        // Nested struct: a collapsing tree node. When open it pushes an ID scope,
        // so sub-fields with the same name as a sibling struct's don't collide;
        // endStruct/TreePop runs only on the open path (beginStruct returned true).
        bool beginStruct(const char* name) override {
            return ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen);
        }
        void endStruct() override { ImGui::TreePop(); }

        bool changed() const { return m_changed; }

    private:
        bool m_changed = false;
};

// Asset-reference combo: pick which loaded asset of type Asset a handle points
// at. Snapshots the asset list so ImGuiListClipper can window thousands of rows
// fluidly. Returns true if the selection changed.
template <typename Asset, typename Handle>
bool pickAsset(const char* comboId, const char* label, ResourceManager& resources, Handle& currentHandle) {
    const std::string cur = (currentHandle && resources.isAlive(currentHandle))
        ? resources.get(currentHandle).name() : std::string("(none)");
    drawPropertyLabel(label);
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo(comboId, cur.empty() ? "(unnamed)" : cur.c_str()))
        return false;

    std::vector<std::pair<Handle, const Asset*>> rows;
    resources.forEachOfType<Asset>([&](Handle h, const Asset& a) {
        if (a.isHidden()) return;
        rows.emplace_back(h, &a);
    });

    bool picked = false;

    // An empty slot is a state the editor hands you and one the scene file
    // round-trips, so the combo has to be able to get back to it.
    if (ImGui::Selectable("(none)", !currentHandle)) {
        currentHandle = Handle{};
        picked = true;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& [h, a] = rows[i];
            ImGui::PushID(static_cast<int>(h.id()));
            const bool sel = currentHandle && currentHandle.id() == h.id();
            if (ImGui::Selectable(a->name().empty() ? "(unnamed)" : a->name().c_str(), sel)) {
                currentHandle = h;
                picked = true;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndCombo();
    return picked;
}

// Bone combo: pick which joint of @p skeleton a socket rides. The list comes
// from the rig the Animator above the socket resolved - the same asset the
// Animator card reports a bone count for - because that is the only rig a
// socket parented there can address. A null skeleton draws the stored name
// disabled rather than an empty list, so a socket authored against a rig that
// is not loaded still shows what it is waiting for.
bool pickBone(const char* comboId, const SkeletonAsset* skeleton, std::string& bone) {
    drawPropertyLabel("Bone");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled(skeleton == nullptr);

    bool picked = false;
    if (ImGui::BeginCombo(comboId, bone.empty() ? "(none)" : bone.c_str())) {
        // BeginDisabled refuses a fresh click but not a popup opened on an
        // earlier frame, and the rig can go away underneath an open list - the
        // socket reparented, an undo - while every line below reads the pointer.
        if (!skeleton) {
            ImGui::CloseCurrentPopup();
        } else {
            static char s_boneFilter[48] = {};
            popupSearchField("##boneFilter", s_boneFilter, sizeof(s_boneFilter));

            // Clearing the bone is a state a socket passes through on its way from
            // one joint to another, so it is offered rather than reachable only by
            // deleting the component.
            if (ImGui::Selectable("(none)", bone.empty())) {
                bone.clear();
                picked = true;
            }

            // Indented by depth, because a rig is a tree; parent < index makes
            // that one forward pass. A filtered list is scattered matches, and
            // indenting those would draw a tree that is not there.
            const bool filtered = s_boneFilter[0] != '\0';
            std::vector<int> depth(skeleton->bones.size(), 0);
            std::vector<std::pair<const std::string*, int>> rows;
            for (size_t i = 0; i < skeleton->bones.size(); ++i) {
                const Bone& joint = skeleton->bones[i];
                depth[i] = joint.parent < 0 ? 0 : depth[joint.parent] + 1;
                if (matchesFilter(joint.name.c_str(), s_boneFilter)) {
                    rows.emplace_back(&joint.name, filtered ? 0 : depth[i]);
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto& [name, indent] = rows[i];
                    ImGui::PushID(i);
                    const std::string label = std::string(static_cast<size_t>(indent) * 2, ' ') + *name;
                    if (ImGui::Selectable(label.c_str(), *name == bone)) {
                        bone   = *name;
                        picked = true;
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::EndDisabled();
    return picked;
}

// The fields of one component this prefab instance has taken over, as a row
// each: the mark that a value is the instance's own rather than the prefab's,
// and the way to give it back. Nothing is drawn for an entity outside an
// instance, which is every entity in most scenes.
//
// Drawn before the fields below it, because a revert re-reads the component
// from the prefab and those widgets must show the restored value.
//
// A row is labelled by its serializer field key, which is what the widget below
// it is called on every card that has one. `rowLabel` is for the one place that
// has none: the name box in the identity header, whose field key is "value".
void drawOverrideRows(Scene& scene, ResourceManager& resources, EditorState& state,
                      EntityId id, const char* component, const char* rowLabel = nullptr) {
    const std::vector<std::string> fields =
        PrefabOverrides::overriddenFields(scene, id, component);
    if (fields.empty()) return;

    std::string revert;
    ImGui::TextColored(EditorStyle::Accent::Prefab, "Overridden by this instance");
    for (const std::string& field : fields) {
        drawPropertyLabel(rowLabel ? rowLabel : field.c_str());
        ImGui::PushID(field.c_str());
        if (ImGui::Button("Revert to prefab", ImVec2(-1.0f, 0.0f))) revert = field;
        ImGui::PopID();
    }
    ImGui::Separator();

    if (!revert.empty()) PrefabOverrides::revert(scene, resources, state, id, component, revert);
}

// A component whose removal is not just the component's. Ragdoll owns a subtree
// of bodies, and the card's own Clear is what takes them with it - offering the
// header's remove beside it would strand every bone and leave undo holding a
// component whose references all name something destroyed.
template <typename T>
constexpr bool CARD_OWNS_MORE_THAN_ITSELF = std::is_same_v<T, Ragdoll>;

// Shared scaffold for a removable, value-edited component card: the remove
// affordance, the begin/end card pair, the get<T> + `before` snapshot, and the
// two undo pushes (ComponentEditCommand when a field changed, then
// RemoveComponentCommand if the remove button was pressed). `drawFields`
// receives the live component and returns whether any field was edited.
// Ordering matters: the edit push happens before endComponentCard, the remove
// push after.
//
// The heading, the accent and both history entries come from CardInfo<T>, so a
// card states what it is once - in the list in the header - rather than four
// times in its own call.
template <typename T, typename DrawFields>
void editComponentCard(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id,
                       DrawFields drawFields) {
    bool remove = false;
    const bool open = beginComponentCard(CardInfo<T>::TITLE, CardInfo<T>::accentColor(), true,
                                         CARD_OWNS_MORE_THAN_ITSELF<T> ? nullptr : &remove);
    if (open) {
        drawOverrideRows(scene, resources, state, id, PrefabOverrides::COMPONENT_KEY<T>);

        auto& component = scene.get<T>(id);
        const T before = component;  // pre-edit value for the undo command
        const bool changed = drawFields(component);
        if (changed) {
            pushEdit<T>(scene, resources, state, id, before, component,
                        CardInfo<T>::EDIT.c_str());
        }
    }
    endComponentCard();
    if (remove) {
        // Snapshot before removal so undo can restore the exact component.
        T snap = scene.get<T>(id);
        scene.remove<T>(id);
        state.commands.push(std::make_unique<RemoveComponentCommand<T>>(
            id, std::move(snap), CardInfo<T>::REMOVE.c_str()));
        state.markSceneDirty();
        PrefabOverrides::warnComponentIsPrefabs(scene, state, id, CardInfo<T>::TITLE,
                                                "comes back from the prefab on the next load");
    }
}

// The line every UI content card owes when its entity carries no UIElement.
//
// The rect a UIImage, UIText or UIButton draws into is the UIElement's, and
// UISystem returns from resolveElement before it looks for any of the three
// when there is none. All three cards otherwise render in full - four button
// state colours, a text string, a colour swatch - for a component that is
// never reached, so the sentence is one thing said in one place.
void warnNoUIElement(const Scene& scene, EntityId id) {
    if (scene.has<UIElement>(id)) return;
    ImGui::TextColored(EditorStyle::DANGER, "No UI Element: nothing to give it a rect");
    ImGui::TextDisabled("Nothing draws for it until one is added.");
}

// The radius of the capsule an entity stands on, or 0 when it has none. The
// first capsule, because a character wears exactly one and a compound collider
// is a mesh fit, which is all boxes.
float capsuleRadiusOf(const Scene& scene, EntityId id) {
    if (!scene.has<Collider>(id)) return 0.0f;
    for (const ColliderPart& part : scene.get<Collider>(id).parts)
        if (part.shape == ColliderShape::Capsule) return part.radius;
    return 0.0f;
}
} // namespace

void InspectorPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    // The audition belongs to the card that started it, so selecting anything
    // else stops it - one voice, one owner, and the transport below stays honest.
    if (m_previewVoice != 0 && state.selectedEntity != m_previewOwner) {
        ec.audioSystem.device().stopVoice(m_previewVoice);
        m_previewVoice = 0;
    }

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

    drawIdentityHeader(scene, ctx.resources, state, id);

    // Retired here, where the banner is drawn, because a field is filled from
    // the picker, from the Asset Browser or by an undo; see
    // docs/reference/system/io.md, "A reference that did not resolve is kept".
    SceneSerializer::pruneResolvedRefs(scene, ctx.resources, id);
    if (scene.has<MissingAssets>(id)) {
        const MissingAssets& missing = scene.get<MissingAssets>(id);
        ImGui::TextColored(EditorStyle::DANGER, "%zu asset reference(s) here did not load:",
                           missing.refs.size());
        // Wrapped: an asset name is as often a path as a word, and the sentence
        // under them is a sentence.
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        for (const MissingAssetRef& ref : missing.refs) {
            ImGui::TextWrapped("  %s.%s  '%s'", ref.component.c_str(), ref.field.c_str(),
                               ref.name.c_str());
        }
        ImGui::TextWrapped("Kept as written: the save puts them back rather than emptying them.");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawPrefabSection(ec, id);

    // One walk over the card list, in its order: these rows and the header's
    // declarations both expand from VKM_INSPECTOR_CARDS, so a card cannot be
    // declared and left undrawn.
#define VKM_INSPECTOR_CARD_DRAW(Component, method, title, accent) \
    if (scene.has<Component>(id)) method(ec, id);
    VKM_INSPECTOR_CARDS(VKM_INSPECTOR_CARD_DRAW)
#undef VKM_INSPECTOR_CARD_DRAW

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAddComponentMenu(scene, state, id);
}

void InspectorPanel::drawEmptySelectionState(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    // Centered empty state, so a fresh user has somewhere to go from a blank
    // panel.
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float glyphSize = EditorStyle::px(56.0f);
    const float lineH     = ImGui::GetTextLineHeightWithSpacing();
    const float blockH    = glyphSize + lineH * 2.0f + ImGui::GetFrameHeight()
                          + EditorStyle::px(24.0f);
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
    // Centring a line wider than the panel puts its start left of the panel,
    // where the head of the sentence is clipped away rather than the tail.
    const auto centre = [&](const char* text) {
        ImGui::SetCursorPosX(std::max(0.0f, (region.x - ImGui::CalcTextSize(text).x) * 0.5f));
    };
    centre(line1);
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
    ImGui::TextUnformatted(line1);
    ImGui::PopStyleColor();
    centre(line2);
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

namespace {

// What the other end will see of this entity, said where an author is looking
// at it. Drawn only for a project that replicates something and only for an
// entity that carries some of it: most entities are not on the wire and do not
// need telling so every time they are selected.
void drawWireIdentity(const Scene& scene, EntityId id) {
    const NetSchema& schema = NetSchema::get();
    if (schema.size() == 0) return;

    std::string carried;
    for (const NetType& type : schema.types()) {
        if (!type.has || !type.has(scene, id)) continue;
        if (!carried.empty()) carried += ", ";
        carried += type.name;
    }
    if (carried.empty()) return;

    // The slot is the name on the wire and survives save and load, so this is
    // true of the file rather than only of this session.
    const NetSilence silence = netSilence(scene, id);
    if (silence != NetSilence::None) {
        static constexpr const char* WHY[] = {
            "", "inside a prefab instance", "the animation places it", "it cannot move"
        };
        static_assert(std::size(WHY) == static_cast<size_t>(NetSilence::Count),
                      "every reason an entity is off the wire needs a word for the author");

        ImGui::TextColored(EditorStyle::WARNING, "Not on the wire: %s.",
                           WHY[static_cast<size_t>(silence)]);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", toString(silence));
        return;
    }
    ImGui::TextDisabled("Wire slot %u  -  %s", id.slot(), carried.c_str());
}

} // namespace

void InspectorPanel::drawIdentityHeader(Scene& scene, ResourceManager& resources,
                                        EditorState& state, EntityId id) {
    // [icon] #41 [name...............]. Naming is opt-in - the inspector never
    // adds Name during draw, only on explicit user action, so a glance at an
    // entity doesn't mutate the scene.
    const float ih = ImGui::GetFrameHeight();
    inlineIcon(entityIconKind(scene, id), ih, ImGui::GetColorU32(EditorStyle::ACCENT));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("#%u", id.slot());
    ImGui::SameLine();

    if (scene.has<Name>(id)) {
        auto& name = scene.get<Name>(id);
        const Name before = name;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Name", name.value, sizeof(name.value))) {
            // Through the command stack like every other inspector edit:
            // tryMerge coalesces the keystrokes into one undo step, and
            // markSceneDirty keeps the rename from being lost on close.
            pushEdit<Name>(scene, resources, state, id, before, name, "Rename");
        }
        drawOverrideRows(scene, resources, state, id, PrefabOverrides::COMPONENT_KEY<Name>, "Name");
    } else {
        char fallback[64];
        getEntityDisplayName(scene, id, fallback, sizeof(fallback));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(fallback);
        ImGui::SameLine();
        if (ImGui::SmallButton("+##addname")) {
            Name n = makeName(fallback);
            scene.add(id, n);
            state.commands.push(std::make_unique<AddComponentCommand<Name>>(id, n, "Add Name"));
            state.markSceneDirty();
            PrefabOverrides::warnComponentIsPrefabs(scene, state, id, "Name",
                                                    "is not stored in the scene");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a Name component to rename this entity");
    }

    drawWireIdentity(scene, id);
}

void InspectorPanel::drawUICanvasSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<UICanvas>(scene, resources, state, id, [&](UICanvas& c) {
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

void InspectorPanel::drawUIElementSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<UIElement>(scene, resources, state, id, [&](UIElement& e) {
            bool changed = false;
            changed |= propRow("Anchor", "Parent anchor point, 0..1 (top-left to bottom-right).",
                [&] { return ImGui::DragFloat2("##v", glm::value_ptr(e.anchor), 0.005f, 0.0f, 1.0f, "%.3f", PROP_CLAMP); });
            changed |= propRow("Pivot", "Element pivot, 0..1; the point placed at the anchor.",
                [&] { return ImGui::DragFloat2("##v", glm::value_ptr(e.pivot), 0.005f, 0.0f, 1.0f, "%.3f", PROP_CLAMP); });
            changed |= propRow("Position", "Offset from the anchor, in reference pixels.",
                [&] { return ImGui::DragFloat2("##v", glm::value_ptr(e.position), 0.5f, 0.0f, 0.0f, "%.1f"); });
            changed |= propRow("Size", "Element size, in reference pixels.",
                [&] { return ImGui::DragFloat2("##v", glm::value_ptr(e.size), 0.5f, 0.0f, 8192.0f, "%.1f", PROP_CLAMP); });
            changed |= propCheckbox("Visible", &e.visible, "Hides this element and its whole subtree.");
            changed |= propCheckbox("Blocks pointer", &e.blocksPointer,
                "Stops a click reaching what is behind. Off for a decorative "
                "overlay meant to be clicked through. Only consulted on an "
                "element that draws - an image or a button.");

            // The default outcome of Create > UI with nothing selected; two
            // lines, like the Bone Socket card: what is wrong, then the fix.
            if (!hasCanvasAncestor(scene, id)) {
                ImGui::TextColored(EditorStyle::DANGER, "No UI Canvas above this: nothing draws");
                ImGui::TextDisabled("Drag this entity onto a canvas in the Hierarchy.");
            }
            return changed;
        });
}

void InspectorPanel::drawUIImageSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<UIImage>(scene, resources, state, id, [&](UIImage& i) {
            const bool changed = propColor4("Color", glm::value_ptr(i.color));
            warnNoUIElement(scene, id);
            return changed;
        });
}

void InspectorPanel::drawUITextSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<UIText>(scene, resources, state, id, [&](UIText& t) {
            bool changed = false;
            changed |= propString("Text", t.text);
            changed |= propString("Font", t.font, "Baked SDF font asset name (e.g. ui:roboto).");
            changed |= propDrag("Size", &t.pixelSize, 0.5f, 1.0f, 512.0f, "%.0f", "Text height in reference pixels.");
            changed |= propEnumCombo("Align", t.align);
            changed |= propEnumCombo("V Align", t.valign);
            changed |= propColor4("Color", glm::value_ptr(t.color));

            warnNoUIElement(scene, id);

            // An unresolved font draws nothing, which looks exactly like an
            // element that is hidden. No asset field reports this one - it is a
            // plain text box - so the card does.
            if (!resources.findByName<FontAsset>(t.font)) {
                // Wrapped, for the reason the listener card's warning is: the
                // name in it is one the author typed, and unwrapped it leaves
                // the panel at any default width.
                ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
                ImGui::TextWrapped("No font named '%s' is loaded",
                                   t.font.empty() ? "" : t.font.c_str());
                ImGui::PopStyleColor();
                ImGui::TextDisabled("Nothing draws until the name matches one.");
            }
            return changed;
        });
}

void InspectorPanel::drawUIButtonSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<UIButton>(scene, resources, state, id, [&](UIButton& b) {
            bool changed = false;
            changed |= propString("Event Id", b.eventId, "Identifier the UIClickEvent carries when this button fires.");
            changed |= propCheckbox("Interactable", &b.interactable);
            changed |= propColor4("Normal", glm::value_ptr(b.normalColor));
            changed |= propColor4("Hover", glm::value_ptr(b.hoverColor));
            changed |= propColor4("Pressed", glm::value_ptr(b.pressedColor));
            changed |= propColor4("Disabled", glm::value_ptr(b.disabledColor));

            warnNoUIElement(scene, id);
            return changed;
        });
}

void InspectorPanel::drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id) {
    // A component added inside an instance is in the prefab or it is nowhere,
    // and the button stays because writing the prefab back is how one is
    // authored - the same rule PrefabOverrides::warnComponentIsPrefabs states.
    const bool inInstance = PrefabOverrides::instanceRoot(scene, id) != EntityId{};
    if (inInstance) {
        ImGui::TextWrapped("Components on an instance belong to the prefab. Save as Prefab "
                           "keeps what you add here; saving the scene does not.");
    }

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
        popupSearchField("##compFilter", s_componentFilter, sizeof(s_componentFilter),
                         EditorStyle::px(200.0f));
        // The line above the button is gone by the time the menu is open, so the
        // add says it again, naming what was added.
        const auto warnPrefabOnly = [&](const char* label) {
            PrefabOverrides::warnComponentIsPrefabs(scene, state, id, label,
                                                    "is not stored in the scene");
        };

        // Headings rather than submenus: the filter searches the whole list, and
        // a match buried in a collapsed submenu could not be reached. A heading
        // is held back until an item under it draws, so it never labels a gap.
        const char* pendingSection = nullptr;
        bool        sectionDrawn   = false;
        const auto section = [&](const char* label) { pendingSection = label; };
        const auto drawPendingSection = [&] {
            if (pendingSection == nullptr) return;
            if (sectionDrawn) ImGui::Spacing();
            sectionLabel(pendingSection);
            pendingSection = nullptr;
            sectionDrawn   = true;
        };

        // Each add routes through AddComponentCommand so undo can drop it, and
        // the item's text and history entry come from CardInfo - so the menu and
        // the card it opens carry the same words by construction.
        auto addItem = [&](auto value) {
            using T = decltype(value);
            const char* label = CardInfo<T>::TITLE;
            if (scene.has<T>(id)) return;
            if (!matchesFilter(label, s_componentFilter)) return;

            drawPendingSection();
            if (!ImGui::MenuItem(label)) return;

            scene.add(id, value);
            state.commands.push(std::make_unique<AddComponentCommand<T>>(
                id, std::move(value), CardInfo<T>::ADD.c_str()));
            state.markSceneDirty();
            warnPrefabOnly(label);
        };

        // Sections, and the order within them, are the subject folders of
        // ecs/component/ - the menu and the tree teach one structure. Core and
        // prefab are absent: an entity is born with one, only prefabs write the other.
        section("Render");
        addItem(Mesh{});
        addItem(generateLight(LightType::Point));
        Camera cam;
        cam.active = false;
        addItem(cam);
        addItem(Decal{});
        addItem(LOD{});
        addItem(ParticleEmitter{});
        addItem(ReflectionProbe{});
        addItem(IrradianceVolume{});

        section("Animation");
        addItem(Animation{});
        addItem(Animator{});
        addItem(BoneSocket{});

        section("Audio");
        addItem(AudioSource{});
        addItem(AudioListener{});

        section("Physics");
        addItem(Rigidbody{});
        addItem(Collider{});
        addItem(CharacterController{});
        addItem(Joint{});
        addItem(Ragdoll{});

        section("UI");
        addItem(UICanvas{});
        addItem(UIElement{});
        addItem(UIImage{});
        addItem(UIText{});
        addItem(UIButton{});

        // ScriptComponent is move-only, so it cannot ride the value-copying
        // AddComponentCommand. ScriptEditCommand holds the serialized component
        // instead - the same step the Script card pushes for everything else.
        section("Script");
        if (!scene.has<ScriptComponent>(id)
                && matchesFilter(CardInfo<ScriptComponent>::TITLE, s_componentFilter)) {
            drawPendingSection();
            if (ImGui::MenuItem(CardInfo<ScriptComponent>::TITLE)) {
                scene.add(id, ScriptComponent{});
                state.commands.push(std::make_unique<ScriptEditCommand>(
                    id, std::string{}, ScriptEditCommand::capture(scene, id),
                    CardInfo<ScriptComponent>::ADD.c_str()));
                state.markSceneDirty();
                warnPrefabOnly(CardInfo<ScriptComponent>::TITLE);
            }
        }
        ImGui::EndPopup();
    }
}

void InspectorPanel::drawPrefabSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    EditorState& state = ec.state;

    const EntityId root = PrefabOverrides::instanceRoot(scene, id);
    if (!root) return;

    const bool open = beginComponentCard("Prefab Instance", EditorStyle::Accent::Prefab, true);
    if (open) {
        const PrefabInstance& instance = scene.get<PrefabInstance>(root);

        // Wrapped, not clipped: the path is the prefab's identity, and two
        // prefabs in different folders share a file name, so the tail is the
        // half a reader needs. A nested one is longer than the column.
        drawPropertyLabel("Source");
        ImGui::TextWrapped("%s", instance.source.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", instance.source.c_str());

        // Inside the subtree the override list is on the root, which is where
        // the total below counts and where the file is named.
        if (root != id) {
            drawPropertyLabel("Root");
            char rootName[64];
            getEntityDisplayName(scene, root, rootName, sizeof(rootName));
            if (ImGui::SmallButton(rootName)) state.selectEntity(root);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the instance root");
        }

        if (id == root && !scene.has<PrefabEntity>(id)) {
            // An expansion marks every entity it builds, root included, so a
            // root without the marker is one nothing was built from: the file
            // would not open. Answered before the scene-added-child branch below.
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextWrapped("The prefab file could not be opened, so this instance is "
                               "empty. Restore it and load the scene again - the reference "
                               "and any overrides are kept until then.");
            ImGui::PopStyleColor();
        } else if (!scene.has<PrefabEntity>(id)) {
            // Nothing here can be stored: the scene writes the instance as a
            // reference and skips its subtree, and the prefab has no entity to
            // rebuild this one from.
            ImGui::TextWrapped("Added to the scene, not to the prefab - this entity is "
                               "dropped when the scene is loaded again.");
        } else {
            const uint32_t uid = scene.get<PrefabEntity>(id).uid;
            size_t here = 0;
            for (const PrefabOverride& o : instance.overrides) {
                if (o.uid == uid) ++here;
            }
            if (instance.overrides.empty()) {
                ImGui::TextDisabled("No overrides. Editing a field here makes one.");
            } else {
                ImGui::TextDisabled("%zu override(s) here, %zu in the instance.",
                                    here, instance.overrides.size());
            }
        }
    }
    endComponentCard();
}

void InspectorPanel::drawTransformSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    // Transform is intrinsic - no remove affordance.
    const bool open = beginComponentCard(CardInfo<Transform>::TITLE,
                                         CardInfo<Transform>::accentColor(), true);
    if (open) {
        drawOverrideRows(scene, resources, state, id, PrefabOverrides::COMPONENT_KEY<Transform>);

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
            // tryMerge collapses the per-frame drag stream into one undo step,
            // mirroring the gizmo's drag-end push.
            pushEdit<Transform>(scene, resources, state, id, before, t, "Transform");
        }

        if (scene.has<Hierarchy>(id) && scene.get<Hierarchy>(id).parent) {
            glm::mat4 worldMat = HierarchyOperations::computeWorldMatrix(scene, id);
            glm::vec3 worldPos(worldMat[3]);
            ImGui::TextDisabled("World: (%.1f, %.1f, %.1f)",
                worldPos.x, worldPos.y, worldPos.z);
        }

        // A socket rewrites this every frame from the bone it rides, so an edit
        // here is gone by the next one. Say where the number that survives is.
        if (scene.has<BoneSocket>(id)) {
            ImGui::TextDisabled("Driven by Bone Socket - author Offset on that card.");
        }
    }
    endComponentCard();
}

void InspectorPanel::drawMeshSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<Mesh>(scene, resources, state, id, [&](Mesh& mesh) {
        bool changed = false;

        changed |= propCheckbox("Visible", &mesh.visible);
        changed |= propCheckbox("Cast Shadow", &mesh.castShadows);

        if (mesh.mesh && resources.isAlive(mesh.mesh)) {
            const auto& asset = resources.get(mesh.mesh);
            ImGui::TextDisabled("%zu verts, %zu tris",
                asset.vertices.size(), asset.indices.size() / 3);
            if (asset.bounds().valid()) {
                glm::vec3 ext = asset.boundsMax - asset.boundsMin;
                ImGui::TextDisabled("Bounds: %.1f x %.1f x %.1f", ext.x, ext.y, ext.z);
            }
            // The rig actually resolved, not the name the mesh remembers: the
            // two look identical until a bone count is shown against one.
            if (!asset.skin.empty()) {
                const SkeletonHandle rig = resources.findByName<SkeletonAsset>(asset.skeleton);
                if (rig) {
                    ImGui::TextDisabled("Skinned: %zu bones (%s)",
                        resources.get(rig).bones.size(), asset.skeleton.c_str());
                } else {
                    ImGui::TextDisabled("Skinned: rig '%s' not loaded", asset.skeleton.c_str());
                }
            }
        } else {
            // The harsher of the card's two absences: VisibilitySystem returns
            // on an empty mesh handle, so the entity is in no draw list at all.
            ImGui::TextColored(EditorStyle::DANGER, "No mesh: this entity draws nothing");
        }

        ImGui::Spacing();

        changed |= pickAsset<MeshAsset>    ("##MeshPick", "Mesh Asset",     resources, mesh.mesh);
        changed |= pickAsset<MaterialAsset>("##MatPick",  "Material Asset", resources, mesh.material);

        ImGui::Spacing();

        // Compact reference only - full PBR + texture editing and the live 3D
        // preview live one tab over, in the Material tab.
        if (mesh.material) {
            const MaterialAsset& m = resources.get(mesh.material);
            ImGui::TextDisabled("Material: %s",
                m.name().empty() ? "(unnamed)" : m.name().c_str());
            const float bw = (ImGui::GetContentRegionAvail().x
                              - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Edit Material", ImVec2(bw, 0))) {
                state.openMaterial(mesh.material);
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate", ImVec2(bw, 0))) {
                if (MaterialHandle nh = EditorActions::duplicateMaterial(resources, mesh.material)) {
                    Mesh after = mesh;
                    after.material = nh;
                    state.commands.push(std::make_unique<ComponentEditCommand<Mesh>>(
                        id, mesh, after, "Duplicate material"));
                    state.openMaterial(nh);
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

void InspectorPanel::drawLightSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    // While the procedural sky is on, SkySystem writes this light's rotation,
    // colour and intensity from the Environment every frame. Asked through
    // findKeyLight so the card and the system cannot disagree which light it is.
    const bool skyDriven = scene.environment().sky.procedural && findKeyLight(scene) == id;

    editComponentCard<Light>(scene, resources, state, id, [&](Light& light) {
        bool changed = false;

        if (skyDriven) {
            ImGui::TextWrapped("Procedural Sky drives this light: its rotation, colour and "
                               "intensity are written from World > Procedural Sky every "
                               "frame, and are what the scene saves.");
            ImGui::Spacing();
        }

        changed |= propEnumCombo("Type", light.type);

        // Disabled rather than hidden: they are still what this light is and
        // what the file records, just not the author's to set right now.
        ImGui::BeginDisabled(skyDriven);
        changed |= propColor3("Color", glm::value_ptr(light.color));

        // Generous rather than advisory: HDR scenes need hundreds for a sun and
        // thousands for a studio light, and PROP_CLAMP makes a row's declared
        // range the bound a typed value is held to.
        changed |= propDrag("Intensity", &light.intensity, 0.5f, 0.0f, 100000.0f, "%.2f");
        ImGui::EndDisabled();

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

        // The picker returns the path relative to the project root, so the
        // stored string stays "assets/envs/<file>.hdr" - what the IBL baker
        // loads relative to the working dir.
        drawPropertyLabel("Skybox HDR");
        ImGui::TextUnformatted(env.sky.hdrPath.empty() ? "(none)" : env.sky.hdrPath.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse...")) {
            const std::filesystem::path appRoot = ProjectPaths::projectRoot();
            m_envPicker.options().title      = "Pick Environment HDR";
            m_envPicker.options().root       = ProjectPaths::envs();
            m_envPicker.options().recursive  = false;
            m_envPicker.options().extensions = {".hdr"};
            m_envPicker.options().relativeTo = appRoot;
            m_envPicker.options().hint.clear();
            m_envPicker.open();
        }
        std::string pickedHdr;
        if (m_envPicker.draw(pickedHdr)) {
            // Normalize to forward slashes so the stored reference matches the
            // combo's generic_string() format across platforms.
            std::string rel = std::filesystem::path(pickedHdr).generic_string();
            if (rel != env.sky.hdrPath) {
                env.sky.hdrPath = rel;
                changed = true;
            }
        }

        changed |= propCheckbox("Show Skybox", &env.sky.showSkybox);
        changed |= propSlider("Brightness", &env.sky.intensity, 0.0f, 3.0f, "%.2f",
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

        changed |= propCheckbox("Enabled", &env.sky.procedural,
                                "Bakes a Rayleigh+Mie atmosphere instead of the HDR; the sun follows the scene's directional light");

        ImGui::BeginDisabled(!env.sky.procedural);

        changed |= propSlider("Sun Elevation", &env.sky.sunElevation, -90.0f, 90.0f, "%.0f deg",
                              "Degrees above the horizon. Below zero is night; the key light follows this");
        changed |= propSlider("Sun Azimuth", &env.sky.sunAzimuth, -180.0f, 180.0f, "%.0f deg",
                              "Degrees around the horizon");

        // The key light is the sky's while the procedural sky is on, so its
        // daylight look is authored here rather than on the Light.
        changed |= propColor3("Sun Light", glm::value_ptr(env.sky.lightColor),
                              ImGuiColorEditFlags_Float,
                              "Key light colour at midday - the sky drives the light, not the other way round");
        changed |= propSlider("Sun Light Intensity", &env.sky.lightIntensity, 0.0f, 20.0f, "%.2f");
        ImGui::Separator();

        changed |= propSlider("Sun Intensity", &env.sky.sunIntensity, 0.0f, 60.0f, "%.1f");
        changed |= propSlider("Rayleigh", &env.sky.rayleigh, 0.0f, 4.0f, "%.2f");
        changed |= propSlider("Mie", &env.sky.mie, 0.0f, 4.0f, "%.2f");
        changed |= propSlider("Mie Asymmetry", &env.sky.mieG, 0.0f, 0.99f, "%.2f");
        changed |= propSlider("Sun Disc Size", &env.sky.sunAngularRadius, 0.002f, 0.1f, "%.3f");
        changed |= propSlider("Sun Disc Intensity", &env.sky.sunDiscIntensity, 0.0f, 60.0f, "%.1f");

        // Night takes over on its own below the horizon, so there is nothing to
        // switch here - only what it looks like when it does.
        ImGui::Separator();
        changed |= propColor3("Night Skyglow", glm::value_ptr(env.night.radiance),
                              ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR,
                              "Lights the scene once the sun is down - the floor that keeps night dark rather than black");
        changed |= propSlider("Star Intensity", &env.night.starIntensity, 0.0f, 10.0f, "%.1f",
                              "0 removes the stars entirely");
        changed |= propSlider("Star Density", &env.night.starDensity, 20.0f, 400.0f, "%.0f",
                              "Higher packs in more stars, each smaller");
        changed |= propSlider("Moon Tilt", &env.night.moonTilt, -60.0f, 60.0f, "%.0f deg",
                              "How far off the point exactly opposite the sun the moon sits");
        changed |= propSlider("Moon Size", &env.night.moonAngularRadius, 0.002f, 0.2f, "%.3f");
        changed |= propSlider("Moon Intensity", &env.night.moonIntensity, 0.0f, 10.0f, "%.1f",
                              "How bright the moon looks; the halo around it follows");
        changed |= propColor3("Moonlight", glm::value_ptr(env.night.moonlightColor),
                              ImGuiColorEditFlags_Float,
                              "Key light colour after dark - the same light, aimed at the moon");
        changed |= propSlider("Moonlight Intensity", &env.night.moonlightIntensity, 0.0f, 2.0f, "%.2f",
                              "How much the moon lights the world. Real moonlight is a tiny fraction of daylight");
        ImGui::EndDisabled();

        if (changed) {
            state.commands.push(std::make_unique<EnvironmentEditCommand>(before, env, "Edit Procedural Sky"));
            state.markSceneDirty();
        }
    }
    endComponentCard();

    if (beginComponentCard("Volumetric Fog", EditorStyle::Accent::Env, true)) {
        bool changed = false;
        const Environment before = env;

        changed |= propCheckbox("Enabled", &env.fog.enabled,
                                "Froxel fog: scatters the scene lights (incl. local lights) through a height-falloff medium");

        ImGui::BeginDisabled(!env.fog.enabled);
        changed |= propSlider("Density", &env.fog.density, 0.0f, 0.3f, "%.3f");
        changed |= propDrag("Height", &env.fog.height, 0.2f, -100.0f, 1000.0f, "%.1f");
        changed |= propSlider("Height Falloff", &env.fog.heightFalloff, 0.0f, 1.0f, "%.3f");
        changed |= propSlider("Anisotropy", &env.fog.anisotropy, -0.95f, 0.95f, "%.2f");
        changed |= propColor3("Albedo", glm::value_ptr(env.fog.albedo));

        // Froxel grid dimensions: raise them when point-light shafts look blocky
        // (fog compute cost scales with X*Y*Z). Defaults 160x90x64.
        changed |= propDragU32("Froxels X", &env.fog.resolutionX, 1.0f, 16u, 512u,
                               "Screen-horizontal froxels. More = sharper light shafts.");
        changed |= propDragU32("Froxels Y", &env.fog.resolutionY, 1.0f, 16u, 512u);
        changed |= propDragU32("Froxels Z", &env.fog.resolutionZ, 1.0f, 16u, 512u,
                               "Depth slices. More = smoother fog falloff with distance.");
        ImGui::EndDisabled();

        if (changed) {
            state.commands.push(std::make_unique<EnvironmentEditCommand>(before, env, "Edit Volumetric Fog"));
            state.markSceneDirty();
        }
    }
    endComponentCard();

    // Physics world parameters - scene-global beside the Environment, read by
    // PhysicsSystem each fixed step.
    if (beginComponentCard("Physics", EditorStyle::Accent::Physics, true)) {
        bool changed = false;
        PhysicsSettings& phys = ec.frame.scene.physics();
        const PhysicsSettings before = phys;

        changed |= propDrag3("Gravity", glm::value_ptr(phys.gravity), 0.05f, -50.0f, 50.0f, "%.2f");
        changed |= propDragInt("Solver Iterations", &phys.solverIterations, 0.1f, 1, 32);

        if (changed) {
            state.commands.push(std::make_unique<PhysicsSettingsEditCommand>(before, phys, "Edit Physics"));
            state.markSceneDirty();
        }
    }
    endComponentCard();
}

void InspectorPanel::drawReflectionProbeSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<ReflectionProbe>(scene, resources, state, id, [&](ReflectionProbe& probe) {
        bool changed = false;

        // Box half-extents: the influence + parallax-correction box. Should
        // roughly match the surrounding walls of the region the probe represents.
        changed |= propDrag3("Box Size", glm::value_ptr(probe.halfExtents), 0.1f, 0.1f, 1000.0f, "%.1f");
        changed |= propSlider("Falloff", &probe.falloff, 0.0f, 1.0f, "%.2f");
        changed |= propDrag("Intensity", &probe.intensity, 0.02f, 0.0f, 8.0f, "%.2f");

        // Sharper reflections cost more VRAM + bake time, and the probe cubes
        // share one GPU array, so the highest resolution drives them all.
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

void InspectorPanel::drawDecalSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<Decal>(scene, resources, state, id, [&](Decal& decal) {
        bool changed = false;

        changed |= propCheckbox("Enabled", &decal.enabled,
                                "Off takes the projector out of the pass entirely");

        // The projected material: its albedo (with alpha) is what lands on the surface.
        changed |= pickAsset<MaterialAsset>("##DecalMatPick", "Material", resources, decal.material);
        changed |= propSlider("Angle Fade", &decal.angleFade, 0.0f, 1.0f, "%.2f",
                              "Fade where the surface turns away from the projector (projects along -Z; the Transform's scale is the box)");
        changed |= propSlider("Opacity", &decal.opacity, 0.0f, 1.0f, "%.2f");

        // Harsher than the Mesh card's "No material assigned": a mesh with no
        // material still draws with the shader's defaults, while the decal pass
        // skips a null-material decal outright.
        if (!decal.material) {
            ImGui::TextColored(EditorStyle::DANGER, "No material: this projects nothing");
        }

        return changed;
    });
}

void InspectorPanel::drawParticleSection(EditorContext& ec, EntityId id) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<ParticleEmitter>(scene, resources, ec.state, id, [&](ParticleEmitter& e) {
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
        // Particles run off the sim delta, so in Edit mode Live is structurally
        // 0 however well the emitter is set up, and the count would otherwise
        // read as a verdict on the emitter.
        if (ec.frame.clock.getSimDelta() <= 0.0f) {
            ImGui::TextDisabled("The world is not running - press Play to see them.");
        }

        return changed;
    });
}

void InspectorPanel::drawAudioSourceSection(EditorContext& ec, EntityId id) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<AudioSource>(scene, resources, ec.state, id, [&](AudioSource& source) {
        bool changed = false;

        changed |= pickAsset<AudioClipAsset>("##SoundPick", "Clip", resources, source.clip);

        const AudioClipAsset* clip = (source.clip && resources.isAlive(source.clip))
            ? &resources.get(source.clip) : nullptr;
        if (clip) {
            ImGui::TextDisabled("%.2fs, %u channel%s, %u Hz, %.1f MB",
                                static_cast<double>(clip->duration()), clip->channels,
                                clip->channels == 1 ? "" : "s", clip->sampleRate,
                                static_cast<double>(clip->sampleCount() * sizeof(int16_t)) / (1024.0 * 1024.0));
        }

        ImGui::Spacing();
        changed |= propSlider("Volume", &source.volume, 0.0f, 2.0f, "%.2f");
        changed |= propDrag("Pitch", &source.pitch, 0.005f, 0.1f, 4.0f, "%.2fx",
                            "Playback rate; also shifts the pitch");
        changed |= propCheckbox("Loop", &source.loop);
        changed |= propCheckbox("Play On Start", &source.playOnStart,
                                "Starts by itself once the simulation runs. In the editor that "
                                "means on Play, never while a scene is only open");

        ImGui::Spacing();
        changed |= propCheckbox("Spatial", &source.spatial,
                                "Positioned in the world and attenuated by distance. Turn it off "
                                "for music, narration and UI sound");
        if (source.spatial) {
            changed |= propDrag("Min Distance", &source.minDistance, 0.05f, 0.0f, 1000.0f, "%.2f",
                                "Full volume inside this radius");
            changed |= propDrag("Max Distance", &source.maxDistance, 0.25f, 0.0f, 5000.0f, "%.1f",
                                "Silent at this radius; the falloff between the two is linear");
            if (source.maxDistance <= source.minDistance) {
                ImGui::TextColored(EditorStyle::WARNING,
                                   "Max Distance is not past Min - nothing is attenuated.");
            }
            // Counted rather than called stereo: this fires on any multi-channel
            // clip, and a six-channel file told it was stereo sends the author
            // looking for a second channel that is not the thing hurting them.
            if (clip && clip->channels > 1) {
                ImGui::TextColored(EditorStyle::WARNING,
                                   "This clip has %u channels - each sticks to one ear.",
                                   clip->channels);
            }
            // The same thing the listener card says, for the same reason: the
            // sound is heard at the world origin rather than where the author
            // meant, and nothing else on screen would explain why.
            if (!scene.has<Transform>(id)) {
                ImGui::TextColored(EditorStyle::WARNING,
                                   "A positioned sound needs a Transform to have a position.");
            }
            // The one genuinely invisible failure here: AudioSystem's own
            // warning waits for a spatial voice to start, so at edit time -
            // where the mistake is made - nothing says it at all.
            if (!findActiveListener(scene)) {
                ImGui::TextColored(EditorStyle::WARNING,
                                   "No active Audio Listener in the scene - this is silent.");
            }
        }

        // Outside the spatial block above: a muted mix is not a positioning
        // mistake. It silences a 2D source, a positioned one and the audition
        // alike, while the transport goes on showing a running cursor.
        if (ec.audioSystem.device().masterVolume() <= 0.0f) {
            ImGui::TextColored(EditorStyle::WARNING,
                               "Audio Listener volume is 0 - nothing is heard, audition included.");
        }

        ImGui::Spacing();
        // The Asset Browser's transport, drawn from one place: what an audition
        // plays and what it refuses to touch is stated on auditionTransport.
        AudioDevice& device = ec.audioSystem.device();
        const float  ih     = ImGui::GetFrameHeight();
        if (auditionTransport("inspSound", device, m_previewVoice, m_previewOwner == id, clip, ih))
            m_previewOwner = id;

        ImGui::SameLine(0, 8.0f);
        ImGui::AlignTextToFramePadding();
        // Named, because the buttons beside it are the audition's transport and
        // an unlabelled "Playing" would look like it belonged to them. Read off
        // the mixer, not off `playing`, which is the scene's word not the sound's.
        const VoiceId sourceVoice = ec.audioSystem.voiceOf(id);
        if (device.isVoiceActive(sourceVoice)) {
            ImGui::TextDisabled("Source: %s %.2fs",
                                device.isVoicePaused(sourceVoice) ? "held" : "playing",
                                static_cast<double>(device.voiceCursor(sourceVoice)));
        } else {
            ImGui::TextDisabled(source.playing ? "Source: playing" : "Source: idle");
        }

        // Full width and on its own line, which is where the animation cards
        // put theirs. Disabled rather than hidden with nothing playing, so the
        // card does not change height when an audition ends.
        auditionScrubber("SndTime", device, m_previewVoice,
                         clip != nullptr ? clip->duration() : 0.0f, -1.0f);

        return changed;
    });
}

void InspectorPanel::drawAudioListenerSection(EditorContext& ec, EntityId id) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<AudioListener>(scene, resources, ec.state, id, [&](AudioListener& listener) {
        bool changed = false;

        changed |= propCheckbox("Active", &listener.active,
                                "Exactly one listener is heard from; the first active one wins");
        changed |= propSlider("Volume", &listener.volume, 0.0f, 1.0f, "%.2f",
                              "Master gain for everything this listener hears");

        // Naming the winner, not counting the candidates. Gated on this listener
        // having a pose: findActiveListener joins on Transform, so one without it
        // lost for the reason the warning below gives, not to storage order.
        const EntityId heard = findActiveListener(scene);
        if (listener.active && heard && heard != id && scene.has<Transform>(id)) {
            char winner[64] = {};
            getEntityDisplayName(scene, heard, winner, sizeof(winner));
            // Wrapped where its neighbours are not: this is the one warning here
            // carrying a name a user typed, and unwrapped it leaves the panel.
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextWrapped("Not the ear: '%s' is heard from, being first in storage order.",
                               winner);
            ImGui::PopStyleColor();
        }
        if (listener.active && !scene.has<Transform>(id)) {
            ImGui::TextColored(EditorStyle::WARNING,
                               "A listener needs a Transform to have a position.");
        }

        return changed;
    });
}

void InspectorPanel::drawIrradianceVolumeSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<IrradianceVolume>(scene, resources, state, id, [&](IrradianceVolume& v) {
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

void InspectorPanel::drawRigidbodySection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<Rigidbody>(scene, resources, state, id, [&](Rigidbody& rb) {
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

        // The layer fields the simulation already filters by. Serialized and
        // load-bearing - a ragdoll build writes them - but they were only
        // reachable through a text editor until they had rows here.
        changed |= propDragInt("Layer", &rb.layer, 0.1f, 0, 1 << 30,
                               "Bit mask of the layers this body is on; a ragdoll puts its bones on 2");
        changed |= propDragInt("Collides With", &rb.collidesWith, 0.1f, INT_MIN, INT_MAX,
                               "Bit mask of layers this body collides with; -1 is everything");

        // Scoped to a dynamic body, the one this ruins: it integrates gravity
        // with nothing to land on and leaves the world, where a static or
        // kinematic body with no shape is merely inert.
        if (!rb.isStatic && !rb.isKinematic && !scene.has<Collider>(id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No Collider: it falls through everything.");
        }

        if (changed) {
            // PhysicsSystem zeroes a sleeping body's velocity, so wake it - on
            // the live component, before the card pushes the undo command, so
            // the wake is in that command's "after" value.
            rb.sleeping = false;
            rb.sleepTimer = 0.0f;
        }
        return changed;
    });
}

void InspectorPanel::drawColliderSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<Collider>(scene, resources, state, id, [&](Collider& col) {
        bool changed = false;

        // A collider is a set of parts. A single part is editable here; a
        // mesh-fitted compound shows its part count (rebuild it via Fit to Mesh).
        changed |= propCheckbox("Enabled", &col.enabled,
                                "Disabled colliders are inert: no broadphase entry, no contacts");

        if (col.parts.size() == 1) {
            ColliderPart& part = col.parts[0];
            changed |= propEnumCombo("Shape", part.shape);
            changed |= drawVec3Control("Center", glm::value_ptr(part.center), 0.0f, 0.05f);
            switch (part.shape) {
                case ColliderShape::Capsule:
                    // The segment runs along local +Y, so the capsule stands
                    // 2*(halfHeight + radius) tall - spelled out because that is
                    // the number an author matches to a model.
                    changed |= propDrag("Radius", &part.radius, 0.01f, 0.001f,
                                        1000.0f, "%.3f");
                    changed |= propDrag("Half Height", &part.halfHeight, 0.01f,
                                        0.0f, 1000.0f, "%.3f",
                                        "Half the segment, caps excluded. 0 is a sphere.");
                    ImGui::TextDisabled("Height %.3f along local +Y",
                                        (part.halfHeight + part.radius) * 2.0f);
                    break;

                case ColliderShape::Mesh:
                    ImGui::TextDisabled("%u triangle(s), %zu tree node(s)",
                                        part.meshCount / 3, col.meshNodes.size());
                    // The one shape that cannot move, and the failure is
                    // silent: a triangle soup has no inside to be pushed out of.
                    if (scene.has<Rigidbody>(id)) {
                        const Rigidbody& rb = scene.get<Rigidbody>(id);
                        if (!rb.isStatic && !rb.isKinematic) {
                            ImGui::TextColored(EditorStyle::DANGER,
                                "A mesh encloses no volume: this body falls through the world.");
                        }
                    }
                    break;

                case ColliderShape::Box:
                    changed |= drawVec3Control("Half Extents",
                        glm::value_ptr(part.halfExtents), 0.5f, 0.05f);
                    break;

                case ColliderShape::Count:
                    break;
            }
        } else {
            ImGui::TextDisabled("%zu parts (mesh-fitted)", col.parts.size());
        }

        // Detail 1 is the scaled bounds, higher a box compound hugging the mesh,
        // with entity scale baked in because the solver ignores it. Looked for
        // downward: an import leaves the geometry below the entity physics is on.
        const EntityId meshNode =
            HierarchyOperations::findInSelfOrDescendants<Mesh>(scene, id);
        if (meshNode && scene.get<Mesh>(meshNode).mesh) {
            const auto& asset = resources.get(scene.get<Mesh>(meshNode).mesh);
            if (meshNode != id && scene.has<Name>(meshNode)) {
                ImGui::Spacing();
                ImGui::TextDisabled("Shape from '%s'", scene.get<Name>(meshNode).value);
            }
            if (asset.bounds().valid()) {
                ImGui::Spacing();
                propSliderInt("Detail", &m_colliderFitDetail, 1, COLLIDER_FIT_MAX_DETAIL,
                    "1 = one box; higher = a tighter box compound (more boxes = heavier)");
                if (ImGui::Button("Fit to Mesh", ImVec2(-1.0f, 0.0f))) {
                    const glm::vec3 scale = meshScaleRelativeTo(scene, id, meshNode);
                    col.parts = fitBoxesToMesh(asset, m_colliderFitDetail, scale);
                    col.meshPoints.clear();
                    col.meshNodes.clear();
                    changed = true;
                }

                // The other way to take a shape from the same mesh - the
                // geometry itself, for something that does not move. It replaces
                // the parts, which would otherwise collide twice.
                if (ImGui::Button("Make Mesh Collider", ImVec2(-1.0f, 0.0f))) {
                    // Built beside the collider and swapped in only if it came
                    // to something: a mesh with no whole triangle would leave an
                    // author with no collider at all.
                    const glm::vec3 scale = meshScaleRelativeTo(scene, id, meshNode);
                    Collider built;
                    built.parts.clear();
                    const bool empty = addMeshCollider(built, asset, scale) == 0;
                    m_meshColliderEmpty = empty ? id : EntityId{};
                    if (!empty) {
                        built.isTrigger = col.isTrigger;
                        built.enabled   = col.enabled;
                        col = std::move(built);
                        changed = true;
                    }
                }
                if (m_meshColliderEmpty == id) {
                    ImGui::TextColored(EditorStyle::DANGER,
                        "That mesh has no whole triangle; the collider is unchanged.");
                }
            }
        }

        changed |= propCheckbox("Trigger", &col.isTrigger);

        // PhysicsSystem walks the Rigidbody storage and reads a Collider only
        // off what it finds there, so a lone collider is in no broadphase: it
        // stops nothing and, Trigger ticked or not, fires nothing.
        if (!scene.has<Rigidbody>(id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No Rigidbody: nothing collides with this.");
        }

        return changed;
    });
}

void InspectorPanel::drawJointSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<Joint>(scene, resources, state, id, [&](Joint& joint) {
        bool changed = false;

        changed |= propEnumCombo("Type", joint.type);

        // Picked from the scene rather than typed, and rebuilt each frame
        // because the list is the scene. Every entity, not only the named ones:
        // an unnamed target reads as "None" and is overwritten unseen.
        m_jointCandidates.clear();
        m_jointCandidateLabels.clear();
        m_jointCandidateNames.clear();
        m_jointCandidates.push_back(EntityId{});
        m_jointCandidateLabels.emplace_back("None");
        scene.forEachEntity([&](EntityId other) {
            if (other == id) return;
            char label[96];
            getEntityDisplayName(scene, other, label, sizeof(label));
            m_jointCandidates.push_back(other);
            m_jointCandidateLabels.emplace_back(label);
        });
        // Pointers taken only once the labels have stopped moving.
        m_jointCandidateNames.reserve(m_jointCandidateLabels.size());
        for (const std::string& label : m_jointCandidateLabels) {
            m_jointCandidateNames.push_back(label.c_str());
        }

        int selected = 0;
        for (size_t i = 0; i < m_jointCandidates.size(); ++i) {
            if (m_jointCandidates[i] == joint.connected) {
                selected = static_cast<int>(i);
                break;
            }
        }
        if (propIndexCombo("Connected", m_jointCandidateNames.data(),
                           static_cast<int>(m_jointCandidateNames.size()),
                           &selected)) {
            joint.connected = m_jointCandidates[static_cast<size_t>(selected)];
            changed = true;
        }

        changed |= drawVec3Control("Anchor", glm::value_ptr(joint.anchor),
                                   0.0f, 0.05f);
        changed |= drawVec3Control("Connected Anchor",
                                   glm::value_ptr(joint.connectedAnchor), 0.0f, 0.05f);

        if (joint.type == JointType::Distance) {
            changed |= propDrag("Distance", &joint.distance, 0.01f, -1.0f, 1000.0f,
                                "%.3f m",
                                "Negative takes whatever the two were apart on the\n"
                                "first tick, so a rope built at play time needs no\n"
                                "one to measure it.");
            // The solver owns the measurement, so it is shown and not edited.
            if (joint.distance < 0.0f && joint.resolvedDistance >= 0.0f) {
                ImGui::TextDisabled("Holding %.3f m, measured on the first tick.",
                                    joint.resolvedDistance);
            }
        }

        changed |= propSlider("Stiffness", &joint.stiffness, 0.0f, 1.0f, "%.2f",
                              "Fraction of the remaining gap closed per tick.\n1 pulls the anchors together at once; lower drifts back slowly.");
        changed |= propCheckbox("Collide Connected", &joint.collideConnected,
                                "Off by default: jointed bodies usually overlap at\n"
                                "the joint, and resolving both the contact and the\n"
                                "joint makes the pair fight and gain energy.");

        // The two ways a joint silently does nothing.
        if (!joint.connected) {
            ImGui::TextColored(EditorStyle::DANGER, "No connected body: this holds nothing.");
        }
        if (!scene.has<Rigidbody>(id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No Rigidbody: this entity is not simulated.");
        }

        return changed;
    });
}

void InspectorPanel::drawRagdollSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    // What the buttons asked for, run after the card rather than inside it: the
    // card holds a reference to the component across the whole draw, and both
    // operations remove it.
    enum class Pending { None, Build, Clear };
    Pending pending = Pending::None;
    EntityId rigNode{};

    editComponentCard<Ragdoll>(scene, resources, state, id, [&](Ragdoll& ragdoll) {
        bool changed = false;

        changed |= propCheckbox("Active", &ragdoll.active,
                                "On, physics poses the rig and the clip is ignored.\n"
                                "Off, the bodies follow the animation and do not fall.");

        ImGui::TextDisabled("%zu simulated bone(s)", ragdoll.bones.size());

        // Offered before the rig is looked for, because clearing does not need
        // one: the bones are ordinary entities, and a ragdoll whose skeleton went
        // away is the case that most wants the button.
        if (ImGui::Button("Clear", ImVec2(-1.0f, 0.0f))) pending = Pending::Clear;

        // Looked for downward: an import puts the Animator below the entity the
        // physics is authored on.
        rigNode = HierarchyOperations::findInSelfOrDescendants<Animator>(scene, id);
        const bool hasRig = rigNode
                         && scene.get<Animator>(rigNode).skeleton
                         && resources.isAlive(scene.get<Animator>(rigNode).skeleton);
        if (!hasRig) {
            ImGui::TextColored(EditorStyle::DANGER,
                "No Animator with a skeleton here or below: nothing to build from.");
            return changed;
        }
        if (rigNode != id && scene.has<Name>(rigNode)) {
            ImGui::TextDisabled("Rig from '%s'", scene.get<Name>(rigNode).value);
        }

        ImGui::Spacing();
        propDrag("Thickness", &m_ragdollSettings.thickness, 0.01f, 0.02f, 1.0f, "%.2f",
                 "Limb radius as a fraction of its length. A rig says nothing\n"
                 "about how solid it is, and length is what scales.");
        propDrag("Mass", &m_ragdollSettings.mass, 1.0f, 0.1f, 1000.0f, "%.0f kg",
                 "Shared out by limb volume, so a forearm does not weigh a torso.");

        if (ImGui::Button("Build", ImVec2(-1.0f, 0.0f))) pending = Pending::Build;

        ImGui::TextDisabled("Clear removes the ragdoll and its bones together.");

        return changed;
    });

    // Both rebuild the subtree, so both record what it looked like before and
    // what the operation left - a scene write plus markSceneDirty is the
    // mutation design.md 2.5 names as the one that breaks undo.
    if ((pending == Pending::Build && rigNode) || pending == Pending::Clear) {
        SubtreeSnapshot before = SubtreeSnapshot::capture(scene, id);

        if (pending == Pending::Build) {
            const SkeletonAsset& rig = resources.get(scene.get<Animator>(rigNode).skeleton);
            buildRagdoll(scene, id, rig, m_ragdollSettings);
        } else {
            clearRagdoll(scene, id);
        }

        SubtreeSnapshot after = SubtreeSnapshot::capture(scene, id);
        state.commands.push(std::make_unique<SubtreeReplaceCommand>(
            std::move(before), std::move(after),
            pending == Pending::Build ? "Build ragdoll" : "Clear ragdoll"));
    }
}

void InspectorPanel::drawCameraSection(EditorContext& ec, EntityId id) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState&     state     = ec.state;

    editComponentCard<Camera>(scene, resources, state, id, [&](Camera& cam) {
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

        // Each bounded by the other, and kept apart rather than merely ordered:
        // equal planes divide by zero in the projection, and the cluster pass
        // takes log(zFar / zNear).
        changed |= propDrag("Near Clip", &cam.zNear, 0.01f, 0.001f,
                            cam.zFar - CLIP_PLANE_SEPARATION, "%.3f");
        changed |= propDrag("Far Clip", &cam.zFar, 1.0f,
                            cam.zNear + CLIP_PLANE_SEPARATION, 100000.0f, "%.0f");

        // Depth of field: amount 0 disables the blur pass entirely.
        changed |= propDrag("Focus Distance", &cam.focusDistance, 0.1f, 0.01f, 10000.0f, "%.2f");
        changed |= propSlider("DoF Amount", &cam.dofAmount, 0.0f, 1.0f, "%.2f");
        changed |= propCheckbox("Active", &cam.active);

        // From the camera controller, not from storage order: it and the
        // visibility pass each hold the camera they resolved while it stays
        // active, so "the first one wins" would hand the author a false reason.
        const EntityId eye = ec.cameraController.getCameraEntity();
        if (cam.active && eye && eye != id && scene.has<Camera>(eye)) {
            char rendered[64] = {};
            getEntityDisplayName(scene, eye, rendered, sizeof(rendered));
            // Wrapped for the same reason the listener's is: it carries a name
            // the author typed, and unwrapped that name leaves the panel.
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextWrapped("Not the eye: '%s' is rendered from.", rendered);
            ImGui::PopStyleColor();
        }

        if (ImGui::Button("Set as Main Camera", ImVec2(-1, 0))) {
            EditorActions::setActiveCamera(scene, state, id);
        }

        return changed;
    });
}

void InspectorPanel::drawLODSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<LOD>(scene, resources, state, id, [&](LOD& lod) {
        bool changed = false;

        changed |= propSlider("Bias", &lod.bias, 0.1f, 4.0f,
            "Scales every level's range; above 1 keeps detail further out");

        for (size_t i = 0; i < lod.levels.size(); ++i) {
            const LODLevel& level = lod.levels[i];
            const char* name = (level.mesh && resources.isAlive(level.mesh))
                ? resources.get(level.mesh).name().c_str() : "<unresolved>";
            const size_t tris = (level.mesh && resources.isAlive(level.mesh))
                ? resources.get(level.mesh).indices.size() / 3 : 0;
            ImGui::TextDisabled("%zu: %s  (%zu tris, to %.0fm)", i, name, tris, level.maxDistance);
        }

        ImGui::Spacing();

        // Generation decimates the Mesh component's geometry. Re-tessellating is
        // better where the source is procedural, but an imported mesh only has
        // its triangles to work with.
        if (scene.has<Mesh>(id) && scene.get<Mesh>(id).mesh) {
            propSliderInt("Levels", &m_lodGenLevels, 1, 4,
                "How many coarser levels to build below the source mesh");
            if (ImGui::Button("Generate Levels", ImVec2(-1.0f, 0.0f))) {
                lod = generateLOD(resources, scene.get<Mesh>(id).mesh,
                                  static_cast<uint32_t>(m_lodGenLevels));
                changed = true;
            }
        } else {
            ImGui::TextDisabled("Add a Mesh to generate levels from.");
        }

        return changed;
    });
}

void InspectorPanel::drawAnimationSection(EditorContext& ec, EntityId id) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<Animation>(scene, resources, ec.state, id, [&](Animation& anim) {
        // Only authoring edits (length, keyframes, Play On Start) set `changed`,
        // so play / pause / stop / scrub stay non-undoable. The snapshot does
        // hold time and playing, so an undo also restores the scrub position.
        const float GAP = EditorStyle::px(8.0f);
        float ih = ImGui::GetFrameHeight();
        if (iconButton("inspPlay", anim.playing ? EditorIcon::Pause : EditorIcon::Play,
                       anim.playing, true, anim.playing ? "Pause" : "Play", ih))
            anim.playing = !anim.playing;
        ImGui::SameLine(0, GAP);
        if (iconButton("inspStop", EditorIcon::Stop, false, true, "Stop (rewind)", ih)) {
            anim.playing = false;
            anim.time = 0.0f;
        }
        // Loop and Speed round-trip with the scene, so they push an edit. Play /
        // Stop / the scrubber do not: dirtying the scene every time someone
        // previews a clip would make the unsaved-changes prompt meaningless.
        bool changed = false;

        ImGui::SameLine(0, GAP);
        if (iconButton("inspLoop", EditorIcon::Loop, anim.looping, true,
                       anim.looping ? "Looping" : "Play once", ih)) {
            anim.looping = !anim.looping;
            changed = true;
        }
        ImGui::SameLine(0, GAP);
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::DragFloat("##ASpeed", &anim.speed, 0.005f, 0.0f, 10.0f, "Speed %.2fx", PROP_CLAMP);

        // Explicit minimum length holds the clip open past the last keyframe
        // (0 = auto, derived from the keyframes).
        if (propDrag("Length", &anim.length, 0.02f, 0.0f, 100000.0f, "%.2f s  (0 = auto)")) {
            anim.length = std::max(0.0f, anim.length);  // same clamp as the Bottom panel
            changed = true;
        }

        // The authored half, worded as the Audio Source card words its twin.
        // The transport above previews; this is what a shipped scene does.
        changed |= propCheckbox("Play On Start", &anim.playOnStart,
                                "Starts by itself once the simulation runs. In the editor that "
                                "means on Play, never while a scene is only open");

        // Play sets a flag AnimationSystem acts on, and that system returns on
        // a zero sim delta - so in Edit mode the button reads Pause while the
        // playhead stays put. The Bottom panel's transport says it too.
        if (anim.playing && ec.frame.clock.getSimDelta() <= 0.0f) {
            ImGui::TextDisabled("Held at %.2fs - it advances while the world runs.", anim.time);
        }

        const float duration = Animation::computeDuration(anim);
        if (duration > 0.0f) {
            ImGui::SetNextItemWidth(-1);
            char timeFmt[32];
            snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", duration);
            ImGui::SliderFloat("##ATime", &anim.time, 0.0f, duration, timeFmt, PROP_CLAMP);
        }

        // Read-only digest. The editable keyframe editor lives in the Bottom
        // panel's track editor; duplicating it here drifted out of sync.
        ImGui::Spacing();
        ImGui::TextUnformatted("Keyframes");
        auto trackSummary = [](const char* label, size_t count, float dur) {
            ImGui::BulletText("%s: %zu key%s, %.2fs", label, count, count == 1 ? "" : "s", dur);
        };
        trackSummary("Position", anim.positionTrack.keyframeCount(), anim.positionTrack.getDuration());
        trackSummary("Rotation", anim.rotationTrack.keyframeCount(), anim.rotationTrack.getDuration());
        trackSummary("Scale",    anim.scaleTrack.keyframeCount(),    anim.scaleTrack.getDuration());
        ImGui::TextDisabled("Edit keyframes in Bottom > Animation.");

        return changed;
    });
}

void InspectorPanel::drawAnimatorSection(EditorContext& ec, EntityId id) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<Animator>(scene, resources, ec.state, id, [&](Animator& animator) {
        bool changed = false;

        changed |= pickAsset<SkeletonAsset>("##RigPick",  "Rig",  resources, animator.skeleton);
        changed |= pickAsset<AnimationClipAsset>("##ClipPick", "Clip", resources, animator.clip);

        const SkeletonAsset* rig = (animator.skeleton && resources.isAlive(animator.skeleton))
            ? &resources.get(animator.skeleton) : nullptr;
        const AnimationClipAsset* clip = (animator.clip && resources.isAlive(animator.clip))
            ? &resources.get(animator.clip) : nullptr;

        if (rig) ImGui::TextDisabled("%zu bones", rig->bones.size());
        // A clip is cooked against one rig's bone order, so one cooked against
        // another poses the wrong joints out of matching indices. The system
        // refuses it and holds the bind pose; said where the pairing is made.
        if (rig && clip && clip->skeleton != rig->name()) {
            ImGui::TextColored(EditorStyle::DANGER, "Clip belongs to rig '%s'",
                               clip->skeleton.c_str());
            ImGui::TextDisabled("The bind pose is held until they match.");
        }

        // Mirrors the Animation card: Loop, Speed and Play On Start round-trip
        // with the scene so they push an edit, while play / stop / the scrubber
        // do not. Scrubbing works paused - the pose system composes every frame.
        const float GAP = EditorStyle::px(8.0f);
        const float ih = ImGui::GetFrameHeight();
        if (iconButton("inspRigPlay", animator.playing ? EditorIcon::Pause : EditorIcon::Play,
                       animator.playing, true, animator.playing ? "Pause" : "Play", ih))
            animator.playing = !animator.playing;
        ImGui::SameLine(0, GAP);
        if (iconButton("inspRigStop", EditorIcon::Stop, false, true, "Stop (rewind)", ih)) {
            animator.playing = false;
            animator.time = 0.0f;
        }
        ImGui::SameLine(0, GAP);
        if (iconButton("inspRigLoop", EditorIcon::Loop, animator.looping, true,
                       animator.looping ? "Looping" : "Play once", ih)) {
            animator.looping = !animator.looping;
            changed = true;
        }
        ImGui::SameLine(0, GAP);
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::DragFloat("##RigSpeed", &animator.speed, 0.005f, -10.0f, 10.0f,
                                    "Speed %.2fx", PROP_CLAMP);

        // The authored half, worded as the Animation and Audio Source cards word
        // their twin. The transport above previews; this is what a shipped scene
        // does.
        changed |= propCheckbox("Play On Start", &animator.playOnStart,
                                "Starts by itself once the simulation runs. In the editor that "
                                "means on Play, never while a scene is only open");

        // The same thing the Animation card says: play sets a flag the pose
        // evaluator acts on, and that evaluator returns on a zero sim delta, so
        // in Edit mode the head stays where it was.
        if (animator.playing && ec.frame.clock.getSimDelta() <= 0.0f) {
            ImGui::TextDisabled("Held at %.2fs - it advances while the world runs.",
                                static_cast<double>(animator.time));
        }

        if (clip && clip->duration > 0.0f) {
            ImGui::SetNextItemWidth(-1);
            char timeFmt[32];
            snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", clip->duration);
            ImGui::SliderFloat("##RigTime", &animator.time, 0.0f, clip->duration, timeFmt, PROP_CLAMP);
        } else if (animator.skeleton) {
            ImGui::TextDisabled("No clip: holding the bind pose.");
        }

        // Read-only, like the Animation card's keyframe digest: a marker belongs
        // to the clip and is authored in its recipe. What the card owes is a
        // look at what the clip just picked will announce, and when.
        if (clip && !clip->markers.empty()) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Markers");
            for (const ClipMarker& marker : clip->markers) {
                ImGui::BulletText("%s at %.2fs", marker.name.c_str(),
                                  static_cast<double>(marker.time));
            }
            ImGui::TextDisabled("Fired as AnimationEvent; edit them in the clip's recipe.");
        }

        // Blend state is deliberately absent, here and in the scene file: a
        // crossfade is started from code through Animator::crossFadeTo.
        return changed;
    });
}

void InspectorPanel::drawBoneSocketSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<BoneSocket>(scene, resources, state, id, [&](BoneSocket& socket) {
        bool changed = false;

        // The rig is the parent and only the parent: BoneSocketSystem writes
        // this entity's local Transform and lets the hierarchy resolve it against
        // the parent's world matrix, so one hung deeper lands plausibly wrong.
        const EntityId rig = scene.has<Hierarchy>(id) ? scene.get<Hierarchy>(id).parent : EntityId{};
        const Animator* animator = (rig && scene.has<Animator>(rig))
            ? &scene.get<Animator>(rig) : nullptr;
        const SkeletonAsset* skeleton =
            (animator && animator->skeleton && resources.isAlive(animator->skeleton))
                ? &resources.get(animator->skeleton) : nullptr;

        if (!animator) {
            ImGui::TextColored(EditorStyle::DANGER, "The parent is not a rig");
            ImGui::TextDisabled("A socket hangs off the entity carrying the Animator. "
                                "Drag this entity onto it in the Hierarchy.");
        } else if (!skeleton) {
            ImGui::TextColored(EditorStyle::DANGER, "The rig above names no skeleton");
            ImGui::TextDisabled("Pick one on its Animator card.");
        }

        changed |= pickBone("##BonePick", skeleton, socket.bone);

        // A name the rig does not carry is the one failure that looks like
        // nothing at all: the socket simply stays where it was put.
        if (skeleton && !socket.bone.empty() && skeleton->indexOf(socket.bone) < 0) {
            ImGui::TextColored(EditorStyle::DANGER, "Rig '%s' has no bone '%s'",
                               skeleton->name().c_str(), socket.bone.c_str());
            ImGui::TextDisabled("The socket stays where it is until they match.");
        } else if (skeleton) {
            ImGui::TextDisabled("%zu bones in rig '%s'",
                                skeleton->bones.size(), skeleton->name().c_str());
        }

        // The offset is authored here rather than on the Transform card because
        // the Transform is the socket's output: it is rewritten from the bone
        // every frame, including while the editor is paused.
        ImGui::Spacing();
        sectionLabel("Offset");
        changed |= drawVec3Control("Position", glm::value_ptr(socket.offset.position), 0.0f, 0.01f);

        m_socketEulerCache.sync(id, socket.offset.rotation);
        if (drawVec3Control("Rotation", m_socketEulerCache.degrees(), 0.0f, 0.5f)) {
            socket.offset.rotation = m_socketEulerCache.toQuat();
            changed = true;
        }

        changed |= drawVec3Control("Scale", glm::value_ptr(socket.offset.scale), 1.0f, 0.01f);

        return changed;
    });
}

void InspectorPanel::drawCharacterControllerSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<CharacterController>(scene, resources, state, id, [&](CharacterController& cc) {
        bool changed = false;

        changed |= propDrag("Jump Speed", &cc.jumpSpeed, 0.1f, 0.0f, 100.0f, "%.2f m/s");
        changed |= propDrag("Acceleration", &cc.acceleration, 0.5f, 0.0f, 1000.0f, "%.1f m/s2",
                            "How fast velocity closes on the requested direction");
        changed |= propSlider("Air Control", &cc.airControl, 0.0f, 1.0f, "%.2f",
                              "Fraction of that acceleration available while airborne");
        changed |= propDrag("Max Slope", &cc.maxSlopeAngle, 0.5f, 0.0f, 90.0f, "%.0f deg",
                            "Steeper than this holds nothing up: the character slides.\n"
                            "The same angle decides what counts as a wall to run\n"
                            "along rather than walk into, and how tall a step the\n"
                            "capsule rolls over.");

        changed |= propDrag("Step Height", &cc.stepHeight, 0.01f, 0.0f, 10.0f, "%.2f m",
                            "Tallest thing the character mounts instead of stopping at.\n"
                            "Checked against real geometry before anything moves: there\n"
                            "has to be clear space above it and walkable ground beyond,\n"
                            "so raising this makes the character climb more, never\n"
                            "climb through. Zero switches it off and a kerb is a wall.");

        // What the capsule rolls over on its own, which is not the same number
        // and is why a character was climbing kerbs before step-up existed: an
        // edge lower than this gives a walkable contact normal by itself.
        if (const float radius = capsuleRadiusOf(scene, id); radius > 0.0f) {
            const float limit = glm::radians(glm::clamp(cc.maxSlopeAngle, 0.0f, 90.0f));
            ImGui::Spacing();
            ImGui::TextDisabled("Rolls over steps up to %.2f m unaided",
                                radius * (1.0f - std::cos(limit)));
        }

        // Live state, not authoring: moveInput is written by gameplay and the
        // rest by the system. Shown because "why is it not jumping" is answered
        // by exactly these three numbers.
        ImGui::Spacing();
        ImGui::TextDisabled(cc.grounded ? "Grounded" : "Airborne");
        if (cc.grounded) {
            ImGui::TextDisabled("Ground %.0f deg from flat",
                glm::degrees(std::acos(glm::clamp(cc.groundNormal.y, -1.0f, 1.0f))));
        }
        ImGui::TextDisabled("Move input %.2f, %.2f, %.2f",
                            cc.moveInput.x, cc.moveInput.y, cc.moveInput.z);

        // The two ways a controller silently does nothing. Both are also named
        // once in the log by the system, but the fix is made here.
        if (!scene.has<Rigidbody>(id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No Rigidbody: nothing to drive.");
        } else if (!scene.get<Rigidbody>(id).freezeRotation) {
            ImGui::TextColored(EditorStyle::DANGER, "Rigidbody rotation is not frozen:");
            ImGui::TextDisabled("contacts will topple the character.");
        }
        if (!scene.has<Collider>(id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No Collider: it will never be grounded.");
        }

        return changed;
    });
}

void InspectorPanel::drawScriptSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    EditorState& state = ec.state;

    // The field widgets write into the behavior itself, and a behavior list is
    // move-only, so there is no pair of values for the copying command path to
    // hold. The serialized component is that pair, read before anything moves it.
    const std::string scriptBefore = ScriptEditCommand::capture(scene, id);
    const auto pushScriptEdit = [&](const char* label) {
        std::string after = ScriptEditCommand::capture(scene, id);
        if (after == scriptBefore) return;
        state.commands.push(std::make_unique<ScriptEditCommand>(
            id, scriptBefore, std::move(after), label));
        state.markSceneDirty();
    };

    bool remove = false;
    const bool open = beginComponentCard(CardInfo<ScriptComponent>::TITLE,
                                         CardInfo<ScriptComponent>::accentColor(), true, &remove);
    if (open) {
        auto& sc = scene.get<ScriptComponent>(id);

        // ScriptComponent serializes as one value holding every behavior, so an
        // instance has nowhere to store a field-level override - and the widgets
        // below write straight into the live behavior. Said once, not per key.
        if (PrefabOverrides::instanceRoot(scene, id)) {
            ImGui::TextWrapped("Script values on an instance belong to the prefab. Save as "
                               "Prefab keeps what you change here; saving the scene does not.");
        }

        int removeIndex = -1;
        for (size_t i = 0; i < sc.behaviors.size(); ++i) {
            Behavior* behavior = sc.behaviors[i].get();
            if (!behavior) continue;
            ImGui::PushID(static_cast<int>(i));

            // Behavior header row: the remove affordance is right-pinned like
            // a card's own x.
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
            if (inspector.changed()) pushScriptEdit("Edit Behavior");

            ImGui::PopID();
            if (i + 1 < sc.behaviors.size()) ImGui::Separator();
        }

        if (sc.behaviors.empty() && sc.unknown.empty())
            ImGui::TextDisabled("No behaviors attached.");

        int removeUnknown = -1;
        // A scope of its own rather than an offset into the one above: the
        // constructed rows push ids from 0 and so would these, and a number
        // picked to sit clear of them is only clear until one list grows.
        ImGui::PushID("held");
        for (size_t i = 0; i < sc.unknown.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (!sc.behaviors.empty() || i > 0) ImGui::Separator();

            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(EditorStyle::WARNING, "%s", sc.unknown[i].type.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - EditorStyle::px(14.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            const bool dropThis = ImGui::SmallButton("x##rmunknown");
            ImGui::PopStyleColor(2);
            if (dropThis) removeUnknown = static_cast<int>(i);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Discard this behavior and its values");

            ImGui::TextDisabled("No such behavior type is registered. Its values are "
                                "kept and saved; it does not run.");
            ImGui::PopID();
        }
        ImGui::PopID();
        if (removeUnknown >= 0) {
            sc.unknown.erase(sc.unknown.begin() + removeUnknown);
            pushScriptEdit("Discard Missing Behavior");
        }

        if (PrefabOverrides::instanceRoot(scene, id)) {
            ImGui::TextWrapped("Behavior fields are not per-instance overrides: "
                               "edit them in the prefab.");
        }

        ImGui::Spacing();
        if (ImGui::Button("+  Add Behavior", ImVec2(-1, 0))) ImGui::OpenPopup("##AddBehavior");
        if (ImGui::BeginPopup("##AddBehavior")) {
            const std::vector<std::string> names = BehaviorRegistry::get().names();
            if (names.empty()) {
                ImGui::TextDisabled("No behaviors registered.");
            } else {
                // Same type-to-narrow affordance as Add Component.
                static char s_behaviorFilter[48] = {};
                popupSearchField("##behaviorFilter", s_behaviorFilter,
                                 sizeof(s_behaviorFilter), EditorStyle::px(200.0f));
                for (const std::string& name : names) {
                    if (!matchesFilter(name.c_str(), s_behaviorFilter)) continue;
                    if (ImGui::MenuItem(name.c_str())) {
                        if (auto behavior = BehaviorRegistry::get().create(name)) {
                            sc.behaviors.push_back(std::move(behavior));
                            pushScriptEdit("Add Behavior");
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (removeIndex >= 0) {
            sc.behaviors.erase(sc.behaviors.begin() + removeIndex);
            pushScriptEdit("Remove Behavior");
        }
    }
    endComponentCard();
    if (remove) {
        scene.remove<ScriptComponent>(id);
        pushScriptEdit("Remove Script");
        PrefabOverrides::warnComponentIsPrefabs(scene, state, id, "Script",
                                                "comes back from the prefab on the next load");
    }
}

void InspectorPanel::drawHierarchySection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    EditorState& state = ec.state;

    const bool open = beginComponentCard(CardInfo<Hierarchy>::TITLE,
                                         CardInfo<Hierarchy>::accentColor(), false);
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
                    snprintf(cid, sizeof(cid), "%u", child.slot());
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

} // namespace Vkm::Engine
