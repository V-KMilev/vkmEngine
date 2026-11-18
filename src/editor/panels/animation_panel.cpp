#include "panels/animation_panel.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/clock.h"
#include "session/play_session.h"
#include "undo/component_edit.h"
#include "undo/composite_command.h"
#include "ecs/component/ui/ui_element.h"
#include "actions/component_actions.h"
#include "core/system.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/scene/transform.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "ui/style/editor_icons.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"
#include "ui/style/editor_style.h"
#include "system/animation/animation_system.h"
#include "ui/clip_transport.h"

namespace Vkm::Engine {

namespace {

// The Animation panel's timeline.
constexpr ImU32 TIMELINE_BG_U32    = IM_COL32( 18,  18,  20, 255);
constexpr ImU32 TIMELINE_TICK_U32  = IM_COL32( 90,  90,  95, 255);
constexpr ImU32 TIMELINE_LABEL_U32 = IM_COL32(150, 150, 155, 255);
constexpr ImU32 TIMELINE_LANE_U32  = IM_COL32( 45,  45,  50, 255);
constexpr ImU32 TIMELINE_GHOST_U32 = IM_COL32( 25,  25,  28, 200);

// Ten ruler divisions: labels do not collide at the panel's shipped width.
constexpr int RULER_TICKS = 10;

// One lane per track, in the order they are drawn and indexed by m_dotTrack.
struct LaneStyle {
    ImU32       color;
    const char* label;
};

const LaneStyle LANES[3] = {
    {EditorStyle::AXIS_X_U32, "P"},
    {EditorStyle::AXIS_Y_U32, "R"},
    {EditorStyle::AXIS_Z_U32, "S"},
};

} // namespace

void AnimationPanel::draw(EditorContext& ec) {
    Scene& scene = ec.frame.scene;
    const EntityId id = ec.state.selection.active();

    if (!scene.isAlive(id)) {
        ImGui::TextDisabled("Select an entity (viewport or Hierarchy) to animate it.");
        return;
    }

    char nameBuf[64];
    getEntityDisplayName(scene, id, nameBuf, sizeof(nameBuf));
    ImGui::Text("Target: %s  (#%u)", nameBuf, id.slot());

    Transform* transform = scene.tryGet<Transform>(id);
    if (!transform) {
        // A UI element has no Transform (its canvas lays it out), so one here would go unread.
        if (scene.has<UIElement>(id)) {
            ImGui::TextDisabled(
                "This is a UI element: its canvas places it, so there is no "
                "Transform to animate."
            );
            ImGui::TextDisabled("Move it from a behavior by writing UIElement::position.");
        } else {
            ImGui::TextDisabled(
                "Animation drives a Transform - add one from the Inspector's "
                "Add Component menu."
            );
        }
        return;
    }

    if (Animation* anim = scene.tryGet<Animation>(id)) {
        drawEditor(ec, *anim, *transform, id);
    } else {
        drawAddOffer(ec, id, *transform);
    }
}

void AnimationPanel::drawEditor(EditorContext& ec, Animation& anim, Transform& transform, EntityId entity) {
    // Authoring edits and their pose are one step per frame, merged per gesture; play/pause
    // are not undoable.
    const Animation before = anim;
    const Transform beforeTransform = transform;

    const std::function<void()> pose = [&] { AnimationSystem::poseAt(anim, anim.time, transform); };

    bool changed = drawTransport(anim, transform, pose);

    // Only AnimationSystem::fixedUpdate advances playback, so with the clock paused say it is held.
    if (anim.playing && ec.frame.clock.getSimDelta() <= 0.0f) {
        ImGui::TextDisabled("Held at %.2fs - it advances while the world runs.", anim.time);
    }

    const float duration = Animation::computeDuration(anim);

    ImGui::Spacing();
    changed |= drawTimeline(anim, duration, pose);

    ImGui::SetNextItemWidth(EditorStyle::px(140.0f));
    const float previousTime = anim.time;
    if (ImGui::DragFloat("Time", &anim.time, 0.005f, 0.0f, std::max(duration, 0.0f), "%.3f s", PROP_CLAMP)) {
        anim.time = std::clamp(anim.time, 0.0f, std::max(duration, 0.0f));
        if (anim.time != previousTime) {
            anim.playing = false;
            pose();
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("/ %.2f s", duration);

    ImGui::Spacing();
    sectionLabel("Tracks");

    const auto vec3Cell = [](size_t, const glm::vec3& in, glm::vec3& out) {
        out = in;
        ImGui::SetNextItemWidth(-1);
        return ImGui::DragFloat3("##v", glm::value_ptr(out), 0.01f, 0.0f, 0.0f, "%.3f", PROP_CLAMP);
    };
    const auto quatCell = [this](size_t index, const glm::quat& in, glm::quat& out) {
        EulerCache<int>& cache = m_rotationEulers[static_cast<int>(index)];
        cache.sync(static_cast<int>(index), in);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##v", cache.degrees(), 0.25f, 0.0f, 0.0f, "%.1f deg", PROP_CLAMP)) {
            out = cache.toQuat();
            return true;
        }
        out = in;
        return false;
    };

    changed |= drawTrack(
        "Position",
        "P",
        anim.positionTrack,
        anim.time,
        [&] { return transform.position; },
        vec3Cell,
        pose
    );
    changed |= drawTrack(
        "Rotation",
        "R",
        anim.rotationTrack,
        anim.time,
        [&] { return transform.rotation; },
        quatCell,
        pose
    );
    changed |= drawTrack(
        "Scale",
        "S",
        anim.scaleTrack,
        anim.time,
        [&] { return transform.scale; },
        vec3Cell,
        pose
    );

    // One composite, as CommandStack::push merges only into the top. The pose is in it, since
    // AnimationSystem does not re-pose outside play; in play poseStep leaves it out.
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    const char* label = changed ? "Edit Animation" : "Scrub Animation";
    const bool playing = ec.play.isPlaying();
    auto step = std::make_unique<CompositeCommand>(label);
    if (changed) step->add(editStep<Animation>(scene, resources, entity, before, anim, label));
    if (auto posed = poseStep(scene, resources, entity, beforeTransform, transform, playing, label)) {
        step->add(std::move(posed));
    }
    if (!step->empty()) {
        ec.state.pushStep(std::move(step));
    }
}

bool AnimationPanel::drawTransport(Animation& anim, Transform& transform, const std::function<void()>& pose) {
    const float height = ImGui::GetFrameHeight();
    const float gap    = EditorStyle::px(8.0f);

    const ClipTransport transport = clipTransport("an", anim, 0.0f, EditorStyle::px(110.0f));
    if (transport.rewound) pose();
    bool changed = transport.looped || transport.sped;

    ImGui::SameLine(0, gap);
    const bool setKey = iconButton(
        "ankey",
        EditorIcon::Key,
        false,
        true,
        "Set Key: add/replace keyframes on all 3 tracks at the current time",
        height
    );
    if (setKey) {
        anim.positionTrack.setKeyframe(anim.time, transform.position);
        anim.rotationTrack.setKeyframe(anim.time, transform.rotation);
        anim.scaleTrack.setKeyframe(anim.time, transform.scale);
        changed = true;
    }

    ImGui::SameLine(0, gap);
    ImGui::SetNextItemWidth(EditorStyle::px(110.0f));
    float length = anim.length;
    if (ImGui::DragFloat("Length", &length, 0.02f, 0.0f, FLT_MAX, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
        anim.length = std::max(0.0f, length);
        changed     = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Animation length in seconds (0 = auto from the last keyframe)");
    }

    return changed;
}

bool AnimationPanel::drawTimeline(Animation& anim, float duration, const std::function<void()>& pose) {
    const float laneHeight  = EditorStyle::px(16.0f);
    const float rulerHeight = EditorStyle::px(18.0f);
    const float hitRadius   = EditorStyle::px(7.0f);
    const float height      = rulerHeight + laneHeight * 3.0f + EditorStyle::px(6.0f);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float  width  = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##timeline", ImVec2(width, height));

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        origin,
        ImVec2(origin.x + width, origin.y + height),
        TIMELINE_BG_U32,
        EditorStyle::px(3.0f)
    );

    // A nonzero span, or the ruler divides by zero.
    const float span = duration > 1e-4f ? duration : 1.0f;
    const auto  timeToX  = [&](float t) { return origin.x + (t / span) * width; };
    const auto xToTime = [&](float x) {
        return std::clamp(((x - origin.x) / width) * span, 0.0f, span);
    };
    const auto laneY = [&](int lane) {
        return origin.y + rulerHeight + laneHeight * static_cast<float>(lane) + laneHeight * 0.5f;
    };

    for (int i = 0; i <= RULER_TICKS; ++i) {
        const float t = span * static_cast<float>(i) / RULER_TICKS;
        const float x = timeToX(t);
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + rulerHeight * 0.5f), TIMELINE_TICK_U32);
        char label[16];
        std::snprintf(label, sizeof(label), "%.2f", t);
        draw->AddText(ImVec2(x + 2, origin.y + 1), TIMELINE_LABEL_U32, label);
    }

