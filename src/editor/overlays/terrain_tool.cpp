#include "overlays/terrain_tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include <imgui.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "command/editor_commands.h"
#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/terrain.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "overlays/wire_draw.h"
#include "resource/resource_manager.h"
#include "system/visibility/visibility.h"
#include "ui/editor_style.h"

namespace Vkm::Engine {

namespace {

/// Segments of the brush's rim.
constexpr int RIM_SEGMENTS = 64;
/// Metres a second a full-strength Raise lifts the brush's centre, per metre of its radius.
constexpr float RAISE_RATE = 0.5f;
/// Shares of the way a full-strength Smooth, Flatten, Paint or Plants moves a second; a dab takes
/// at most all of it.
constexpr float BLEND_RATE = 4.0f;
/// Metres along the cursor's ray the brush looks for the surface.
constexpr float BRUSH_RANGE = 5000.0f;
/// Metres the rim is drawn above the surface, clear of it.
constexpr float RIM_LIFT = 0.05f;

/// The terrain's placement in the world: its entity's position and turn, unscaled, as it is drawn.
glm::mat4 terrainModel(const Scene& scene, EntityId id) {
    const Transform& local = scene.get<Transform>(id);
    return glm::translate(glm::mat4(1.0f), resolvedWorldPosition(scene, id, local))
        * glm::mat4_cast(resolvedWorldRotation(scene, id, local));
}

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

/// The brush's rim at @p radius around local @p centre, laid on the surface.
void drawRim(
    ImDrawList* dl,
    const EditorContext& ec,
    const TerrainAsset& terrain,
    const glm::mat4& model,
    glm::vec2 centre,
    float radius,
    ImU32 colour
) {
    const glm::mat4& vp = ec.frame.visibility->camera.viewProjection;
    glm::vec3 last{};
    for (int k = 0; k <= RIM_SEGMENTS; ++k) {
        const float     turn = glm::two_pi<float>() * static_cast<float>(k) / RIM_SEGMENTS;
        const glm::vec2 xz   = centre + radius * glm::vec2(std::cos(turn), std::sin(turn));
        const float     y    = terrain.heightAt(xz.x, xz.y) + RIM_LIFT;
        const glm::vec3 at   = glm::vec3(model * glm::vec4(xz.x, y, xz.y, 1.0f));
        const float     wide = EditorStyle::px(2.0f);
        if (k > 0) wireSegment(dl, vp, last, at, ec.viewportPos, ec.viewportSize, colour, wide);
        last = at;
    }
}

} // namespace

EntityId terrainToolTarget(const EditorContext& ec) {
    const Scene&   scene = ec.frame.scene;
    const EntityId id    = ec.state.terrainTool.target;
    const bool     alive = id && scene.isAlive(id) && scene.has<Terrain>(id) && scene.has<Transform>(id);
    return alive ? id : EntityId{};
}

bool terrainToolApplies(const EditorContext& ec) {
    const EditorState& state  = ec.state;
    const EntityId     target = terrainToolTarget(ec);
    // An entity whose Terrain names no live asset has nothing to brush.
    return state.showTerrain && state.terrainTool.tool != TerrainTool::None && !ec.input.gameHasViewport()
        && target && ec.frame.resources.isAlive(ec.frame.scene.get<Terrain>(target).terrain);
}

const char* terrainToolHint(TerrainTool tool) {
    switch (tool) {
        case TerrainTool::Raise:   return "Drag to raise the ground; Shift lowers it";
        case TerrainTool::Lower:   return "Drag to lower the ground; Shift raises it";
        case TerrainTool::Smooth:  return "Drag to soften bumps and edges";
        case TerrainTool::Flatten: return "Drag to level the ground; Ctrl+click takes a height";
        case TerrainTool::Paint:   return "Drag to paint the chosen layer";
        case TerrainTool::Plants:  return "Drag to grow the chosen plants; Shift thins them";
        case TerrainTool::None:
        case TerrainTool::Count:   break;
    }
    return "";
}

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
    const glm::mat4 model   = terrainModel(ctx.scene, target);
    const glm::mat4 toLocal = glm::inverse(model);
    const Math::Ray ray     = viewportRay(
        ctx.visibility->camera.invViewProj,
        ImGui::GetMousePos(),
        ec.viewportPos,
        ec.viewportSize
    );
    const glm::vec3 origin    = glm::vec3(toLocal * glm::vec4(ray.origin, 1.0f));
    const glm::vec3 direction = glm::normalize(glm::mat3(toLocal) * ray.direction);
    float           t         = 0.0f;
    const bool overScene = ec.input.pointer == PointerOwner::Scene || m_terrain;
    if (!overScene || !TerrainBrush::hit(terrain, origin, direction, BRUSH_RANGE, t)) return false;
    const glm::vec3 at = origin + direction * t;

