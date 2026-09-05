#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include <glm/gtc/quaternion.hpp>

#include "framework/component_edit.h"
#include "framework/editor_actions.h"
#include "framework/editor_common.h"
#include "framework/editor_commands.h"
#include "overlays/wire_draw.h"
#include "system/visibility/visibility.h"
#include "core/math/bounds.h"
#include "system/camera/camera_controller_system.h"
#include "resource/resource_manager.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/world_transform.h"

namespace Vkm::Engine {

void GizmoOverlay::finishDrag(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    auto changedOf = [&](EntityId id, const Transform& bef, const Transform*& after) {
        if (!id || !ctx.scene.isAlive(id) || !ctx.scene.has<Transform>(id)) return false;
        after = &ctx.scene.get<Transform>(id);
        return bef.position != after->position
            || bef.rotation != after->rotation
            || bef.scale    != after->scale;
    };

    // The step, not the push: the drag marks the scene dirty as it goes and a
    // multi-entity drag collects its steps into one CompositeCommand.
    auto stepFor = [&](EntityId id, const Transform& bef,
                       const Transform& after) -> std::unique_ptr<Command> {
        return editStep<Transform>(ctx.scene, ctx.resources, id, bef, after, "Transform");
    };

    if (!m_dragSelection.empty()) {
        // Every selection root, not just a multi-root drag: when the active
        // entity is a descendant of the only root, the root is the one thing
        // that moved and the else-branch below would find nothing to push.
        auto batch = std::make_unique<CompositeCommand>(
            m_dragSelection.size() > 1 ? "Transform Selection" : "Transform");
        for (const auto& [id, bef] : m_dragSelection) {
            const Transform* after = nullptr;
            if (changedOf(id, bef, after)) batch->add(stepFor(id, bef, *after));
        }
        if (!batch->empty()) state.commands.push(std::move(batch));
    } else {
        const Transform* after = nullptr;
        if (changedOf(m_dragEntity, m_dragStartTransform, after)) {
            state.commands.push(stepFor(m_dragEntity, m_dragStartTransform, *after));
        }
    }
    m_gizmo.endDrag();
    m_dragActive             = false;
    m_dragActiveIsDescendant = false;
    m_dragEntity             = {};
    m_dragSelection.clear();
}

void GizmoOverlay::drawTransformGizmo(EditorContext& ec) {
    FrameContext& ctx     = ec.frame;
    EditorState&  state   = ec.state;
    ImVec2 vpMin          = ec.viewportPos;
    float  vpWidth        = ec.viewportSize.x;
    float  vpHeight       = ec.viewportSize.y;

    // The flown camera is excluded because it is the viewport eye: a gizmo on it
    // would fight the fly controller, both writing its Transform every frame. It
    // is still selectable, and its Camera params still editable.
    const bool canManipulate =
           state.tool != EditorTool::Select                 // Select is pick-only, no handles
        && state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)
        && ctx.visibility && ctx.visibility->hasCamera
        && ctx.scene.has<Transform>(state.selectedEntity)
        && state.selectedEntity != ec.cameraController.getCameraEntity();

    // Before the draw guard, not after: a shortcut can switch tool or selection
    // mid-drag, and whether the drag is over is a different question from
    // whether the handles are drawn this frame.
    if (m_dragActive && (!canManipulate || !m_gizmo.isUsing())) finishDrag(ec);
    if (!canManipulate) return;

    // The visibility projection is built with the viewport's aspect and the 3D
    // pass renders into viewport-sized FBOs, so it matches the rendered image
    // 1:1 and needs no remap.
    const glm::mat4 subProj = ctx.visibility->projection;

    auto& transform = ctx.scene.get<Transform>(state.selectedEntity);

    // For parented entities, manipulate in world space and convert back to local
    bool hasParent = ctx.scene.has<Hierarchy>(state.selectedEntity)
                  && ctx.scene.get<Hierarchy>(state.selectedEntity).parent;

    glm::mat4 parentWorld = glm::mat4(1.0f);
    if (hasParent) {
        parentWorld = HierarchyOperations::computeWorldMatrix(ctx.scene, state.selectedEntity);
        glm::mat4 localModel = Transform::computeModelMatrix(transform);
        parentWorld = parentWorld * glm::inverse(localModel);
    }

