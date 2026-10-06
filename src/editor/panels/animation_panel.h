#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>

#include "ecs/entity.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

struct Animation;
struct EditorContext;
struct Transform;
class SceneIOController;

/**
 * @brief Keyframe editing for the selected entity's Animation component.
 *
 * A transport, a draggable three-lane timeline and a table per track, all posing the
 * entity's Transform as the playhead moves. Its drag state and euler caches must survive
 * frames spent behind another tab.
 */
class AnimationPanel {
    public:
        AnimationPanel()  = default;
        ~AnimationPanel() = default;

        AnimationPanel(const AnimationPanel& other) = delete;
        AnimationPanel& operator=(const AnimationPanel& other) = delete;

        AnimationPanel(AnimationPanel && other) = delete;
        AnimationPanel& operator=(AnimationPanel && other) = delete;

    public:
        /**
         * @brief Draw the tab: the editor for the selection, or what is missing.
         *
         * @param ec The frame's editor context.
         * @param sceneIO Play state: decides whether a scrub is authoring.
         */
        void draw(EditorContext& ec, SceneIOController& sceneIO);

    private:
        /**
         * @brief Draw the whole editor over one Animation, and record the edit.
         *
         * One composite step per changed frame: the Animation and the authored Transform the
         * pose writes, since AnimationSystem does not re-pose outside play. One composite
         * because CommandStack::push merges only into the top. During play the pose is
         * left out; Stop discards it.
         *
         * @param ec The frame's editor context.
         * @param sceneIO Play state.
         * @param anim The animation being edited.
         * @param transform Posed as the playhead moves.
         * @param entity Owner of both, for the undo command.
         */
        void drawEditor(
            EditorContext& ec,
            SceneIOController& sceneIO,
            Animation& anim,
            Transform& transform,
            EntityId entity
        );

        /**
         * @brief The shared clip transport, then Set Key and the length field.
         *
         * @param anim The animation being edited.
         * @param transform Set Key reads its pose.
         * @param pose Called when the playhead moves.
         * @return Whether this row authored something undoable.
         */
        bool drawTransport(Animation& anim, Transform& transform, const std::function<void()>& pose);

        /**
         * @brief The ruler, the three keyframe lanes and the playhead.
         *
         * A press on a dot grabs and retimes it; elsewhere it scrubs the playhead.
         *
         * @param anim The animation being edited.
         * @param duration Resolved length the ruler spans.
         * @param pose Called when the playhead or a keyframe moves.
         * @return Whether a keyframe was retimed (a scrub alone is not an edit).
         */
        bool drawTimeline(Animation& anim, float duration, const std::function<void()>& pose);

        /**
         * @brief One track's header, easing picker and keyframe table.
         *
         * @tparam Track Position, rotation or scale track.
         * @tparam Record Callable returning the transform's current value.
         * @tparam Edit Callable drawing one value cell: (index, in, out) -> bool.
         * @param label Header text.
         * @param tag Stable id, unique per track within the panel.
         * @param track The track being edited.
         * @param time The playhead, where an added keyframe lands.
         * @param record Reads a new keyframe's value.
         * @param edit Draws and edits one keyframe's value.
         * @param pose Called when the track changes.
         * @return Whether the track was changed.
         */
        template<typename Track, typename Record, typename Edit>
        bool drawTrack(
            const char* label,
            const char* tag,
            Track& track,
            float time,
            Record record,
            Edit edit,
            const std::function<void()>& pose
        );

        /**
         * @brief Draw the disabled editor and the offer to add the component.
         *
         * The real editor over a throwaway Animation, safe because disabled widgets report no
         * change: no undo step, no pose.
         *
         * @param ec The frame's editor context.
         * @param sceneIO Play state.
         * @param entity The selection, which has no Animation.
         * @param transform Its transform, left unposed.
         */
        void drawAddOffer(
            EditorContext& ec,
            SceneIOController& sceneIO,
            EntityId entity,
            Transform& transform
        );

    private:
        /**
         * @brief The dragged keyframe's track: -1 none, else 0/1/2 for position, rotation, scale.
         *
         * By index, not time: float equality breaks once the drag retimes it.
         */
        int    m_dotTrack = -1;
        size_t m_dotIndex = 0;

        /**
         * @brief One euler cache per rotation keyframe row.
         *
         * Not one shared cache: EulerCache reseeds when its key changes, so the next row
         * would reseed it every frame.
         */
        std::unordered_map<int, EulerCache<int>> m_rotationEulers;
};

} // namespace Vkm::Engine
