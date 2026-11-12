#include "panels/inspector/cards.h"

#include <cstdint>

#include <imgui.h>

#include "ecs/component/render/mesh.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/component/landscape/water.h"
#include "resource/asset/terrain_asset.h"
#include "ui/editor_widgets.h"
#include "editor_state.h"
namespace Vkm::Engine {

CardInfo cardInfo(Tag<Water>) {
    const char* hint = "Makes its mesh water: a pond, a river, the sea";
    return {"Water", AddSection::Landscape, hint, EditorIcon::Water};
}

void drawCard(Rows<Water>& c, CardContext& card) {
    c.slider<&Water::waves>();
    c.slider<&Water::ripples>();
    c.row<&Water::flow>("%.2f m/s");
    c.row<&Water::depth>("%.1f m");
    c.slider<&Water::foam>();
    c.row<&Water::waterline>("%.2f m");
    if (!card.scene.has<Mesh>(card.id)) cardNote(Note::Danger, "Needs a Mesh: its surface is the water.");
}

CardInfo cardInfo(Tag<Terrain>) {
    return {"Terrain", AddSection::Landscape, nullptr, EditorIcon::Terrain};
}

void drawCard(Rows<Terrain>& c, CardContext& card) {
    c.row<&Terrain::terrain>();
    c.row<&Terrain::castShadows>();
    const TerrainAsset* terrain = card.resources.tryGet(c.value().terrain);
    if (!terrain) {
        cardNote(Note::Danger, "No terrain: this draws nothing.");
        return;
    }
    const uint32_t side = terrain->resolution;
    ImGui::TextDisabled("%u x %u vertices, %.0f m across", side, side, terrain->size);
    // Its brushes, layers and plants are the Terrain window's.
    if (ImGui::Button("Edit Terrain", ImVec2(-1.0f, 0.0f))) card.state.openTerrain(card.id);
}

} // namespace Vkm::Engine