    TerrainToolSettings& brush  = state.terrainTool;
    const bool           invert = ImGui::GetIO().KeyShift;
    const bool invertible = brush.tool == TerrainTool::Raise || brush.tool == TerrainTool::Lower
        || brush.tool == TerrainTool::Plants;
    if (ImDrawList* dl = ImGui::GetWindowDrawList()) {
        const ImVec2 end(ec.viewportPos.x + ec.viewportSize.x, ec.viewportPos.y + ec.viewportSize.y);
        dl->PushClipRect(ec.viewportPos, end, true);
        // Shift held turns the rim to the warning colour: the drag does the opposite.
        const ImU32 rim = invert && invertible
            ? ImGui::GetColorU32(EditorStyle::WARNING)
            : EditorStyle::HIGHLIGHT_U32;
        drawRim(dl, ec, terrain, model, {at.x, at.z}, brush.radius, rim);
        // The hard core, fainter: within it the brush works in full.
        if (brush.hardness > 0.0f && brush.hardness < 1.0f) {
            const ImU32 faint = (rim & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 128);
            drawRim(dl, ec, terrain, model, {at.x, at.z}, brush.radius * brush.hardness, faint);
        }
        ImVec2          centre;
        const glm::mat4& vp    = ctx.visibility->camera.viewProjection;
        const glm::vec3  world = glm::vec3(model * glm::vec4(at, 1.0f));
        if (projectToViewport(vp, world, ec.viewportPos, ec.viewportSize, centre)) {
            dl->AddCircleFilled(centre, EditorStyle::px(2.5f), rim);
        }
        dl->PopClipRect();
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
    dab.centre   = {at.x, at.z};
    dab.radius   = brush.radius;
    dab.hardness = brush.hardness;
    dab.strength = std::min(brush.strength * BLEND_RATE * dt, 1.0f);
    TerrainBrush::Region touched;
    switch (brush.tool) {
        case TerrainTool::Raise:
        case TerrainTool::Lower: {
            const bool  up     = (brush.tool == TerrainTool::Raise) != invert;
            const float metres = brush.strength * RAISE_RATE * brush.radius * dt;
            dab.strength = 1.0f;
            touched = TerrainBrush::raise(terrain, dab, up ? metres : -metres);
            break;
        }
        case TerrainTool::Smooth:  touched = TerrainBrush::smooth(terrain, dab);                    break;
        case TerrainTool::Flatten: touched = TerrainBrush::flatten(terrain, dab, m_flatten);        break;
        case TerrainTool::Paint:   touched = TerrainBrush::paint(terrain, dab, brush.layer);        break;
        case TerrainTool::Plants:
            // A pick past the end, from another terrain or an undone Add, grows the last layer.
            if (!terrain.foliage.empty()) {
                const auto last = static_cast<uint32_t>(terrain.foliage.size() - 1);
                brush.foliage   = std::min(brush.foliage, last);
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
    const auto&                keys  = ec.state.prefs.keybinds;
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
    const float  foot = ec.viewportPos.y + ec.viewportSize.y - EditorStyle::overlayInset();
    const float  left = ec.viewportPos.x + (ec.viewportSize.x - text.x) * 0.5f - padX;
    const ImVec2 min(left, foot - text.y - 2.0f * padY);
    const ImVec2 max(min.x + text.x + 2.0f * padX, foot);
    const float  round = EditorStyle::px(6.0f);
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
