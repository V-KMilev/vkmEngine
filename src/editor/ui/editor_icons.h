#pragma once

#include <imgui.h>

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
    Probe, Volume, Decal, Particle, UIWidget,
    UICanvas, UIText, UIImage, UIButton,
    LightRect, LightDisk,
    Cube, Sphere, Plane, Pyramid, Cone, Triangle,
    Empty, Import, Colliders,
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
 * @brief Square icon button.
 *
 * @param idStr   Unique id fragment (becomes "###<idStr>").
 * @param icon    Which glyph to render.
 * @param active  Highlight with the accent color (toggle/selected state).
 * @param enabled When false the button is disabled and dimmed.
 * @param tooltip Optional hover tooltip (already formatted), may be null.
 * @param size    Button side length in pixels.
 * @return true on the frame the button is pressed.
 */
bool iconButton(
    const char* idStr,
    EditorIcon icon,
    bool active,
    bool enabled,
    const char* tooltip,
    float size = 26.0f
);

} // namespace Vkm::Engine
