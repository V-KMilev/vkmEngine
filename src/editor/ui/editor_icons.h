#pragma once

#include <imgui.h>

namespace Engine {

/**
 * @brief Vector icon set drawn with ImDrawList (no icon-font dependency).
 *
 * Shared by the viewport toolbar and the animation editor so iconography
 * stays consistent. Strictly ASCII source per the style guide - glyphs
 * are stroked/filled primitives, not font characters.
 */
enum class EditorIcon {
    Select, Move, Rotate, Scale,
    SpaceLocal, SpaceWorld, Snap,
    Duplicate, Focus, Trash,
    Play, Pause, Stop, Key, Plus, Cross,
    // Entity-type glyphs (Hierarchy / Inspector identity) - replace the old
    // [C]/[M]/[D] ASCII badges.
    Entity, Mesh, Camera, LightDir, LightPoint, LightSpot, Anim,
    // Singleton scene Environment (globe).
    Environment,
    // Viewport actions
    FrameAll, Screenshot
};

/// Draw @p icon centered at @p c with half-extent @p r in color @p col.
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
bool iconButton(const char* idStr, EditorIcon icon, bool active,
                bool enabled, const char* tooltip, float size = 26.0f);

} // namespace Engine
