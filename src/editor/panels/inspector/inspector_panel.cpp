#include "panels/inspector/inspector_panel.h"

#include <any>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <imgui.h>

#include "undo/component_edit.h"
#include "undo/composite_command.h"
#include "undo/component_commands.h"
#include "undo/prefab_overrides.h"
#include "core/clock.h"
#include "core/frame_context.h"
#include "ecs/component/scene/missing_assets.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/scene.h"
#include "actions/prefab_actions.h"
#include "actions/component_actions.h"
#include "editor_context.h"
#include "editor_state.h"
#include "io/scene/prefab.h"
#include "ecs/prefab_instances.h"
#include "io/scene/scene_serializer.h"
#include "net/replication/silence.h"
#include "net/wire/schema.h"
#include "ecs/component/light/light.h"
#include "resource/resource_manager.h"
#include "session/play_session.h"
#include "system/audio/audio_system.h"
#include "system/script/behavior_registry.h"
#include "core/frame/visibility.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"

namespace Vkm::Engine {

namespace {

// History entries keep their label's pointer, so each lives as long as the program.
template <typename T>
const char* editLabel() {
    static const std::string s_label = std::string("Edit ") + cardInfo(Tag<T>{}).title;
    return s_label.c_str();
}

template <typename T>
const char* addLabel() {
    static const std::string s_label = std::string("Add ") + cardInfo(Tag<T>{}).title;
    return s_label.c_str();
}

template <typename T>
const char* removeLabel() {
    static const std::string s_label = std::string("Remove ") + cardInfo(Tag<T>{}).title;
    return s_label.c_str();
}

// Ragdoll owns a subtree of bodies its own Clear removes; a header remove would strand them.
template <typename T>
constexpr bool CARD_OWNS_MORE_THAN_ITSELF = std::is_same_v<T, Ragdoll>;

// A card's whole-component edits, in its header's menu, each one undo step like a field's. Values move
// by reflected field, so an unreflected T offers none.
template <typename T>
void cardValueItems(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id) {
    if constexpr (Reflect::IS_REFLECTED<T>) {
        const T* copied = std::any_cast<T>(&state.componentClipboard);
        std::optional<T> source;
        if (ImGui::MenuItem("Reset")) source = T{};
        if (ImGui::MenuItem("Copy")) state.componentClipboard = scene.get<T>(id);
        if (ImGui::MenuItem("Paste Values", nullptr, false, copied != nullptr)) source = *copied;
        if (source) {
            // Every selected entity holding a T takes the source's authored fields whole,
            // not the active one's difference from it: one already at it would share nothing.
            const char* label = editLabel<T>();
            auto step = std::make_unique<CompositeCommand>(label);
            for (const EntityId target : state.selection.entities()) {
                if (!scene.isAlive(target) || !scene.has<T>(target)) continue;
                T& value    = scene.get<T>(target);
                const T was = value;
                value = withAuthored(value, *source);
                settleEdit(value, resources);
                if constexpr (std::is_same_v<T, Transform>) {
                    step->add(transformStep(scene, resources, target, was, value, label));
                } else {
                    step->add(editStep<T>(scene, resources, target, was, value, label));
                }
            }
            if (!step->empty()) state.pushStep(std::move(step));
        }
    }
}

/**
 * @brief Push the step for an edit the card's rows or code made to @p id's T.
 *
 * A Transform moves the rest of the selection by the axes moved, each scale into its Collider; a card
 * owning more than itself edits its own entity alone; any other reaches each selected entity holding a
 * T, as the fields its rows edited (@p shared).
 */
template <typename T>
void pushCardEdit(CardContext& card, const T& before, const T& after, uint64_t shared) {
    Scene& scene = card.scene;
    ResourceManager& resources = card.resources;
    EditorState& state = card.state;
    const char* label = editLabel<T>();
    if constexpr (std::is_same_v<T, Transform>) {
        // tryMerge collapses the drag into one undo step; a composite merges the same way.
        const std::vector<EntityId>& selection = state.selection.entities();
        state.pushStep(transformSelectionStep(scene, resources, selection, card.id, before, after, label));
    } else if constexpr (CARD_OWNS_MORE_THAN_ITSELF<T>) {
        pushEdit<T>(scene, resources, state, card.id, before, after, label);
    } else {
        const std::vector<EntityId>& selection = state.selection.entities();
        auto step = editSelectionStep<T>(scene, resources, selection, card.id, before, after, label, shared);
        state.pushStep(std::move(step));
    }
}

/**
 * @brief @p card's entity's T, when it has one: the header and its menu, then drawCard over its rows,
 *        then the step for what they edited.
 */
template <typename T>
void drawComponent(CardContext& card) {
    Scene& scene = card.scene;
    ResourceManager& resources = card.resources;
    EditorState& state = card.state;
    if (!scene.has<T>(card.id)) return;

    const CardInfo info = cardInfo(Tag<T>{});
    const bool removable = !CARD_OWNS_MORE_THAN_ITSELF<T> && !std::is_same_v<T, Transform>;
    const bool menu      = !CARD_OWNS_MORE_THAN_ITSELF<T>;
    bool remove = false;
    const bool open = beginComponentCard(info.title, info.icon, true, menu ? "##CardMenu" : nullptr);
    // Not for a card owning more than itself: a reset or a paste would strand or share its bodies.
    const char* key = PrefabOverrides::COMPONENT_KEY<T>;
    const std::vector<std::string> overridden = PrefabOverrides::overriddenFields(scene, card.id, key);
    if (menu && ImGui::BeginPopup("##CardMenu")) {
        cardValueItems<T>(scene, resources, state, card.id);
        if (!overridden.empty() && ImGui::MenuItem("Revert to Prefab")) {
            PrefabOverrides::revertComponent(scene, resources, state, card.id, key);
        }
        if (removable) {
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) remove = true;
        }
        ImGui::EndPopup();
    }
    if (open) {
        T& live = scene.get<T>(card.id);
        const T before = live;
        const AssetSlots slots{resources, state};
        Rows<T>          rows(live, overridden, &slots);
        drawCard(rows, card);
        if (rows.changed()) {
            // On the live component before the step is cut, so what it settles is in the after.
            settleEdit(live, resources);
            pushCardEdit<T>(card, before, live, rows.shared());
        }
        const std::optional<std::string>& revert = rows.revertAsked();
        if (revert) PrefabOverrides::revert(scene, resources, state, card.id, key, *revert);
    }
    endComponentCard();
    if (card.after) {
        const std::function<void()> after = std::move(card.after);
        card.after = nullptr;
        after();
    }
    if (remove) {
        T snap = scene.get<T>(card.id);
        scene.remove<T>(card.id);
        const char* label = removeLabel<T>();
        auto removal = std::make_unique<RemoveComponentCommand<T>>(card.id, std::move(snap), label);
        state.pushStep(std::move(removal));
        const char* fate = "comes back from the prefab on the next load";
        PrefabOverrides::warnComponentIsPrefabs(scene, state, card.id, info.title, fate);
    }
}

// What the other end sees of this entity, when it carries replicated state; empty when nothing.
std::string wireIdentity(const Scene& scene, EntityId id) {
    const NetSchema& schema = NetSchema::get();
    if (schema.size() == 0) return {};

    std::string carried;
    for (const NetType& type : schema.types()) {
        if (!type.has(scene, id)) continue;
        if (!carried.empty()) carried += ", ";
        carried += type.name;
    }
    if (carried.empty()) return {};

    // The slot is the wire name and survives save and load.
    const NetSilence silence = netSilence(scene, id);
    if (silence != NetSilence::None) {
        const NetSilenceText why = explain(silence);
        return std::string("Off the wire: ") + why.label + ".\n" + why.explanation;
    }
    return "Wire slot " + std::to_string(id.slot()) + " - " + carried;
}

// The name is the header's.
template <>
void drawComponent<Name>(CardContext&) {}

} // namespace

void InspectorPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (m_memory.auditionVoice != 0 && state.selection.active() != m_memory.auditionOwner) {
        ec.audioSystem.device().stopVoice(m_memory.auditionVoice);
        m_memory.auditionVoice = 0;
    }

    if (state.selection.entities().size() > 1) {
        const size_t picked = state.selection.entities().size();
        ImGui::TextDisabled("%zu selected - an edit reaches each that has the component", picked);
        ImGui::SetItemTooltip(
            "The cards are the active entity's. Lists, entity fields, the name, Ragdoll, Scripts and "
            "Remove act on it alone."
        );
    }

    Scene& scene = ctx.scene;
    const bool haveEntity = state.selection.active() && scene.isAlive(state.selection.active());
    const EntityId id = haveEntity ? state.selection.active() : EntityId{};
    // Another entity's cards start at their top, not at the scroll the last one's were left at.
    if (id != m_shown) ImGui::SetScrollY(0.0f);
    m_shown = id;
    // The game's eye: the frame's camera while the game has the viewport, else the one a session
    // would start on.
    const bool gameView = ec.input.gameHasViewport();
    CardContext card{
        scene,
        ctx.resources,
        state,
        id,
        ec.play.isPlaying(),
        gameView,
        ctx.clock.getSimDelta(),
        ec.audioSystem,
        ctx.particles,
        gameView && ctx.visibility ? ctx.visibility->cameraEntity : findActiveCamera(scene),
        m_memory,
        nullptr
    };

    // With nothing selected the World is what there is to edit.
    if (!haveEntity) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("World");
        ImGui::SameLine();
        ImGui::TextDisabled(state.selection.world() ? " Scene-global settings" : " - nothing selected");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        drawWorldCards(card);
        return;
    }

    drawIdentityHeader(scene, ctx.resources, state, id);

    // Never pruned: an undo can empty the field again; see docs/reference/io.md.
    const std::vector<MissingAssetRef> missing = SceneSerializer::unresolvedRefs(scene, ctx.resources, id);
    if (!missing.empty()) {
        ImGui::TextColored(EditorStyle::DANGER, "%zu asset reference(s) here did not load:", missing.size());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        for (const MissingAssetRef& ref : missing) {
            ImGui::TextWrapped("  %s.%s  '%s'", ref.component.c_str(), ref.field.c_str(), ref.name.c_str());
        }
        ImGui::TextWrapped("Kept as written: the save puts them back rather than emptying them.");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawPrefabSection(ec, id);

    // In the scene format's order; the name is the header's, the Script card last.
#define VKM_INSPECTOR_CARD(Type, Key) drawComponent<Type>(card);
    VKM_SCENE_VALUE_COMPONENTS(VKM_INSPECTOR_CARD, VKM_INSPECTOR_CARD, VKM_INSPECTOR_CARD)
#undef VKM_INSPECTOR_CARD
    if (scene.has<ScriptComponent>(id)) drawScriptCard(card);
}

