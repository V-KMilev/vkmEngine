#include "panels/inspector/cards.h"

#include <cmath>
#include <memory>
#include <utility>

#include <glm/glm.hpp>
#include <imgui.h>

#include "undo/entity_commands.h"
#include "undo/entity_snapshot.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/physics/buoyancy.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "editor_state.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/resource_manager.h"
#include "system/physics/authoring/collider_fit.h"
#include "ecs/component/physics/mesh_collider.h"
#include "system/physics/authoring/ragdoll_build.h"
#include "ui/editor_widgets.h"
#include "ui/physics_layer_widgets.h"

namespace Vkm::Engine {

namespace {

// Scale from the collider's entity down to the mesh node: an import's child carries its own
// unit fix-up. Local scales are multiplied, not decomposed, so mirroring keeps its sign
// (an import chain has no rotation to approximate).
glm::vec3 meshScaleRelativeTo(const Scene& scene, EntityId collider, EntityId meshNode) {
    glm::vec3 scale(1.0f);
    HierarchyOperations::forSelfAndAncestors(scene, meshNode, [&](EntityId at) {
        if (const Transform* local = scene.tryGet<Transform>(at)) scale *= local->scale;
        return at != collider;
    });
    return scale;
}

// The first capsule's radius, or 0: a character wears one, and a compound fit is all boxes.
float capsuleRadiusOf(const Scene& scene, EntityId id) {
    const Collider* collider = scene.tryGet<Collider>(id);
    if (!collider) return 0.0f;
    for (const ColliderPart& part : collider->parts) {
        if (part.shape == ColliderShape::Capsule) return part.radius;
    }
    return 0.0f;
}

/**
 * @brief The collider's part: its shape's fields, and the triangles @p col built.
 *
 * @param part The part's rows.
 * @param col  The collider holding it, for its built triangles.
 * @param card The context; the entity's Rigidbody and Terrain decide its warnings.
 */
void drawPartRows(Rows<ColliderPart>& part, const Collider& col, CardContext& card) {
    const ColliderPart& p = part.value();
    part.row<&ColliderPart::shape>();
    // A terrain's part lies where the terrain is drawn.
    if (p.shape != ColliderShape::Terrain) part.row<&ColliderPart::center>();
    switch (p.shape) {
        case ColliderShape::Capsule:
            part.row<&ColliderPart::radius>("%.3f m");
            part.row<&ColliderPart::halfHeight>("%.3f m");
            ImGui::TextDisabled("%.3f m tall along +Y", (p.halfHeight + p.radius) * 2.0f);
            break;
        case ColliderShape::Mesh: {
            part.row<&ColliderPart::mesh>();
            part.row<&ColliderPart::meshScale>();
            if (col.triangles.points.empty()) {
                cardNote(Note::Danger, "No triangles: it collides with nothing. Pick a mesh, or make one.");
                break;
            }
            const size_t triangles = col.triangles.points.size() / 3;
            ImGui::TextDisabled("%zu triangles, %zu tree nodes", triangles, col.triangles.nodes.size());
            // Two meshes never collide, so a moving one drops through level geometry.
            const Rigidbody* rb = card.scene.tryGet<Rigidbody>(card.id);
            if (rb && rb->motion == RigidbodyMotion::Dynamic) {
                cardNote(Note::Danger, "Meshes never collide with each other: it falls through mesh floors");
            }
            break;
        }
        case ColliderShape::Box:
            part.row<&ColliderPart::halfExtents>();
            break;
        case ColliderShape::Terrain:
            if (!card.scene.has<Terrain>(card.id)) {
                cardNote(Note::Danger, "No Terrain: this collides with nothing.");
                break;
            }
            ImGui::TextDisabled("%zu triangles from its Terrain", col.triangles.points.size() / 3);
            break;
        case ColliderShape::Count:
            break;
    }
}

/**
 * @brief Fit to Mesh and Make Mesh Collider, from the mesh here or below: an import puts the
 *        geometry under the physics entity.
 *
 * Detail 1 is the scaled bounds; higher a box compound with the entity's scale baked in, which the
 * solver ignores.
 */
void drawFitRows(Rows<Collider>& c, CardContext& card) {
    Collider& col = c.value();
    const EntityId meshNode = HierarchyOperations::findInSelfOrDescendants<Mesh>(card.scene, card.id);
    const Mesh* source = card.scene.tryGet<Mesh>(meshNode);
    if (!source || !source->mesh) return;
    const MeshAsset& asset = card.resources.get(source->mesh);
    if (!asset.bounds().valid()) return;

    ImGui::Spacing();
    if (const Name* from = meshNode != card.id ? card.scene.tryGet<Name>(meshNode) : nullptr) {
        ImGui::TextDisabled("Shape from '%s'", from->value);
    }
    const char* detailTip = "1 is one box; higher is a tighter compound of boxes, heavier";
    propSliderInt("Detail", &card.memory.colliderFitDetail, 1, COLLIDER_FIT_MAX_DETAIL, detailTip);
    const glm::vec3 scale = meshScaleRelativeTo(card.scene, card.id, meshNode);
    if (ImGui::Button("Fit to Mesh", ImVec2(-1.0f, 0.0f))) {
        col.parts = fitBoxesToMesh(asset, card.memory.colliderFitDetail, scale);
        c.edited();
    }
    // The mesh's own triangles, for static things; it replaces the parts, or both would collide.
    if (ImGui::Button("Make Mesh Collider", ImVec2(-1.0f, 0.0f))) {
        Collider built;
        built.parts.clear();
        // Swapped in only when it holds a whole triangle, or no collider would be left.
        const bool empty = addMeshCollider(built, source->mesh, card.resources, scale) == 0;
        card.memory.meshColliderEmpty = empty ? card.id : EntityId{};
        if (!empty) {
            built.isTrigger = col.isTrigger;
            built.enabled   = col.enabled;
            col             = std::move(built);
            c.edited();
        }
    }
    if (card.memory.meshColliderEmpty == card.id) {
        cardNote(Note::Danger, "That mesh has no whole triangle; the collider is unchanged.");
    }
}

} // namespace

CardInfo cardInfo(Tag<Rigidbody>) {
    const char* hint = "Moved by physics: falls, is pushed, collides";
    return {"Rigidbody", AddSection::Physics, hint, EditorIcon::Weight};
}

void drawCard(Rows<Rigidbody>& c, CardContext& card) {
    c.row<&Rigidbody::motion>();
    c.row<&Rigidbody::mass>("%.2f kg");
    c.row<&Rigidbody::gravityScale>();
    c.slider<&Rigidbody::restitution>();
    c.slider<&Rigidbody::friction>();
    c.slider<&Rigidbody::linearDamping>("%.3f");
    c.slider<&Rigidbody::angularDamping>("%.3f");
    c.row<&Rigidbody::linearVelocity>();
    c.row<&Rigidbody::angularVelocity>();
    c.row<&Rigidbody::freezeRotation>();
    c.row<&Rigidbody::canSleep>();
    // Named in Project Settings > Physics Layers.
    const std::vector<std::string>& layers = card.state.project.physicsLayers;
    c.rowWith<&Rigidbody::layer>([&](int& layer, const FieldInfo&) { return layerCombo(&layer, layers); });
    const auto maskCombo = [&](int& mask, const FieldInfo&) { return layerMaskCombo(&mask, layers); };
    c.rowWith<&Rigidbody::collidesWith>(maskCombo);

    // Only a dynamic body falls out of the world without a shape; others are merely inert.
    if (c.value().motion == RigidbodyMotion::Dynamic && !card.scene.has<Collider>(card.id)) {
        cardNote(Note::Danger, "No Collider: it falls through everything.");
    }
}

CardInfo cardInfo(Tag<Collider>) {
    return {"Collider", AddSection::Physics, "The shape physics sees", EditorIcon::Colliders};
}

void drawCard(Rows<Collider>& c, CardContext& card) {
    const Collider& col = c.value();
    c.row<&Collider::enabled>();
    if (col.parts.size() == 1) {
        c.each<&Collider::parts>([&](Rows<ColliderPart>& part, size_t) { drawPartRows(part, col, card); });
    } else {
        ImGui::TextDisabled("%zu parts, fitted to its mesh", col.parts.size());
    }
    drawFitRows(c, card);
    c.row<&Collider::isTrigger>();

    // PhysicsSystem reads a Collider through its own entity's Rigidbody: alone it neither blocks nor senses.
    if (card.scene.has<Rigidbody>(card.id)) return;
    if (col.isTrigger) {
        cardNote(Note::Danger, "No Rigidbody: it senses nothing. Add one, Static where it stays put.");
    } else {
        cardNote(Note::Danger, "No Rigidbody: nothing hits it.");
    }
}

CardInfo cardInfo(Tag<CharacterController>) {
    const char* hint = "Walks, climbs steps and jumps under a player's input";
    return {"Character Controller", AddSection::Physics, hint, EditorIcon::Character};
}

void drawCard(Rows<CharacterController>& c, CardContext& card) {
    const CharacterController& cc = c.value();
    c.row<&CharacterController::jumpSpeed>("%.2f m/s");
    c.row<&CharacterController::acceleration>("%.1f m/s2");
    c.slider<&CharacterController::airControl>();
    c.row<&CharacterController::maxSlopeAngle>("%.0f deg");
    c.row<&CharacterController::knockbackDrag>("%.1f m/s2");
    c.row<&CharacterController::climbSpeed>("%.2f m/s");
    c.row<&CharacterController::crouchHeight>("%.2f m");
    c.row<&CharacterController::stepHeight>("%.2f m");

    // An edge lower than this gives a walkable normal even without stepping up.
    if (const float radius = capsuleRadiusOf(card.scene, card.id); radius > 0.0f) {
        const float limit = glm::radians(glm::clamp(cc.maxSlopeAngle, 0.0f, 90.0f));
        ImGui::TextDisabled("Rolls over steps up to %.2f m unaided", radius * (1.0f - std::cos(limit)));
    }

    // Live state: moveInput is gameplay's, the rest the system's.
    ImGui::TextDisabled("%s%s", cc.grounded ? "Grounded" : "Airborne", cc.crouched ? ", crouched" : "");
    if (glm::dot(cc.groundVelocity, cc.groundVelocity) > 0.0f) {
        const glm::vec3 carry = cc.groundVelocity;
        ImGui::TextDisabled("Carried at %.2f, %.2f, %.2f m/s", carry.x, carry.y, carry.z);
    }
    if (cc.grounded) {
        const float slope = glm::degrees(std::acos(glm::clamp(cc.groundNormal.y, -1.0f, 1.0f)));
        ImGui::TextDisabled("Ground %.0f deg from flat", slope);
    }
    ImGui::TextDisabled("Move input %.2f, %.2f, %.2f", cc.moveInput.x, cc.moveInput.y, cc.moveInput.z);

    const Rigidbody* body = card.scene.tryGet<Rigidbody>(card.id);
    if (!body) {
        cardNote(Note::Danger, "No Rigidbody: nothing to drive.");
    } else if (!body->freezeRotation) {
        cardNote(Note::Danger, "Its Rigidbody's rotation is not frozen: contacts will topple it.");
    }
    if (!card.scene.has<Collider>(card.id)) cardNote(Note::Danger, "No Collider: it will never be grounded.");
}

CardInfo cardInfo(Tag<Buoyancy>) {
    return {"Buoyancy", AddSection::Physics, "Floats on water and is carried by a river", EditorIcon::Water};
}

void drawCard(Rows<Buoyancy>& c, CardContext& card) {
    c.row<&Buoyancy::buoyancy>("%.2f g");
    c.row<&Buoyancy::drag>("%.2f /s");
    c.row<&Buoyancy::righting>("%.1f /s");

    const Rigidbody* body = card.scene.tryGet<Rigidbody>(card.id);
    if (!body || body->motion != RigidbodyMotion::Dynamic) {
        cardNote(Note::Danger, "Needs a Dynamic Rigidbody: nothing here floats.");
    } else if (!card.scene.has<Collider>(card.id)) {
        cardNote(Note::Danger, "Needs a Collider: its box is what floats.");
    }
}

CardInfo cardInfo(Tag<Joint>) {
    return {"Joint", AddSection::Physics, "Ties this body to another: a hinge, a rope", EditorIcon::Joint};
}

void drawCard(Rows<Joint>& c, CardContext& card) {
    const Joint& joint = c.value();
    c.row<&Joint::type>();
    EntityId& connected = c.custom<&Joint::connected>();
    if (propEntity(card.scene, card.id, "Connected", connected, "The body it is tied to")) c.edited();
    c.row<&Joint::anchor>();
    c.row<&Joint::connectedAnchor>();
    if (joint.type == JointType::Distance) {
        c.row<&Joint::distance>("%.3f m");
        // The solver's measurement: shown, not edited.
        if (joint.distance < 0.0f && joint.resolvedDistance >= 0.0f) {
            ImGui::TextDisabled("Holding %.3f m, measured on the first tick.", joint.resolvedDistance);
        }
    }
    c.slider<&Joint::stiffness>();
    c.row<&Joint::holdTorque>("%.1f N m");
    c.row<&Joint::collideConnected>();

    if (!joint.connected) cardNote(Note::Danger, "No connected body: this holds nothing.");
    if (!card.scene.has<Rigidbody>(card.id)) cardNote(Note::Danger, "No Rigidbody: it is not simulated.");
}

CardInfo cardInfo(Tag<Ragdoll>) {
    return {"Ragdoll", AddSection::Physics, "Bodies on a rig's bones, to fall limp", EditorIcon::Ragdoll};
}

void drawCard(Rows<Ragdoll>& c, CardContext& card) {
    enum class Pending { None, Build, Clear };
    Pending pending = Pending::None;

    c.row<&Ragdoll::active>();
    // What the build recorded: shown, not edited.
    const float scale  = c.custom<&Ragdoll::rigScale>();
    const size_t bones = c.value().bones.size();
    ImGui::TextDisabled(
        "%zu simulated bones on layer bit %d, built at scale %.2f",
        bones,
        Ragdoll::BONE_LAYER,
        scale
    );
    // Before the rig lookup: clearing needs none, the bones being ordinary entities.
    if (ImGui::Button("Clear", ImVec2(-1.0f, 0.0f))) pending = Pending::Clear;
    ImGui::SetItemTooltip("Remove the ragdoll and its bones together");

    // Searched downward: an import puts the Animator below the physics entity.
    const EntityId  rigNode = HierarchyOperations::findInSelfOrDescendants<Animator>(card.scene, card.id);
    const Animator* rig     = card.scene.tryGet<Animator>(rigNode);
    if (!rig || !card.resources.tryGet(rig->skeleton)) {
        cardNote(Note::Danger, "No Animator with a skeleton here or below: nothing to build from.");
    } else {
        if (const Name* named = rigNode != card.id ? card.scene.tryGet<Name>(rigNode) : nullptr) {
            ImGui::TextDisabled("Rig from '%s'", named->value);
        }
        RagdollSettings& settings = card.memory.ragdollSettings;
        const char* thicknessTip  = "Limb radius as a share of its length: a rig never says how solid it is";
        propDrag("Thickness", &settings.thickness, 0.01f, 0.02f, 1.0f, "%.2f", thicknessTip);
        propDrag("Mass", &settings.mass, 1.0f, 0.1f, 1000.0f, "%.0f kg", "Shared out by limb volume");
        const char* muscleTip = "How much of its shape it keeps once limp: each joint's Hold Torque";
        propSlider("Muscle", &settings.muscle, 0.0f, 1.0f, "%.2f", muscleTip);
        if (ImGui::Button("Build", ImVec2(-1.0f, 0.0f))) pending = Pending::Build;
    }
    if (pending == Pending::None) return;

    // After the card: both rebuild the subtree, removing the component the card is holding, and
    // both record a before and after (editor.md, Undo / redo).
    const EntityId id = card.id;
    card.after = [&card, id, pending, rigNode] {
        Scene& scene = card.scene;
        SubtreeSnapshot before = SubtreeSnapshot::capture(scene, id);
        if (pending == Pending::Build) {
            const SkeletonAsset& skeleton = card.resources.get(scene.get<Animator>(rigNode).skeleton);
            buildRagdoll(scene, id, skeleton, card.memory.ragdollSettings);
        } else {
            clearRagdoll(scene, id);
        }
        SubtreeSnapshot after = SubtreeSnapshot::capture(scene, id);
        const char* label = pending == Pending::Build ? "Build ragdoll" : "Clear ragdoll";
        auto step = std::make_unique<SubtreeReplaceCommand>(std::move(before), std::move(after), label);
        card.state.pushStep(std::move(step));
    };
}

} // namespace Vkm::Engine