    glm::mat4 model = hasParent
        ? parentWorld * Transform::computeModelMatrix(transform)
        : Transform::computeModelMatrix(transform);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Configure gizmo snap (rotation is handled inside the gizmo to avoid gimbal lock)
    bool snap = state.snapEnabled || ImGui::GetIO().KeyCtrl;
    m_gizmo.setSnapAngle(snap ? glm::radians(state.snapRotate) : 0.0f);

    // Snapshot the FULL transform when a drag begins so on drag-end we can
    // push one TransformChangeCommand covering the whole motion (instead of
    // one per frame). Rotation drags also re-use this as their start basis.
    if (m_gizmo.isUsing() && !m_dragActive) {
        m_dragStartTransform = transform;
        m_dragEntity         = state.selectedEntity;
        m_dragActive         = true;

        // Every selected transform, so the drag moves the whole set and drag-end
        // is one batch undo. Roots only: an entity whose ancestor is selected
        // already inherits that motion, and moving it again doubles the delta.
        m_dragSelection.clear();
        const EntityId flown = ec.cameraController.getCameraEntity();
        for (EntityId id : state.selection) {
            if (!ctx.scene.isAlive(id) || !ctx.scene.has<Transform>(id)) continue;
            if (id == flown) continue;
            if (EditorActions::hasSelectedAncestor(ctx.scene, state.selection, id)) continue;
            m_dragSelection.emplace_back(id, ctx.scene.get<Transform>(id));
        }
        m_dragActiveIsDescendant =
            !m_dragSelection.empty()
            && EditorActions::hasSelectedAncestor(ctx.scene, state.selection, state.selectedEntity);
    }
    if (m_gizmo.manipulate(drawList, ctx.visibility->view, subProj,
                            operationFor(state.tool), state.gizmoMode, model,
                            vpMin, vpWidth, vpHeight)) {

        if (state.tool == EditorTool::Rotate) {
            // For rotation: apply delta quaternion directly to start rotation
            // This completely bypasses matrix decomposition and avoids quaternion flips
            glm::quat deltaRot = m_gizmo.getDragRotation();

            if (hasParent) {
                // Convert world-space rotation delta to local space
                glm::quat parentRot = glm::quat_cast(glm::mat3(parentWorld));
                glm::quat invParentRot = glm::inverse(parentRot);
                deltaRot = invParentRot * deltaRot * parentRot;
            }

            transform.rotation = glm::normalize(deltaRot * m_dragStartTransform.rotation);
        } else {
            // For translate/scale: decompose is safe (no quaternion boundary issues)
            if (hasParent) {
                model = glm::inverse(parentWorld) * model;
            }

            glm::vec3 pos = glm::vec3(model[3]);
            glm::vec3 scale;
            scale.x = glm::length(glm::vec3(model[0]));
            scale.y = glm::length(glm::vec3(model[1]));
            scale.z = glm::length(glm::vec3(model[2]));

            if (snap) {
                auto snapValue = [](float v, float step) {
                    return std::round(v / step) * step;
                };
                if (state.tool == EditorTool::Translate) {
                    float s = state.snapTranslate;
                    pos.x = snapValue(pos.x, s);
                    pos.y = snapValue(pos.y, s);
                    pos.z = snapValue(pos.z, s);
                } else if (state.tool == EditorTool::Scale) {
                    float s = state.snapScale;
                    scale.x = snapValue(scale.x, s);
                    scale.y = snapValue(scale.y, s);
                    scale.z = snapValue(scale.z, s);
                }
            }

            transform.position = pos;
            transform.scale    = scale;
        }

        // Each from its own drag-start snapshot, never incrementally, so error
        // does not accumulate. Translation is a world delta taken into each
        // parent space; rotation applies in place; scale is a component ratio.
        if (!m_dragSelection.empty()) {
            const glm::vec3 worldDelta =
                glm::vec3(parentWorld * glm::vec4(transform.position, 1.0f))
              - glm::vec3(parentWorld * glm::vec4(m_dragStartTransform.position, 1.0f));
            const glm::quat worldRot = m_gizmo.getDragRotation();
            const glm::vec3 startScale = m_dragStartTransform.scale;
            const glm::vec3 ratio(
                startScale.x != 0.0f ? transform.scale.x / startScale.x : 1.0f,
                startScale.y != 0.0f ? transform.scale.y / startScale.y : 1.0f,
                startScale.z != 0.0f ? transform.scale.z / startScale.z : 1.0f);

            for (const auto& [id, start] : m_dragSelection) {
                if (id == state.selectedEntity) continue;
                if (!ctx.scene.isAlive(id) || !ctx.scene.has<Transform>(id)) continue;
                Transform& t = ctx.scene.get<Transform>(id);

                glm::mat4 pw(1.0f);
                if (ctx.scene.has<Hierarchy>(id) && ctx.scene.get<Hierarchy>(id).parent) {
                    pw = HierarchyOperations::computeWorldMatrix(ctx.scene, id)
                       * glm::inverse(Transform::computeModelMatrix(t));
                }

                if (state.tool == EditorTool::Translate) {
                    const glm::vec3 worldStart =
                        glm::vec3(pw * glm::vec4(start.position, 1.0f));
                    t.position = glm::vec3(glm::inverse(pw)
                                 * glm::vec4(worldStart + worldDelta, 1.0f));
                } else if (state.tool == EditorTool::Rotate) {
                    const glm::quat parentRot = glm::quat_cast(glm::mat3(pw));
                    const glm::quat localRot  =
                        glm::inverse(parentRot) * worldRot * parentRot;
                    t.rotation = glm::normalize(localRot * start.rotation);
                } else if (state.tool == EditorTool::Scale) {
                    t.scale = start.scale * ratio;
                }
            }

            // The gizmo wrote this entity directly, but a moving ancestor already
            // reaches it through the hierarchy. Put it back where the drag
            // started, so it is carried rather than carried and pushed.
            if (m_dragActiveIsDescendant) transform = m_dragStartTransform;
        }

        state.markSceneDirty();
    }
}

