#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>

#include "ecs/entity.h"
#include "ui/editor_widgets.h"
#include "ui/euler_cache.h"

namespace Vkm::Engine {

struct Animation;
struct EditorContext;
struct Transform;

/**
 * @brief Keyframe editing for the selected entity's Animation component; the playhead poses its Transform.
 *
 * Its drag state and euler caches must survive frames spent behind another tab.
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
         */
        void draw(EditorContext& ec);

    private:
        /**
         * @brief Draw the whole editor over one Animation, and record the edit.
         *
         * Pushes one composite step per changed frame, the Transform the pose wrote included.
         *
         * @param ec The frame's editor context.
         * @param anim The animation being edited.
         * @param transform Posed as the playhead moves.
         * @param entity Owner of both, for the undo command.
         */
        void drawEditor(EditorContext& ec, Animation& anim, Transform& transform, EntityId entity);

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
         * @param ec The frame's editor context.
         * @param entity The selection, which has no Animation.
         * @param transform Its transform, left unposed.
         */
        void drawAddOffer(EditorContext& ec, EntityId entity, Transform& transform);

    private:
        int    m_dotTrack = -1;    ///< The dragged keyframe's lane: 0/1/2 position/rotation/scale, -1 none.
        size_t m_dotIndex = 0;     ///< By index, not time: float equality breaks once the drag retimes it.
        float  m_dotFromX = 0.0f;  ///< Where the pointer took the dot, screen pixels.
        float  m_dotFrom  = 0.0f;  ///< The key's time then, seconds.
        float  m_timeHeld = 0.0f;  ///< A key's Time cell as dragged: the track takes it once let go.

        std::unordered_map<int, EulerCache<int>> m_rotationEulers;  ///< One per rotation key row: a shared
                                                                    ///< EulerCache would reseed every frame.
};

} // namespace Vkm::Engine