void InspectorPanel::drawIdentityHeader(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id
) {
    // The entity's kind; its slot and what goes on the wire are a hover away, rarely needed.
    const float ih = ImGui::GetFrameHeight();
    inlineIcon(entityIconKind(scene, id), ih, ImGui::GetColorU32(EditorStyle::ACCENT));
    if (ImGui::IsItemHovered()) {
        const std::string wire = wireIdentity(scene, id);
        ImGui::SetTooltip("#%u%s%s", id.slot(), wire.empty() ? "" : "\n", wire.c_str());
    }
    ImGui::SameLine();

    // Add sits beside the name, so a long entity needs no scroll to take another component.
    const char* add   = "+ Add";
    const float addW  = ImGui::CalcTextSize(add).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float nameW = ImGui::GetContentRegionAvail().x - addW - ImGui::GetStyle().ItemSpacing.x;

    // Name is added only on user action, so looking never mutates the scene.
    if (Name* name = scene.tryGet<Name>(id)) {
        const char* key = PrefabOverrides::COMPONENT_KEY<Name>;
        const bool overridden = !PrefabOverrides::overriddenFields(scene, id, key).empty();
        const Name before = *name;
        ImGui::SetNextItemWidth(nameW);
        if (overridden) ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::ACCENT);
        // tryMerge coalesces the keystrokes into one undo step.
        const bool typed = ImGui::InputText("##Name", name->value, sizeof(name->value));
        if (overridden) ImGui::PopStyleColor();
        if (typed) pushEdit<Name>(scene, resources, state, id, before, *name, "Rename");
        if (overridden && ImGui::BeginPopupContextItem("##nameMenu")) {
            const bool revert = ImGui::MenuItem("Revert to Prefab");
            if (revert) PrefabOverrides::revertComponent(scene, resources, state, id, key);
            ImGui::EndPopup();
        }
    } else {
        char fallback[64];
        getEntityDisplayName(scene, id, fallback, sizeof(fallback));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(fallback);
        ImGui::SameLine();
        if (ImGui::SmallButton("+##addname")) {
            EditorActions::addComponent(scene, state, id, makeName(fallback), "Name", "Add Name");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a Name component to rename this entity");
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - addW);
    if (ImGui::Button(add, ImVec2(addW, 0.0f))) ImGui::OpenPopup("##AddComp");
    ImGui::SetItemTooltip("Add a component or a behavior");
    drawAddComponentMenu(scene, state, id);
}

