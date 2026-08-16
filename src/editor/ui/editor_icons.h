#pragma once

#include <imgui.h>

namespace Engine {

/**
 * @brief The editor's icon set, drawn through ImDrawList.
 *
 * Shared across the viewport toolbar, playback bar and panels so iconography
 * stays consistent. Each icon renders as a glyph from the Lucide font when
 * loadEditorIconFont() succeeded, and as a stroked/filled vector primitive
 * otherwise - so a stripped install still has icons. Strictly ASCII source
 * per the style guide.
 */
enum class EditorIcon {
    Select, Move, Rotate, Scale,
    SpaceLocal, SpaceWorld, Snap,
    Duplicate, Focus, Trash,
    Play, Pause, Stop, Step, Loop, Key, Plus, Cross,
    // Entity-type glyphs (Hierarchy / Inspector identity).
    Entity, Mesh, Camera, LightDir, LightPoint, LightSpot, Anim,
    Probe, Volume, Decal, Particle, UIWidget,
    // Fine-grained entity glyphs (icon-font set; vector path falls back to
    // the coarse glyph above).
    UICanvas, UIText, UIImage, UIButton,
    LightRect, LightDisk,
    Cube, Sphere, Plane, Pyramid, Cone, Triangle,
    Empty, Import, Colliders,
    FrameAll
};

/**
 * @brief Load the editor icon font (Lucide) into the ImGui atlas.
 *
 * Call once at editor init, after the text font is added. When the file is
 * missing the editor falls back to the built-in vector glyphs, so a stripped
 * install still has icons - just not the designed set.
 *
 * @param path Filesystem path to the icon TTF.
 * @return Whether the font loaded.
 */
bool loadEditorIconFont(const char* path);

/**
 * @brief Draw a vector icon centered at a point with a given half-extent.
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

} // namespace Engine