    const std::vector<float>* times[3] = {
        &anim.positionTrack.getTimes(),
        &anim.rotationTrack.getTimes(),
        &anim.scaleTrack.getTimes(),
    };

    // Keyframe dot under the pointer: the hover highlight and the grab target.
    const ImVec2 pointer = ImGui::GetIO().MousePos;
    int hotTrack = -1;
    size_t hotIndex = 0;
    for (int lane = 0; lane < 3; ++lane) {
        const float y = laneY(lane);
        for (size_t k = 0; k < times[lane]->size(); ++k) {
            const float dx = timeToX((*times[lane])[k]) - pointer.x;
            const float dy = y - pointer.y;
            if (dx * dx + dy * dy <= hitRadius * hitRadius) {
                hotTrack = lane;
                hotIndex = k;
            }
        }
    }

    bool changed = false;
    if (ImGui::IsItemActivated()) {
        m_dotTrack = hotTrack;
        m_dotIndex = hotIndex;
        m_dotFromX = pointer.x;
        m_dotFrom  = hotTrack >= 0 ? (*times[hotTrack])[hotIndex] : 0.0f;
    }
    if (ImGui::IsItemActive()) {
        // A key moves by as far as the pointer has, so a dot clicked and held still stays put and
        // pushes no step.
        const float moved = xToTime(pointer.x) - xToTime(m_dotFromX);
        const float to    = std::clamp(m_dotFrom + moved, 0.0f, span);
        // Follow the keyframe, not the index: dragging past a neighbor re-sorts the track.
        const auto retime = [&](auto& track) {
            if (m_dotIndex >= track.keyframeCount() || track.getTimes()[m_dotIndex] == to) {
                return false;
            }
            m_dotIndex = track.setKeyframeTime(m_dotIndex, to);
            return true;
        };
        switch (m_dotTrack) {
            case 0:  changed = retime(anim.positionTrack);   break;
            case 1:  changed = retime(anim.rotationTrack);   break;
            case 2:  changed = retime(anim.scaleTrack);      break;
            default:
                anim.time    = xToTime(pointer.x);
                anim.playing = false;
                break;
        }
        pose();
    }
    if (ImGui::IsItemDeactivated()) m_dotTrack = -1;