void GizmoOverlay::handleViewportPick(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    // Input via ImGui (same source as the rest of the editor): the editor
    // gates mouse capture per-frame and ImGui handles edge detection.
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (!state.viewportHovered) return;
    if (m_gizmo.isOver()) return;
    if (m_gizmo.isUsing()) return;

    // The 3D pass renders into the editor's viewport rect, so mouse -> NDC
    // is computed against the viewport (not the full window).
    const ImVec2 mp = ImGui::GetMousePos();
    const float vpX = ec.viewportPos.x;
    const float vpY = ec.viewportPos.y;
    const float vpW = std::max(1.0f, ec.viewportSize.x);
    const float vpH = std::max(1.0f, ec.viewportSize.y);

    float ndcX =  (2.0f * (mp.x - vpX) / vpW) - 1.0f;
    float ndcY = -(2.0f * (mp.y - vpY) / vpH) + 1.0f;

    // Unproject to world-space ray
    glm::mat4 invProj = glm::inverse(ctx.visibility->projection);
    glm::mat4 invView = glm::inverse(ctx.visibility->view);

    glm::vec4 clipRay(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 viewRay = invProj * clipRay;
    viewRay = glm::vec4(viewRay.x, viewRay.y, -1.0f, 0.0f);

    glm::vec3 worldDir = glm::normalize(glm::vec3(invView * viewRay));
    glm::vec3 rayOrigin = ctx.visibility->cameraPosition;
    glm::vec3 invDir(1.0f / worldDir.x, 1.0f / worldDir.y, 1.0f / worldDir.z);

    // Against the culled visible set, not the whole scene: the visibility pass
    // already filtered and precomputed each world matrix, so picking gets both
    // free - and what is off-screen is not pickable, which is what is meant.
    EntityId hitEntity{};
    float nearestT = std::numeric_limits<float>::max();

    for (const VisibleEntity& v : ctx.visibility->entries) {
        if (!ctx.scene.has<Mesh>(v.id)) continue;
        const Mesh& mesh = ctx.scene.get<Mesh>(v.id);
        if (!mesh.mesh || !ctx.resources.isAlive(mesh.mesh)) continue;
        const auto& asset = ctx.resources.get(mesh.mesh);
        if (!asset.bounds().valid()) continue;

        const Math::AABB world = Math::transform(v.model, asset.bounds());

        float t;
        if (Math::rayIntersectsAABB(rayOrigin, invDir, world, t) && t < nearestT) {
            nearestT = t;
            hitEntity = v.id;
        }
    }

    // A marker is a fixed pixel size whatever it marks, so the hit test is screen
    // space - projected, not derived from the fov, which keeps an orthographic
    // camera right. Anchor depth is what wins it against the wall behind it.
    const glm::mat4 viewProj = ctx.visibility->projection * ctx.visibility->view;
    const EntityId  flownCam = ec.cameraController.getCameraEntity();

    auto pickMarker = [&](EntityId id, const glm::vec3& pos) {
        ImVec2 sp;
        if (!projectToViewport(viewProj, pos, ImVec2(vpX, vpY), ImVec2(vpW, vpH), sp)) return;

        const float dx = mp.x - sp.x;
        const float dy = mp.y - sp.y;
        const float hitR = entityMarkerHitRadius();
        if (dx * dx + dy * dy > hitR * hitR) return;

        const float t = glm::distance(rayOrigin, pos);
        if (t < nearestT) {
            nearestT  = t;
            hitEntity = id;
        }
    };

    // Also test light entities (no mesh, just position proximity). Lights
    // aren't in the visibility set, but HierarchySystem already cached each
    // hierarchical entity's WorldTransform so we just read it.
    ctx.scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (ctx.scene.has<Mesh>(id)) return; // already tested above
        if (!light.enabled)          return; // unselectable when off, matches gizmo draw

        glm::vec3 pos = resolvedWorldPosition(ctx.scene, id, transform);
        pickMarker(id, pos);

        // Beside the marker, not instead of it: a light's gizmo is a volume the
        // user points at, so this box is a second target. It scales with the
        // light's reach; a directional has none, so it takes a fixed value.
        const float radius = (light.type == LightType::Directional)
            ? 0.5f
            : std::clamp(light.radius * 0.2f, 0.3f, 3.0f);
        const Math::AABB marker{pos - glm::vec3(radius), pos + glm::vec3(radius)};

        float t;
        if (Math::rayIntersectsAABB(rayOrigin, invDir, marker, t) && t < nearestT) {
            nearestT = t;
            hitEntity = id;
        }
    });

    ctx.scene.forEach<Camera, Transform>([&](EntityId id, const Camera&, const Transform& transform) {
        if (ctx.scene.has<Mesh>(id)) return; // already tested above
        // The flown editor camera draws no marker - it is the viewer, and a
        // marker there would sit inside the user's own eye.
        if (id == flownCam)          return;
        pickMarker(id, resolvedWorldPosition(ctx.scene, id, transform));
    });

    // A sound has no visual extent at all, so unlike a light there is no volume
    // to fall back on: the marker is the whole target, which is also what keeps
    // a source with a 60-unit reach from swallowing every click near it.
    ctx.scene.forEach<AudioSource, Transform>([&](EntityId id, const AudioSource&,
                                                  const Transform& transform) {
        if (ctx.scene.has<Mesh>(id)) return; // already tested above
        pickMarker(id, resolvedWorldPosition(ctx.scene, id, transform));
    });

    ctx.scene.forEach<AudioListener, Transform>([&](EntityId id, const AudioListener&,
                                                    const Transform& transform) {
        if (ctx.scene.has<Mesh>(id)) return; // already tested above
        if (id == flownCam)          return; // the ear riding the viewer's camera
        pickMarker(id, resolvedWorldPosition(ctx.scene, id, transform));
    });

    // Selection is editor UI state and does not modify the scene, so nothing here
    // sets sceneDirty: clicking an entity is not a change to save.
    const bool ctrl  = ImGui::GetIO().KeyCtrl;
    const bool shift = ImGui::GetIO().KeyShift;
    if (hitEntity) {
        if (ctrl)       state.toggleSelection(hitEntity);
        else if (shift) state.addToSelection(hitEntity);
        else            state.selectEntity(hitEntity);
    } else if (!ctrl && !shift) {
        // Click on empty space deselects (modified clicks leave the set alone)
        state.deselect();
    }
}

} // namespace Vkm::Engine
