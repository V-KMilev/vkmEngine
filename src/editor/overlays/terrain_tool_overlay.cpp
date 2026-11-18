#include "overlays/terrain_tool_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include <imgui.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "undo/asset_commands.h"
#include "core/frame_context.h"
#include "debug/draw/debug_draw.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "view/viewport_projection.h"
#include "resource/resource_manager.h"
#include "core/frame/visibility.h"
#include "ui/style/editor_style.h"

#include "overlays/terrain_tool.h"
#include "overlays/overlay_strip.h"

namespace Vkm::Engine {

namespace {

constexpr int RIM_SEGMENTS = 64;  ///< Segments of the brush's rim.
constexpr float RAISE_RATE = 0.5f;  ///< Meters a second a full-strength Raise lifts the brush's center, per
                                    ///< meter of its radius.
constexpr float BLEND_RATE = 4.0f;  ///< Share of the way a full-strength Smooth, Flatten, Paint or Plants
                                    ///< moves a second, capped at all per dab.
constexpr float BRUSH_RANGE = 5000.0f;  ///< Meters along the cursor's ray the brush looks for the surface.
constexpr float RIM_LIFT = 0.05f;  ///< Meters the rim is drawn above the surface, clear of it.

/// The undo step a stroke of @p tool makes.
const char* strokeLabel(TerrainTool tool) {
    switch (tool) {
        case TerrainTool::Raise:   return "Raise Terrain";
        case TerrainTool::Lower:   return "Lower Terrain";
        case TerrainTool::Smooth:  return "Smooth Terrain";
        case TerrainTool::Flatten: return "Flatten Terrain";
        case TerrainTool::Paint:   return "Paint Terrain";
        case TerrainTool::Plants:  return "Grow Plants";
        case TerrainTool::None:
        case TerrainTool::Count:   break;
    }
    return "Terrain Stroke";
}

/// The brush's rim at @p radius around local @p center, laid on the surface.
void drawRim(
    DebugDraw& draw,
    const TerrainAsset& terrain,
    const glm::mat4& model,
    glm::vec2 center,
    float radius,
    const DebugStyle& style
) {
    glm::vec3 rim[RIM_SEGMENTS];
    for (int k = 0; k < RIM_SEGMENTS; ++k) {
        const float turn = glm::two_pi<float>() * static_cast<float>(k) / RIM_SEGMENTS;
        const glm::vec2 xz = center + radius * glm::vec2(std::cos(turn), std::sin(turn));
        const float y = terrain.heightAt(xz.x, xz.y) + RIM_LIFT;
        rim[k] = glm::vec3(model * glm::vec4(xz.x, y, xz.y, 1.0f));
    }
    draw.polyline(rim, RIM_SEGMENTS, style, true);
}

} // namespace

bool TerrainToolOverlay::update(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    TerrainHandle  handle;
    const EntityId target = terrainToolApplies(ec) ? state.terrainTool.target : EntityId{};
    if (target && ctx.visibility && ctx.visibility->hasCamera) {
        handle = ctx.scene.get<Terrain>(target).terrain;
    }
    // A stroke ends when the button lets go, or the tool or the terrain moves on.
    if (m_terrain && (m_terrain != handle || !ImGui::IsMouseDown(ImGuiMouseButton_Left))) finish(ec);
    if (!handle) return false;
    drawStrip(ec);

    TerrainAsset&   terrain = ctx.resources.edit(handle);
    const glm::mat4 model   = terrainModel(ctx.scene, target, ctx.scene.get<Transform>(target));
    const glm::mat4 toLocal = glm::inverse(model);
    const Math::Ray ray     = viewportRay(
        ctx.visibility->camera.invViewProj,
        ImGui::GetMousePos(),
        ec.viewportPos,
        ec.viewportSize
    );
    const glm::vec3 origin    = glm::vec3(toLocal * glm::vec4(ray.origin, 1.0f));
    const glm::vec3 direction = glm::normalize(glm::mat3(toLocal) * ray.direction);
    float t = 0.0f;
    const bool overScene = ec.input.pointer == PointerOwner::Scene || m_terrain;
    if (!overScene || !TerrainBrush::hit(terrain, origin, direction, BRUSH_RANGE, t)) return false;
    const glm::vec3 at = origin + direction * t;

    TerrainToolSettings& brush = state.terrainTool;
    const bool invert     = ImGui::GetIO().KeyShift;
    const bool invertible = brush.tool == TerrainTool::Raise || brush.tool == TerrainTool::Lower
        || brush.tool == TerrainTool::Plants;
    if (ctx.debug) {
        // Shift held turns the rim to the warning color: the drag does the opposite.
        const ImVec4&    warning = EditorStyle::WARNING;
        const glm::vec4  color   = invert && invertible
            ? glm::vec4(warning.x, warning.y, warning.z, warning.w)
            : DebugPalette::HIGHLIGHT.value();
        const DebugStyle rim = DebugStyle(color).wide(2.0f);
        drawRim(*ctx.debug, terrain, model, {at.x, at.z}, brush.radius, rim);
        // The hard core, fainter: within it the brush works in full.
        if (brush.hardness > 0.0f && brush.hardness < 1.0f) {
            const DebugStyle faint = DebugStyle(glm::vec4(glm::vec3(color), 0.5f)).wide(2.0f);
            drawRim(*ctx.debug, terrain, model, {at.x, at.z}, brush.radius * brush.hardness, faint);
        }
        ctx.debug->point(glm::vec3(model * glm::vec4(at, 1.0f)), DebugStyle(rim).wide(1.25f));
    }

    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ec.input.clickPicks();
    // Ctrl+click with Flatten takes the height under the cursor for the strokes after it.
    if (clicked && !m_terrain && brush.tool == TerrainTool::Flatten && ImGui::GetIO().KeyCtrl) {
        brush.height      = at.y;
        brush.fixedHeight = true;
        return true;
    }
    if (clicked && !m_terrain) {
        m_terrain = handle;
        m_before  = TerrainAsset(terrain);
        m_touched = {};
        m_flatten = brush.fixedHeight ? brush.height : at.y;
        m_label   = strokeLabel(brush.tool);
    }
    if (!m_terrain) return false;

    const float dt = ImGui::GetIO().DeltaTime;
    TerrainBrush::Dab dab;
    dab.center   = {at.x, at.z};
    dab.radius   = brush.radius;
    dab.hardness = brush.hardness;
    dab.strength = std::min(brush.strength * BLEND_RATE * dt, 1.0f);
    TerrainBrush::Region touched;
    switch (brush.tool) {
        case TerrainTool::Raise:
        case TerrainTool::Lower: {
            const bool up = (brush.tool == TerrainTool::Raise) != invert;
            const float meters = brush.strength * RAISE_RATE * brush.radius * dt;
            dab.strength = 1.0f;
            touched      = TerrainBrush::raise(terrain, dab, up ? meters : -meters);
            break;
        }
        case TerrainTool::Smooth:  touched = TerrainBrush::smooth(terrain, dab);                    break;
        case TerrainTool::Flatten: touched = TerrainBrush::flatten(terrain, dab, m_flatten);        break;
        case TerrainTool::Paint:   touched = TerrainBrush::paint(terrain, dab, brush.layer);        break;
        case TerrainTool::Plants:
            // A pick past the end, from another terrain or an undone Add, grows the last layer.
            if (!terrain.foliage.empty()) {
                const auto last = static_cast<uint32_t>(terrain.foliage.size() - 1);
                brush.foliage = std::min(brush.foliage, last);
            }
            touched = TerrainBrush::grow(terrain, dab, brush.foliage, invert ? 0.0f : 1.0f);
            break;
        case TerrainTool::None:
        case TerrainTool::Count:
            break;
    }
    if (!touched.empty()) {
        m_touched.add(touched);
        ctx.resources.commit(handle);
    }
    return clicked;
}

void TerrainToolOverlay::drawStrip(const EditorContext& ec) const {
    const TerrainToolSettings& brush = ec.state.terrainTool;
    const auto& keys = ec.state.prefs.keybinds;
    char line[256];
    snprintf(
        line,
        sizeof(line),
        "%s   %.1f m   \xc2\xb7   %s   \xc2\xb7   %s %s size   \xc2\xb7   %s puts it down",
        Reflect::enumName(brush.tool),
        brush.radius,
        terrainToolHint(brush.tool),
        keyLabel(keys.terrainBrushSmaller).buf,
        keyLabel(keys.terrainBrushLarger).buf,
        keyLabel(keys.deselect).buf
    );
    ImDrawList*  dl   = ImGui::GetWindowDrawList();
    const ImVec2 text = ImGui::CalcTextSize(line);
    const float  padX = EditorStyle::px(12.0f);
    const float  padY = EditorStyle::px(6.0f);
    const float  foot = ec.viewportPos.y + ec.viewportSize.y - OverlayStrip::inset();
    const float  left = ec.viewportPos.x + (ec.viewportSize.x - text.x) * 0.5f - padX;
    const ImVec2 min(left, foot - text.y - 2.0f * padY);
    const ImVec2 max(min.x + text.x + 2.0f * padX, foot);
    const float round = EditorStyle::px(6.0f);
    dl->AddRectFilled(min, max, ImGui::GetColorU32(EditorStyle::OVERLAY_BG), round);
    dl->AddRect(min, max, ImGui::GetColorU32(EditorStyle::OVERLAY_EDGE), round);
    dl->AddText(ImVec2(min.x + padX, min.y + padY), ImGui::GetColorU32(ImGuiCol_Text), line);
}

void TerrainToolOverlay::finish(EditorContext& ec) {
    ResourceManager& resources = ec.frame.resources;
    if (!m_touched.empty() && resources.isAlive(m_terrain)) {
        const TerrainAsset& now = resources.get(m_terrain);
        auto stroke = std::make_unique<TerrainStrokeCommand>(
            resources,
            m_terrain,
            m_touched,
            TerrainStrokeCommand::Patch::cut(m_before, m_touched),
            TerrainStrokeCommand::Patch::cut(now, m_touched),
            m_label
        );
        ec.state.pushStep(std::move(stroke));
    }
    m_terrain = {};
    m_touched = {};
    m_before  = TerrainAsset{};
}

} // namespace Vkm::Engine
