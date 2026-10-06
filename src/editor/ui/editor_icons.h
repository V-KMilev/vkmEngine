#pragma once

#include <imgui.h>

#include "ui/editor_style.h"

namespace Vkm::Engine {

/**
 * @brief The editor's icon set, drawn through ImDrawList.
 *
 * Glyphs come from the Lucide font in ProjectPaths::engineFonts().
 */
enum class EditorIcon {
    Select, Move, Rotate, Scale,
    SpaceLocal, SpaceWorld, Snap,
    Focus, Trash,
    Play, Pause, Stop, Step, Loop, Key, Plus, Cross,
    Entity, Mesh, Camera, LightDir, LightPoint, LightSpot, Anim,
    Probe, Volume, Decal, Particle, UIWidget, Audio, Audio2D, Listener,
    UICanvas, UIText, UIImage, UIButton, UIScroll,
    LightRect, LightDisk,
    Cube, Sphere, Plane, Pyramid, Cone, Triangle,
    Empty, Import, Colliders, Material, Texture, Skeleton,
    Character, Ragdoll, Joint, Prefab, Socket,
    FrameAll
};

/**
 * @brief Load the editor icon font (Lucide) into the ImGui atlas.
 *
 * Call once at editor init, after the text font. On failure every icon draws as one
 * neutral square.
 *
 * @param path Filesystem path to the icon TTF.
 * @return Whether the font loaded.
 */
bool loadEditorIconFont(const char* path);

/**
 * @brief Draw an icon centered at a point with a given half-extent.
 *
 * @param dl Draw list to append to.
 * @param icon Glyph to render.
 * @param c Screen-space centre.
 * @param r Half-extent in pixels.
 * @param col Glyph colour.
 */
void drawEditorIcon(ImDrawList* dl, EditorIcon icon, ImVec2 c, float r, ImU32 col);

/**
 * @brief The radius an entity marker is sized from: its glyph is drawn inside it, its disc just past it.
 *
 * Font-relative, and one size for every kind: the glyph says what, so a larger light
 * would read as more important.
 *
 * @return Screen pixels.
 */
inline float entityMarkerRadius() { return EditorStyle::px(8.0f); }

/**
 * @brief Outer radius of an entity marker, the backing disc included.
 *
 * Both the disc and the click target, so resizing the glyph cannot detune picking.
 *
 * @return Screen pixels.
 */
inline float entityMarkerHitRadius() { return entityMarkerRadius() + EditorStyle::px(1.0f); }

/**
 * @brief Draw the viewport's marker for an entity: a glyph on a dim disc.
 *
 * Drawn through GizmoOverlay::markEntity, which records it so a click within
 * entityMarkerHitRadius() selects the entity.
 *
 * @param dl Draw list to append to.
 * @param icon Glyph naming the entity's kind.
 * @param center Projected screen-space position.
 * @param col Glyph colour; the disc is fixed.
 */
void drawEntityMarker(ImDrawList* dl, EditorIcon icon, ImVec2 center, ImU32 col);

/**
 * @brief Square icon button.
 *
 * @param idStr   Unique id fragment (becomes "###<idStr>").
 * @param icon    Glyph to render.
 * @param active  Accent highlight (toggle/selected state).
 * @param enabled False disables and dims it.
 * @param tooltip Formatted hover tooltip; may be null.
 * @param size    Side length in screen pixels; no default, which would not scale with the font.
 * @return True on the frame the button is pressed.
 */
bool iconButton(
    const char* idStr,
    EditorIcon icon,
    bool active,
    bool enabled,
    const char* tooltip,
    float size
);

} // namespace Vkm::Engine