    for (int lane = 0; lane < 3; ++lane) {
        const float y = laneY(lane);
        draw->AddText(
            ImVec2(origin.x + EditorStyle::px(3.0f), y - EditorStyle::px(7.0f)),
            LANES[lane].color,
            LANES[lane].label
        );
        draw->AddLine(
            ImVec2(origin.x + EditorStyle::px(16.0f), y),
            ImVec2(origin.x + width, y),
            TIMELINE_LANE_U32
        );
        for (size_t k = 0; k < times[lane]->size(); ++k) {
            const bool hot = (lane == hotTrack && k == hotIndex) || (lane == m_dotTrack && k == m_dotIndex);
            draw->AddCircleFilled(
                ImVec2(timeToX((*times[lane])[k]), y),
                hot ? EditorStyle::px(5.5f) : EditorStyle::px(3.5f),
                hot ? EditorStyle::HIGHLIGHT_U32 : LANES[lane].color
            );
        }
    }

    const float playhead = timeToX(anim.time);
    draw->AddLine(
        ImVec2(playhead, origin.y),
        ImVec2(playhead, origin.y + height),
        EditorStyle::HIGHLIGHT_U32,
        EditorStyle::px(1.5f)
    );

    if (hotTrack >= 0 && ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    return changed;
}

template<typename Track, typename Record, typename Edit>
bool AnimationPanel::drawTrack(
    const char* label,
    const char* tag,
    Track& track,
    float time,
    Record record,
    Edit edit,
    const std::function<void()>& pose
) {
    char header[48];
    std::snprintf(header, sizeof(header), "%s  (%zu)###%s", label, track.keyframeCount(), tag);
    if (!beginFold(header, true)) return false;

    const float rowHeight = ImGui::GetFrameHeight();
    bool changed = false;

    char addId[16];
    std::snprintf(addId, sizeof(addId), "ka%s", tag);
    const bool add = iconButton(
        addId,
        EditorIcon::Plus,
        false,
        true,
        "Add/replace a keyframe at the current time from the live transform",
        rowHeight
    );
    if (add) {
        track.setKeyframe(time, record());
        pose();
        changed = true;
    }
    ImGui::SameLine();

    char clearId[16];
    std::snprintf(clearId, sizeof(clearId), "kc%s", tag);
    const bool clear = iconButton(
        clearId,
        EditorIcon::Trash,
        false,
        track.keyframeCount() > 0,
        "Clear every keyframe on this track",
        rowHeight
    );
    if (clear) {
        track.clear();
        changed = true;
    }
    ImGui::SameLine(0, EditorStyle::px(12.0f));

    char easingId[24];
    std::snprintf(easingId, sizeof(easingId), "##e%s", tag);
    Math::Easing easing = track.getEasing();
    if (drawEnumCombo(easingId, easing)) {
        track.setEasing(easing);
        changed = true;
    }

    const size_t count = track.keyframeCount();
    if (count == 0) {
        endFold();
        return changed;
    }

    using Value = decltype(record());

    char tableId[16];
    std::snprintf(tableId, sizeof(tableId), "##kt%s", tag);
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable(tableId, 4, tableFlags)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##del");
        ImGui::TableHeadersRow();

        // Applied after the walk: each mutates the arrays the rows read.
        int   retimeIndex = -1, deleteIndex = -1, valueIndex = -1;
        float retimeTo    = 0.0f;
        Value valueTo{};

        const auto& times  = track.getTimes();
        const auto& values = track.getValues();
        for (size_t k = 0; k < count; ++k) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(k));

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%zu", k);