namespace {

constexpr const char* ADD_SECTION_LABELS[] = {
    "Scene",
    "Net",
    "Render",
    "Light",
    "Effect",
    "Landscape",
    "Animation",
    "Audio",
    "Physics",
    "UI",
    "Behaviors"
};
static_assert(std::size(ADD_SECTION_LABELS) == static_cast<size_t>(AddSection::Count), "a label per heading");

/// What Add Component adds: the component's defaults.
template <typename T>
T addedComponent() {
    return T{};
}

/// A light added is a point lamp.
template <>
Light addedComponent<Light>() {
    return generateLight(LightType::Point);
}

/// A camera added does not take the game's view from the one it starts on.
template <>
Camera addedComponent<Camera>() {
    Camera camera;
    camera.active = false;
    return camera;
}

} // namespace

void InspectorPanel::drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id) {
    if (ImGui::BeginPopup("##AddComp")) {
        sectionLabel("Add Component");
        // Inside an instance a component lives only in the prefab; adding stays, as saving the
        // prefab back authors one (see PrefabOverrides::warnComponentIsPrefabs).
        if (Prefab::instanceRootOf(scene, id)) {
            ImGui::PushTextWrapPos(EditorStyle::px(260.0f));
            ImGui::TextDisabled(
                "Components on an instance belong to the prefab: Apply to Prefab keeps what is "
                "added here, saving the scene does not."
            );
            ImGui::PopTextWrapPos();
        }
        popupSearchField(
            "##compFilter",
            m_componentFilter,
            sizeof(m_componentFilter),
            EditorStyle::px(200.0f)
        );
        // Headings, not submenus, so filter matches are reachable; a heading waits for its first item.
        const char* pendingSection = nullptr;
        bool sectionDrawn = false;
        const auto section = [&](const char* label) { pendingSection = label; };
        const auto drawPendingSection = [&] {
            if (pendingSection == nullptr) return;
            if (sectionDrawn) ImGui::Spacing();
            sectionLabel(pendingSection);
            pendingSection = nullptr;
            sectionDrawn   = true;
        };

        // Every component with a line to say, under its heading. No Prefab (only saving one writes
        // it); Name is the + beside the entity title.
        const auto addItem = [&](auto tag, AddSection under) {
            using T = typename decltype(tag)::Type;
            const CardInfo info = cardInfo(tag);
            if (info.section != under || info.hint == nullptr || scene.has<T>(id)) return;
            if (!matchesFilter(info.title, m_componentFilter)) return;

            drawPendingSection();
            const bool picked = listRow(info.title, info.icon, info.title, false).clicked;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.hint);
            if (!picked) return;

            // The warning above is hidden by the open menu, so the add repeats it.
            EditorActions::addComponent(scene, state, id, addedComponent<T>(), info.title, addLabel<T>());
        };
        for (size_t s = 0; s < std::size(ADD_SECTION_LABELS); ++s) {
            const AddSection under = static_cast<AddSection>(s);
            section(ADD_SECTION_LABELS[s]);
#define VKM_ADD_COMPONENT_ITEM(Type, Key) addItem(Tag<Type>{}, under);
            VKM_SCENE_VALUE_COMPONENTS(VKM_ADD_COMPONENT_ITEM, VKM_ADD_COMPONENT_ITEM, VKM_ADD_COMPONENT_ITEM)
#undef VKM_ADD_COMPONENT_ITEM
        }
        for (const std::string& name : BehaviorRegistry::get().names()) {
            if (!matchesFilter(name.c_str(), m_componentFilter)) continue;
            drawPendingSection();
            if (listRow(name.c_str(), EditorIcon::Script, name.c_str(), false).clicked) {
                EditorActions::addBehavior(scene, state, id, name);
            }
        }
        ImGui::EndPopup();
    }
}

