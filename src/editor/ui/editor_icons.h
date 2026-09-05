#pragma once

#include <imgui.h>

#include "ui/editor_style.h"

namespace Vkm::Engine {

/**
 * @brief The editor's icon set, drawn through ImDrawList.
 *
 * Shared across the viewport toolbar, playback bar and panels so iconography
 * stays consistent. Each icon renders as a glyph from the Lucide font, which
 * ships with the engine beside the shaders. Without it every icon degrades to
 * the same neutral square - buttons stay clickable and keep their tooltips,
 * but the set is gone. Strictly ASCII source per the style guide.
 */
enum class EditorIcon {
    Select, Move, Rotate, Scale,
    SpaceLocal, SpaceWorld, Snap,
    Duplicate, Focus, Trash,
    Play, Pause, Stop, Step, Loop, Key, Plus, Cross,
    // Entity-type glyphs (Hierarchy / Inspector identity).
    Entity, Mesh, Camera, LightDir, LightPoint, LightSpot, Anim,
    Probe, Volume, Decal, Particle, UIWidget, Audio, Audio2D, Listener,
    UICanvas, UIText, UIImage, UIButton,
    LightRect, LightDisk,
    Cube, Sphere, Plane, Pyramid, Cone, Triangle,
    Empty, Import, Colliders, Material, Texture, Skeleton,
    Character, Ragdoll, Joint, Prefab, Socket,
    FrameAll
};

/**
 * @brief Load the editor icon font (Lucide) into the ImGui atlas.
 *
 * Call once at editor init, after the text font is added. The font ships with
 * the engine, so a failure here means the installed file was removed by hand;
 * the editor stays usable but every icon draws as the same neutral square.
 *
 * @param path Filesystem path to the icon TTF.
 * @return Whether the font loaded.
 */
bool loadEditorIconFont(const char* path);

/**
 * @brief Draw an icon centered at a point with a given half-extent.
 *
 * @param dl Draw list to append the icon primitives to.
 * @param icon Which glyph to render.
 * @param c Center of the icon in screen space.
 * @param r Half-extent (radius) of the icon in pixels.
 * @param col Color the glyph is stroked/filled with.
 */
void drawEditorIcon(ImDrawList* dl, EditorIcon icon, ImVec2 c, float r, ImU32 col);

/**
 * @brief Glyph half-extent of the marker an overlay draws to say an entity is here.
 *
 * Font-relative, so the marker keeps its size against the panels around it on a
 * scaled display rather than staying at 1x while the chrome grows.
 *
 * One size whatever it marks: the marker's job is to say that something is
 * there and the glyph inside it says what, so a light drawn larger than a
 * camera would read as a more important light rather than as a different kind
 * of thing.
 */
inline float entityMarkerRadius() { return EditorStyle::px(8.0f); }

/**
 * @brief Outer radius of an entity marker, the backing disc included.
 *
 * What a click has to land inside to hit the marker, and what the disc is
 * drawn at. Derived here rather than restated by the picker because the two
 * are one marker: stated twice, resizing the glyph would silently detune the
 * target that answers a click on it, with nothing failing to build to say so.
 */
inline float entityMarkerHitRadius() { return entityMarkerRadius() + EditorStyle::px(1.0f); }

/**
 * @brief Draw the viewport's marker for an entity: a glyph on a dim disc.
 *
 * The disc is what makes the glyph read against a bright sky or a white wall,
 * so the two are one marker rather than a glyph with a decoration behind it.
 * Lights, cameras, audio sources and listeners all mark themselves this way,
 * and the picker answers a click within entityMarkerHitRadius() of @p center
 * with the entity that drew it - so a kind that marks itself here is a kind
 * that can be selected.
 *
 * @param dl Draw list to append the marker to.
 * @param icon Glyph naming what kind of entity is there.
 * @param center Screen-space position of the entity, already projected.
 * @param col Colour of the glyph; the disc behind it is fixed.
 */
void drawEntityMarker(ImDrawList* dl, EditorIcon icon, ImVec2 center, ImU32 col);

/**
 * @brief Square icon button.
 *
 * @param idStr   Unique id fragment (becomes "###<idStr>").
 * @param icon    Which glyph to render.
 * @param active  Highlight with the accent color (toggle/selected state).
 * @param enabled When false the button is disabled and dimmed.
 * @param tooltip Optional hover tooltip (already formatted), may be null.
 * @param size    Button side length in framebuffer pixels. Stated by every
 *                caller: a default here would be a raw number that does not
 *                scale with the font, which is the one thing an editor metric
 *                must do.
 * @return true on the frame the button is pressed.
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
