#include "panels/inspector/cards.h"

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>
#include <imgui.h>

#include "mesh/lod_generator.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/outline.h"
#include "actions/asset_actions.h"
#include "undo/component_commands.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"
namespace Vkm::Engine {

namespace {

// Touching clip planes are degenerate: glm::perspective divides by (zFar - zNear) and the
// cluster pass takes log(zFar / zNear).
constexpr float CLIP_PLANE_SEPARATION = 0.001f;

// A level added to an empty LOD holds to this many meters; one added after, this many times as far
// as the last, and never nearer than the first.
constexpr float FIRST_LOD_RANGE = 10.0f;
constexpr float LOD_RANGE_STEP  = 2.0f;

} // namespace

CardInfo cardInfo(Tag<Mesh>) {
    return {"Mesh", AddSection::Render, "Draws a mesh in a material", EditorIcon::Mesh};
}

void drawCard(Rows<Mesh>& c, CardContext& card) {
    const Mesh& mesh = c.value();
    c.row<&Mesh::mesh>();
    if (const MeshAsset* asset = card.resources.tryGet(mesh.mesh)) {
        const bool boxed     = asset->bounds().valid();
        const glm::vec3 size = boxed ? asset->boundsMax - asset->boundsMin : glm::vec3(0.0f);
        const size_t tris    = asset->indices.size() / 3;
        ImGui::TextDisabled("%zu tris, %.1f x %.1f x %.1f m", tris, size.x, size.y, size.z);
        // The rig resolved, not the name the mesh remembers.
        if (!asset->skin.empty()) {
            const SkeletonHandle rig = card.resources.findByName<SkeletonAsset>(asset->skeleton);
            if (rig) ImGui::TextDisabled("Skinned: %zu bones", card.resources.get(rig).bones.size());
            else     cardNote(Note::Warning, "Its rig '%s' is not loaded.", asset->skeleton.c_str());
        }
    } else {
        // VisibilitySystem skips an empty mesh or material handle.
        cardNote(Note::Danger, "No mesh: this entity draws nothing.");
    }

    c.row<&Mesh::material>();
    if (mesh.material) {
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Edit Material", ImVec2(half, 0.0f))) card.state.openMaterial(mesh.material);
        ImGui::SameLine();
        const bool fork = ImGui::Button("Duplicate", ImVec2(half, 0.0f));
        ImGui::SetItemTooltip("Fork the material and edit the copy safely");
        // After the card, which pushes its own step for what else changed.
        const EntityId id = card.id;
        if (fork) {
            card.after = [&card, id] {
                if (!card.scene.has<Mesh>(id)) return;
                const MaterialHandle was = card.scene.get<Mesh>(id).material;
                EditorActions::forkMaterial(card.scene, card.resources, card.state, id, was);
            };
        }
    } else {
        cardNote(Note::Danger, "No material: this entity draws nothing.");
    }

    c.row<&Mesh::visible>();
    c.row<&Mesh::castShadows>();
    c.row<&Mesh::seenBy>();
    c.row<&Mesh::movable>();
}

CardInfo cardInfo(Tag<LOD>) {
    return {"LOD", AddSection::Render, "Simpler meshes for when it is far off", EditorIcon::Levels};
}

void drawCard(Rows<LOD>& c, CardContext& card) {
    LOD& lod = c.value();
    c.slider<&LOD::bias>();

    // Each level its mesh and how far it holds, near to far; a level's mesh may be any.
    int  removed = -1;
    bool ordered = true;
    c.each<&LOD::levels>([&](Rows<LODLevel>& level, size_t i) {
        const MeshAsset* mesh = card.resources.tryGet(level.value().mesh);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Level %zu  -  %zu tris", i, mesh ? mesh->indices.size() / 3 : 0);
        if (removeButton("remove", "Remove this level")) removed = static_cast<int>(i);
        level.row<&LODLevel::mesh>();
        level.row<&LODLevel::maxDistance>("%.1f m");
        if (i > 0 && level.value().maxDistance <= lod.levels[i - 1].maxDistance) ordered = false;
    });
    if (removed >= 0) {
        lod.levels.erase(lod.levels.begin() + removed);
        c.edited();
    }
    if (!ordered) cardNote(Note::Warning, "Levels run near to far: each further than the last.");
    const Mesh* drawn = card.scene.tryGet<Mesh>(card.id);
    if (ImGui::Button("+  Add Level", ImVec2(-1.0f, 0.0f))) {
        // Past the last, on its mesh, or the entity's own for the first.
        LODLevel    next;
        const bool  first   = lod.levels.empty();
        const float further = first ? 0.0f : lod.levels.back().maxDistance * LOD_RANGE_STEP;
        next.mesh        = first ? (drawn ? drawn->mesh : MeshHandle{}) : lod.levels.back().mesh;
        next.maxDistance = std::max(further, FIRST_LOD_RANGE);
        lod.levels.push_back(next);
        c.edited();
    }

    ImGui::Spacing();
    if (!drawn || !drawn->mesh) {
        cardNote(Note::Info, "Add a Mesh to generate levels from.");
        return;
    }
    propSliderInt("Generate", &card.memory.lodLevels, 1, 4, "How many coarser levels to build below it");
    if (ImGui::Button("Generate Levels", ImVec2(-1.0f, 0.0f))) {
        lod = generateLOD(card.resources, drawn->mesh, static_cast<uint32_t>(card.memory.lodLevels));
        c.edited();
    }
}

CardInfo cardInfo(Tag<Camera>) {
    return {"Camera", AddSection::Render, "A view the game can draw through", EditorIcon::Camera};
}

void drawCard(Rows<Camera>& c, CardContext& card) {
    Camera& cam = c.value();
    c.row<&Camera::projection>();
    if (cam.projection == ProjectionType::Perspective) {
        c.angle<&Camera::fovY>("%.0f deg");
        c.angle<&Camera::viewmodelFovY>("%.0f deg");
    } else {
        c.row<&Camera::orthoHeight>("%.1f m");
    }

    // A positive aspect pins the ratio; 0 follows the viewport.
    bool autoAspect = cam.aspect <= 0.0f;
    if (propCheckbox("Auto Aspect", &autoAspect, "Follow the viewport's width over height")) {
        c.custom<&Camera::aspect>() = autoAspect ? 0.0f : 16.0f / 9.0f;
        c.edited();
    }
    if (!autoAspect) c.row<&Camera::aspect>("%.3f");

    if (c.row<&Camera::zNear>("%.3f")) cam.zNear = std::min(cam.zNear, cam.zFar - CLIP_PLANE_SEPARATION);
    if (c.row<&Camera::zFar>("%.0f"))  cam.zFar  = std::max(cam.zFar, cam.zNear + CLIP_PLANE_SEPARATION);
    c.row<&Camera::active>();

    const EntityId eye = card.gameEye;
    if (cam.active && eye && eye != card.id && card.scene.has<Camera>(eye)) {
        char eyeName[64];
        getEntityDisplayName(card.scene, eye, eyeName, sizeof(eyeName));
        cardNote(Note::Warning, "Not the game's eye: it renders from '%s'.", eyeName);
    }
    if (ImGui::Button("Set as Main Camera", ImVec2(-1.0f, 0.0f))) {
        pushActiveCamera(card.scene, card.resources, card.state, card.id);
    }
}

CardInfo cardInfo(Tag<Outline>) {
    const char* hint = "A rim of color around this mesh, to read it against the scene";
    return {"Outline", AddSection::Render, hint, EditorIcon::Focus};
}

void drawCard(Rows<Outline>& c, CardContext& card) {
    c.row<&Outline::color>();
    c.slider<&Outline::width>("%.1f px");
    c.row<&Outline::throughWalls>();
    c.row<&Outline::enabled>();
    // The renderer outlines the rim of the Mesh on this entity alone.
    if (!card.scene.has<Mesh>(card.id)) cardNote(Note::Danger, "No Mesh here: this outlines nothing.");
}

} // namespace Vkm::Engine