            ImGui::TableNextColumn();
            // A drag's time is held until it is let go, as a retime re-sorts the rows under it.
            float at = ImGui::GetActiveID() == ImGui::GetID("##t") ? m_timeHeld : times[k];
            ImGui::SetNextItemWidth(EditorStyle::px(74.0f));
            ImGui::DragFloat("##t", &at, 0.005f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemActive()) m_timeHeld = at;
            if (ImGui::IsItemDeactivatedAfterEdit() && at != times[k]) {
                retimeIndex = static_cast<int>(k);
                retimeTo    = at;
            }

            ImGui::TableNextColumn();
            Value edited{};
            if (edit(k, values[k], edited)) {
                valueIndex = static_cast<int>(k);
                valueTo    = edited;
            }

            ImGui::TableNextColumn();
            if (iconButton("kdel", EditorIcon::Cross, false, true, "Delete this keyframe", rowHeight)) {
                deleteIndex = static_cast<int>(k);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();

        if (deleteIndex >= 0) {
            track.removeKeyframe(static_cast<size_t>(deleteIndex));
        } else if (retimeIndex >= 0) {
            track.setKeyframeTime(static_cast<size_t>(retimeIndex), std::max(0.0f, retimeTo));
        } else if (valueIndex >= 0) {
            track.setKeyframeValue(static_cast<size_t>(valueIndex), valueTo);
        } else {
            endFold();
            return changed;
        }
        pose();
        changed = true;
    }

    endFold();
    return changed;
}

void AnimationPanel::drawAddOffer(EditorContext& ec, EntityId entity, Transform& transform) {
    Animation preview;
    preview.length = 5.0f;

    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float  width = ImGui::GetContentRegionAvail().x;
    // Measured before the ghost, which is taller than the panel and would center the button
    // below the fold.
    const float visibleHeight = ImGui::GetContentRegionAvail().y;

    // The real editor over a throwaway: disabled widgets report no change, so no step and no pose.
    ImGui::BeginDisabled();
    drawEditor(ec, preview, transform, entity);
    ImGui::EndDisabled();

    const float endY         = std::min(ImGui::GetCursorScreenPos().y, start.y + visibleHeight);
    const float buttonWidth  = EditorStyle::px(240.0f);
    const float buttonHeight = ImGui::GetFrameHeight() + EditorStyle::px(10.0f);
    const ImVec2 buttonOrigin(
        start.x + (width - buttonWidth) * 0.5f,
        (start.y + endY) * 0.5f - buttonHeight * 0.5f
    );

    const float halo = EditorStyle::px(14.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(buttonOrigin.x - halo, buttonOrigin.y - halo),
        ImVec2(buttonOrigin.x + buttonWidth + halo, buttonOrigin.y + buttonHeight + halo),
        TIMELINE_GHOST_U32,
        EditorStyle::px(6.0f)
    );

    ImGui::SetCursorScreenPos(buttonOrigin);
    if (!ImGui::Button("Add Animation Component", ImVec2(buttonWidth, buttonHeight))) return;

    Animation added;
    added.length = 5.0f;
    EditorActions::addComponent(
        ec.frame.scene,
        ec.state,
        entity,
        std::move(added),
        "Animation",
        "Add Animation"
    );
}

} // namespace Vkm::Engine
