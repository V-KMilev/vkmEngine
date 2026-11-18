#include "overlays/terrain_tool.h"

#include "core/frame_context.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

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
    const bool brushing = state.workspace.showTerrain && state.terrainTool.tool != TerrainTool::None;
    return brushing && !ec.input.gameHasViewport()
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

void followTerrainSelection(EditorState& state, const Scene& scene) {
    // On a change of selection only, so a terrain chosen in the window holds until the next.
    TerrainToolSettings& brush = state.terrainTool;
    const EntityId selected    = state.selection.active();
    if (selected != brush.followed) {
        brush.followed = selected;
        if (selected && scene.isAlive(selected) && scene.has<Terrain>(selected)) brush.target = selected;
    }
    const EntityId target = brush.target;
    if (target && (!scene.isAlive(target) || !scene.has<Terrain>(target))) state.terrainTool.target = {};
}

} // namespace Vkm::Engine
