#pragma once

#include <imgui.h>

#include "ui/style/editor_style.h"

namespace Vkm::Engine {

/**
 * @brief The editor's icon set: glyphs of the Lucide font in Roots::engineFonts().
 */
enum class EditorIcon {
    Select, Move, Rotate, Scale,
    SpaceLocal, SpaceWorld, Snap,
    Focus, Trash,
    Play, Pause, Stop, Step, Loop, Key, Plus, Cross,
    Entity, Mesh, Camera, LightDir, LightPoint, LightSpot, Anim,
    Probe, Volume, Decal, Particle, UIWidget, Audio, Audio2D, Listener,
    UICanvas, UIText, UIImage, UIButton, UIScroll, UISlider, UIToggle, UIChoice,
    Cube, Sphere, Plane, Pyramid, Cone, Triangle,
    Empty, Import, Colliders, Material, Texture, Skeleton,
    Character, Ragdoll, Joint, Prefab, Socket,
    FrameAll, Perspective, Orthographic,
    Terrain, Raise, Lower, Smooth, Flatten, Paint, Plants,
    Info, Warning, Error, Check, More, Search, Copy, Terminal, ArrowDown, ArrowUp,
    ChevronDown, ChevronRight, Ellipsis, Weight, Script, Levels, Water, Fog, NetLocal, Grid,
    Eye, EyeOff, Lock, LockOpen,
    None  ///< No glyph: a card with no kind to show.
};

/**
 * @brief Load the editor icon font into the ImGui atlas, once, after the text font.
 *
 * @param path Filesystem path to the icon TTF.
 * @return Whether the font loaded; if not, every icon draws as one neutral square.
 */
bool loadEditorIconFont(const char* path);

/**
 * @brief Draw an icon centered at a point.
 *
 * @param dl Draw list to append to.
 * @param icon Glyph to render.
 * @param c Screen-space center.
 * @param r Half-extent, in screen pixels.
 * @param col Glyph color.
 */
void drawEditorIcon(ImDrawList* dl, EditorIcon icon, ImVec2 c, float r, ImU32 col);

/**
 * @brief Square icon button.
 *
 * @param idStr   Unique id fragment (becomes "###<idStr>").
 * @param icon    Glyph to render.
 * @param active  Accent highlight, for a toggled or selected state.
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
