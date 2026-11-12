#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <imgui.h>

#include "ecs/entity.h"
#include "ui/style/editor_icons.h"
#include "ui/style/editor_style.h"

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief The radius an entity marker's glyph is drawn inside; its disc reaches just past it.
 *
 * One size for every kind: the glyph says what, so a larger light would read as more important.
 *
 * @return Screen pixels.
 */
inline float entityMarkerRadius() { return EditorStyle::px(8.0f); }

/**
 * @brief Radius of a marker's disc and of its click target, so resizing the glyph cannot detune picking.
 *
 * @return Screen pixels.
 */
inline float entityMarkerHitRadius() { return entityMarkerRadius() + EditorStyle::px(1.0f); }

/**
 * @brief The viewport's entity markers: an icon on each light, camera, probe, effect, sound and joint,
 *        and an outline on each selected UI element, drawn each frame and kept for the pick.
 */
class EntityMarkers {
    public:
        EntityMarkers() = default;
        ~EntityMarkers() = default;

        EntityMarkers(const EntityMarkers& other) = delete;
        EntityMarkers& operator=(const EntityMarkers& other) = delete;

        EntityMarkers(EntityMarkers && other) = delete;
        EntityMarkers& operator=(EntityMarkers && other) = delete;

    public:
        /**
         * @brief One entity marker drawn this frame, kept for the picker.
         */
        struct Marker {
            EntityId  id;
            ImVec2    screen;        ///< Where it was drawn.
            glm::vec3 world;         ///< Its distance ranks it against meshes.
            float     reach = 0.0f;  ///< Half-size of a world box that also answers; 0 none.
        };

    public:
        /**
         * @brief Mark each light with its icon in its color; a box sized by its reach answers a click too.
         *
         * @param ec The frame's editor context.
         */
        void markLights(EditorContext& ec);

        /**
         * @brief Mark each camera with its icon, except the one the frame renders through.
         *
         * @param ec The frame's editor context.
         */
        void markCameras(EditorContext& ec);

        /**
         * @brief Mark each reflection probe and irradiance volume.
         *
         * @param ec The frame's editor context.
         */
        void markProbes(EditorContext& ec);

        /**
         * @brief Mark each decal and particle emitter.
         *
         * @param ec The frame's editor context.
         */
        void markEffects(EditorContext& ec);

        /**
         * @brief Mark each audio source and listener: a source faded while it has no clip and
         *        ringed while it plays, the listeners other than the ear faded.
         *
         * @param ec The frame's editor context.
         */
        void markAudio(EditorContext& ec);

        /**
         * @brief Mark each joint halfway along its rope, or at its anchor while it is off.
         *
         * Shown with GizmoVisibility::colliders.
         *
         * @param ec The frame's editor context.
         */
        void markJoints(EditorContext& ec);

        /**
         * @brief Outline each selected UI element's resolved rect, the active one in full highlight.
         *
         * A UI element is no object for the engine's selection boxes.
         *
         * @param ec The frame's editor context.
         */
        void drawUIRects(EditorContext& ec);

        /**
         * @brief The markers drawn this ImGui frame, for the pick.
         *
         * @return Empty when none were drawn this frame.
         */
        const std::vector<Marker>& drawn() const;

    private:
        /**
         * @brief Draw an entity's viewport marker and record it as a pick target.
         *
         * Every marker goes through here, so what is drawn is exactly what is pickable.
         *
         * @param ec Says what the Hierarchy hides, drawn as nothing, and locks, never recorded.
         * @param dl Draw list to append to.
         * @param icon Glyph naming the entity's kind.
         * @param id Entity a click selects.
         * @param screen Projected position.
         * @param world The point it marks.
         * @param col Glyph color.
         * @param reach Half-size of a world box around @p world that also answers, or 0.
         */
        void markEntity(
            const EditorContext& ec,
            ImDrawList* dl,
            EditorIcon icon,
            EntityId id,
            ImVec2 screen,
            const glm::vec3& world,
            ImU32 col,
            float reach = 0.0f
        );

    private:
        std::vector<Marker> m_markers;           ///< This frame's, for the picker.
        int                 m_markerFrame = -1;  ///< The ImGui frame they were drawn in.
};

} // namespace Vkm::Engine