void InspectorPanel::drawPrefabSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    EditorState& state = ec.state;

    const EntityId root = Prefab::instanceRootOf(scene, id);
    if (!root) return;

    bool unpack     = false;
    const bool open = beginComponentCard("Prefab Instance", EditorIcon::Prefab, true);
    if (open) {
        const PrefabInstance& instance = scene.get<PrefabInstance>(root);

        drawPropertyLabel("Source");
        ImGui::TextWrapped("%s", instance.source.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", instance.source.c_str());

        // What an instance is for: its edits sent to every instance, taken back, or let go of.
        // Not during play: the pose and state are the session's, not the authored ones.
        const bool playing = ec.input.playing;
        ImGui::BeginDisabled(playing);
        if (ImGui::Button("Apply to Prefab")) {
            EditorActions::saveAsPrefab(scene, ec.frame.resources, state, root);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                playing ? "Stop the play session first"
                        : "Write this instance, overrides and all, over its prefab: every instance takes them"
            );
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(instance.overrides.empty());
        if (ImGui::Button("Revert All")) {
            PrefabOverrides::revertAll(scene, ec.frame.resources, state, root);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Drop every override: the instance as its prefab is");
        }
        ImGui::SameLine();
        // Done once the card is drawn: the card below still reads the instance.
        unpack = ImGui::Button("Unpack");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Keep these entities as they are, no longer an instance of the prefab");
        }
        ImGui::EndDisabled();

        // Below the root, point to it: the overrides and the file live there.
        if (root != id) {
            drawPropertyLabel("Root");
            char rootName[64];
            getEntityDisplayName(scene, root, rootName, sizeof(rootName));
            if (ImGui::SmallButton(rootName)) state.selectEntity(root);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the instance root");
        }

        const PrefabEntity* marker = scene.tryGet<PrefabEntity>(id);
        if (id == root && !marker) {
            // An expansion marks every entity it builds, so an unmarked root means the file did not
            // open. Checked before the scene-added-child branch.
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextWrapped(
                "The prefab file could not be opened, so this instance's "
                "entities are empty. Restore it and load the scene again - the "
                "reference, any overrides and where its entities stood are kept "
                "until then."
            );
            ImGui::PopStyleColor();
        } else if (!marker) {
            // Unstorable: the scene saves the instance as a reference, and the prefab lacks it.
            ImGui::TextWrapped(
                "Added to the scene, not to the prefab - this entity is "
                "dropped when the scene is loaded again."
            );
        } else {
            const uint32_t uid = marker->uid;
            size_t here = 0;
            for (const PrefabOverride& o : instance.overrides) {
                if (o.uid == uid) ++here;
            }
            if (instance.overrides.empty()) {
                ImGui::TextDisabled("No overrides. Editing a field here makes one.");
            } else {
                ImGui::TextDisabled(
                    "%zu override(s) here, %zu in the instance.",
                    here,
                    instance.overrides.size()
                );
            }
        }
    }
    endComponentCard();
    if (unpack) EditorActions::unpackPrefab(scene, state, root);
}

} // namespace Vkm::Engine
