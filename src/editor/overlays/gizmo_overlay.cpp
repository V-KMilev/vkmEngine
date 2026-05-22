#include "overlays/gizmo_overlay.h"

#include <algorithm>
#include <memory>

#include <glm/gtc/quaternion.hpp>

#include "framework/editor_common.h"
#include "framework/editor_commands.h"
#include "system/visibility/visibility.h"
#include "system/visibility/bounds_utils.h"
#include "system/camera/camera_controller.h"
#include "resource/resource_manager.h"
#include "ecs/component/world_transform.h"
#include "core/math/rotation.h"

namespace Engine {

void GizmoOverlay::drawTransformGizmo(EditorContext& ec) {
    FrameContext& ctx     = ec.frame;
    EditorState&  state   = ec.state;
    ImVec2 vpMin          = ec.viewportPos;
    float  vpWidth        = ec.viewportSize.x;
    float  vpHeight       = ec.viewportSize.y;

    if (state.gizmoOperation == GizmoOperation::Select) return;  // pick-only, no handles
    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) return;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;
    if (!ctx.scene.has<Transform>(state.selectedEntity)) return;

    // The camera you fly *is* the viewport eye: a transform gizmo on it would
    // fight the fly controller (both write its Transform every frame). It can
    // still be selected - Camera params stay editable in the Inspector.
    if (state.selectedEntity == ec.cameraController.getCameraEntity().getID())
        return;

    // The 3D pass now renders into viewport-sized FBOs and composite
    // blits to the viewport sub-rect of the backbuffer (gl_composite_pass).
    // So the visibility projection (built with the viewport's aspect)
    // matches the rendered image 1:1 - no remap matrix needed.
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

    // Capture start rotation when drag begins. Also snapshot the FULL
    // transform so on drag-end we can push one TransformChangeCommand
    // covering the whole motion (instead of one per frame).
    if (m_gizmo.isUsing() && !m_dragActive) {
        m_dragStartRot       = transform.rotation;
        m_dragStartTransform = transform;
        m_dragEntity         = state.selectedEntity;
        m_dragActive         = true;
    }
    // Drag-end: only push if the transform actually changed during the drag
    // (no-op clicks on the gizmo don't deserve an undo entry).
    if (!m_gizmo.isUsing() && m_dragActive) {
        if (m_dragEntity && ctx.scene.isAlive(m_dragEntity)
                && ctx.scene.has<Transform>(m_dragEntity)) {
            const Transform& after = ctx.scene.get<Transform>(m_dragEntity);
            const Transform& bef   = m_dragStartTransform;
            const bool changed = bef.position != after.position
                              || bef.rotation != after.rotation
                              || bef.scale    != after.scale;
            if (changed) {
                state.commands.push(std::make_unique<TransformChangeCommand>(
                    m_dragEntity, bef, after, "Transform"));
            }
        }
        m_dragActive = false;
        m_dragEntity = {};
    }
    if (!m_gizmo.isUsing()) {
        m_dragActive = false;
    }

    if (m_gizmo.manipulate(drawList, ctx.visibility->view, subProj,
                            state.gizmoOperation, state.gizmoMode, model,
                            vpMin, vpWidth, vpHeight)) {

        if (state.gizmoOperation == GizmoOperation::Rotate) {
            // For rotation: apply delta quaternion directly to start rotation
            // This completely bypasses matrix decomposition and avoids quaternion flips
            glm::quat deltaRot = m_gizmo.getDragRotation();

            if (hasParent) {
                // Convert world-space rotation delta to local space
                glm::quat parentRot = glm::quat_cast(glm::mat3(parentWorld));
                glm::quat invParentRot = glm::inverse(parentRot);
                deltaRot = invParentRot * deltaRot * parentRot;
            }

            transform.rotation = glm::normalize(deltaRot * m_dragStartRot);
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
                if (state.gizmoOperation == GizmoOperation::Translate) {
                    float s = state.snapTranslate;
                    pos.x = snapValue(pos.x, s);
                    pos.y = snapValue(pos.y, s);
                    pos.z = snapValue(pos.z, s);
                } else if (state.gizmoOperation == GizmoOperation::Scale) {
                    float s = state.snapScale;
                    scale.x = snapValue(scale.x, s);
                    scale.y = snapValue(scale.y, s);
                    scale.z = snapValue(scale.z, s);
                }
            }

            transform.position = pos;
            transform.scale    = scale;
        }

        // Local transform changed - mark this entity's hierarchy subtree dirty
        // so HierarchySystem recomputes WorldTransforms next frame.
        HierarchyOperations::markDirty(ctx.scene, state.selectedEntity);
        state.markSceneDirty();
    }
}

namespace {
    // Project a world point through the viewport's view+projection into
    // screen coordinates inside the viewport child rect. Returns false when
    // the point is behind the camera. The 3D pass renders at viewport size
    // and the viewport child sits at vpMin onscreen, so NDC maps to
    // (vpMin + (0..vpSize)) directly.
    bool projectToViewport(const glm::mat4& vp, const glm::vec3& p,
                           ImVec2 vpMin, ImVec2 vpSize, ImVec2& out) {
        const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
        if (clip.w <= 1e-5f) return false;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        out = ImVec2(vpMin.x + (ndc.x * 0.5f + 0.5f) * vpSize.x,
                     vpMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * vpSize.y);
        return true;
    }

    glm::vec3 lightWorldPos(const Scene& scene, EntityId id, const Transform& tf) {
        if (scene.has<WorldTransform>(id))
            return glm::vec3(scene.get<WorldTransform>(id).model[3]);
        return tf.position;
    }
}

void GizmoOverlay::drawLightGizmos(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    const glm::mat4 vp = ctx.visibility->projection * ctx.visibility->view;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ec.viewportPos,
        ImVec2(ec.viewportPos.x + ec.viewportSize.x,
               ec.viewportPos.y + ec.viewportSize.y), true);

    ctx.scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& tf) {
        if (!light.enabled) return;
        const bool selected = (ec.state.selectedEntity == id);
        const ImU32 col = selected
            ? EditorStyle::HIGHLIGHT_U32
            : IM_COL32(static_cast<int>(light.color.r * 220),
                       static_cast<int>(light.color.g * 220),
                       static_cast<int>(light.color.b * 220), 200);

        const glm::vec3 pos = lightWorldPos(ctx.scene, id, tf);
        const glm::vec3 dir = glm::normalize(Math::computeForward(tf.rotation));

        // Billboard icon at the entity origin so a light is always findable
        // even if the wireframe is tiny or pointed away. Drawn first so the
        // wireframe overlays it; for the user the dot reads as "the light
        // lives here".
        {
            ImVec2 sp;
            if (projectToViewport(vp, pos, ec.viewportPos, ec.viewportSize, sp)) {
                const float r = 8.0f;
                // Dim disc behind the glyph so the icon reads on any background.
                dl->AddCircleFilled(sp, r + 1.0f, IM_COL32(15, 15, 18, 180), 16);
                const EditorIcon glyph =
                    light.type == LightType::Directional ? EditorIcon::LightDir :
                    light.type == LightType::Point       ? EditorIcon::LightPoint :
                                                            EditorIcon::LightSpot;
                drawEditorIcon(dl, glyph, sp, r * 0.85f, col);
            }
        }

        switch (light.type) {
            case LightType::Directional: {
                // Sun gizmo: a small disc at the light origin plus three
                // parallel rays in the forward direction. The triangular
                // offset reads as "parallel rays" instead of a single arrow
                // (which always looked more like a spotlight).
                const float L      = 1.5f;          // ray length (world units)
                const float spread = 0.18f;         // lateral offset of side rays
                const float discR  = 0.10f;         // sun disc radius

                // Build an orthonormal basis in the plane perpendicular to dir
                // so the three rays are coplanar with that plane.
                const glm::vec3 up = std::abs(dir.y) < 0.99f
                    ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                const glm::vec3 right = glm::normalize(glm::cross(dir, up));
                const glm::vec3 udir  = glm::normalize(glm::cross(right, dir));

                const glm::vec3 offsets[3] = {
                    glm::vec3(0.0f),
                    right *  spread + udir *  spread * 0.5f,
                    right * -spread + udir *  spread * 0.5f,
                };

                // Disc outline (perpendicular to dir) so the user can see the
                // light origin distinctly from the rays.
                {
                    const int N = 16;
                    ImVec2 prev{};
                    bool havePrev = false;
                    for (int s = 0; s <= N; ++s) {
                        const float t = (static_cast<float>(s) / N) * 6.2831853f;
                        const glm::vec3 p = pos + (right * std::cos(t) + udir * std::sin(t)) * discR;
                        ImVec2 sp;
                        if (projectToViewport(vp, p, ec.viewportPos, ec.viewportSize, sp)) {
                            if (havePrev) dl->AddLine(prev, sp, col, 1.5f);
                            prev = sp; havePrev = true;
                        } else havePrev = false;
                    }
                }

                // Three parallel arrows from the disc plane forward.
                for (int i = 0; i < 3; ++i) {
                    const glm::vec3 start = pos + offsets[i];
                    const glm::vec3 end   = start + dir * L;
                    ImVec2 a, b;
                    if (!projectToViewport(vp, start, ec.viewportPos, ec.viewportSize, a)) continue;
                    if (!projectToViewport(vp, end,   ec.viewportPos, ec.viewportSize, b)) continue;
                    dl->AddLine(a, b, col, 2.0f);

                    ImVec2 dv(b.x - a.x, b.y - a.y);
                    const float len = std::sqrt(dv.x*dv.x + dv.y*dv.y);
                    if (len > 1.0f) {
                        dv.x /= len; dv.y /= len;
                        const ImVec2 perp(-dv.y, dv.x);
                        const float h = 6.0f;
                        dl->AddTriangleFilled(b,
                            ImVec2(b.x - dv.x * h * 2 + perp.x * h, b.y - dv.y * h * 2 + perp.y * h),
                            ImVec2(b.x - dv.x * h * 2 - perp.x * h, b.y - dv.y * h * 2 - perp.y * h), col);
                    }
                }
                break;
            }
            case LightType::Point: {
                // Three orthogonal great-circle wireframes at radius.
                const int N = 32;
                const float r = std::max(0.05f, light.radius);
                const glm::vec3 axes[3][2] = {
                    {{1,0,0},{0,1,0}}, {{1,0,0},{0,0,1}}, {{0,1,0},{0,0,1}}
                };
                for (int ring = 0; ring < 3; ++ring) {
                    ImVec2 prev{};
                    bool havePrev = false;
                    for (int s = 0; s <= N; ++s) {
                        const float t = (static_cast<float>(s) / N) * 6.2831853f;
                        const glm::vec3 p = pos + (axes[ring][0] * std::cos(t) + axes[ring][1] * std::sin(t)) * r;
                        ImVec2 sp;
                        if (projectToViewport(vp, p, ec.viewportPos, ec.viewportSize, sp)) {
                            if (havePrev) dl->AddLine(prev, sp, col, 1.0f);
                            prev = sp;
                            havePrev = true;
                        } else {
                            havePrev = false;
                        }
                    }
                }
                break;
            }
            case LightType::Spot: {
                // Cone: apex at pos, base circle of half-angle outerCone at radius.
                const float r = std::max(0.05f, light.radius);
                const float half = light.outerConeAngle;
                const float baseR = std::tan(half) * r;
                const glm::vec3 baseC = pos + dir * r;

                // Basis perpendicular to dir.
                glm::vec3 tangent = std::abs(dir.y) < 0.99f
                    ? glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)))
                    : glm::normalize(glm::cross(dir, glm::vec3(1, 0, 0)));
                glm::vec3 bitangent = glm::cross(dir, tangent);

                ImVec2 apexSp;
                bool haveApex = projectToViewport(vp, pos, ec.viewportPos, ec.viewportSize, apexSp);

                // Base ring + 4 spokes from apex.
                const int N = 32;
                ImVec2 prev{};
                bool havePrev = false;
                for (int s = 0; s <= N; ++s) {
                    const float t = (static_cast<float>(s) / N) * 6.2831853f;
                    const glm::vec3 p = baseC + (tangent * std::cos(t) + bitangent * std::sin(t)) * baseR;
                    ImVec2 sp;
                    if (projectToViewport(vp, p, ec.viewportPos, ec.viewportSize, sp)) {
                        if (havePrev) dl->AddLine(prev, sp, col, 1.0f);
                        prev = sp;
                        havePrev = true;
                        if (haveApex && (s % 8) == 0) dl->AddLine(apexSp, sp, col, 1.0f);
                    } else {
                        havePrev = false;
                    }
                }
                break;
            }
        }
    });

    dl->PopClipRect();
}

void GizmoOverlay::drawCameraGizmos(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    if (!ctx.visibility || !ctx.visibility->hasCamera) return;

    const glm::mat4 vp = ctx.visibility->projection * ctx.visibility->view;
    const EntityId activeCamId = ec.cameraController.getCameraEntity().getID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ec.viewportPos,
        ImVec2(ec.viewportPos.x + ec.viewportSize.x,
               ec.viewportPos.y + ec.viewportSize.y), true);

    ctx.scene.forEach<Camera, Transform>([&](EntityId id, const Camera& cam, const Transform& tf) {
        // The active editor camera *is* the viewer - drawing a frustum
        // there would put a gizmo inside the user's eye. Skip it.
        if (id == activeCamId) return;

        const bool selected = (ec.state.selectedEntity == id);
        const ImU32 col = selected ? EditorStyle::HIGHLIGHT_U32 : IM_COL32(120, 200, 220, 220);

        const glm::vec3 pos = (ctx.scene.has<WorldTransform>(id))
            ? glm::vec3(ctx.scene.get<WorldTransform>(id).model[3])
            : tf.position;
        const glm::vec3 fwd   = glm::normalize(Math::computeForward(tf.rotation));
        const glm::vec3 right = glm::normalize(glm::cross(fwd,
            std::abs(fwd.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
        const glm::vec3 up    = glm::normalize(glm::cross(right, fwd));

        const float dist   = 1.5f;
        const float aspect = ec.viewportSize.y > 1.0f
            ? ec.viewportSize.x / ec.viewportSize.y : 16.0f / 9.0f;

        // Frustum half-extents at the chosen depth.
        float halfH, halfW;
        if (cam.projection == ProjectionType::Perspective) {
            halfH = std::tan(cam.fovY * 0.5f) * dist;
            halfW = halfH * aspect;
        } else {
            halfH = cam.orthoHeight * 0.5f;
            halfW = halfH * aspect;
        }

        const glm::vec3 c = pos + fwd * dist;
        const glm::vec3 corners[4] = {
            c + right *  halfW + up *  halfH,  // top-right
            c + right * -halfW + up *  halfH,  // top-left
            c + right * -halfW + up * -halfH,  // bottom-left
            c + right *  halfW + up * -halfH,  // bottom-right
        };

        ImVec2 apexSp;
        bool haveApex = projectToViewport(vp, pos, ec.viewportPos, ec.viewportSize, apexSp);

        ImVec2 cornerSp[4]{};
        bool haveCorner[4] = {};
        for (int i = 0; i < 4; ++i) {
            haveCorner[i] = projectToViewport(vp, corners[i],
                ec.viewportPos, ec.viewportSize, cornerSp[i]);
        }

        // For ortho: also project the apex face (a rectangle at the camera
        // origin, same extents) so the gizmo reads as a box, not a pyramid.
        if (cam.projection == ProjectionType::Orthographic) {
            const glm::vec3 nearCorners[4] = {
                pos + right *  halfW + up *  halfH,
                pos + right * -halfW + up *  halfH,
                pos + right * -halfW + up * -halfH,
                pos + right *  halfW + up * -halfH,
            };
            ImVec2 nearSp[4]{};
            bool haveNear[4] = {};
            for (int i = 0; i < 4; ++i) {
                haveNear[i] = projectToViewport(vp, nearCorners[i],
                    ec.viewportPos, ec.viewportSize, nearSp[i]);
            }
            for (int i = 0; i < 4; ++i) {
                if (haveNear[i] && haveNear[(i + 1) % 4])
                    dl->AddLine(nearSp[i], nearSp[(i + 1) % 4], col, 1.0f);
                if (haveNear[i] && haveCorner[i])
                    dl->AddLine(nearSp[i], cornerSp[i], col, 1.0f);
            }
        } else if (haveApex) {
            // Perspective: spokes from apex to far corners.
            for (int i = 0; i < 4; ++i) {
                if (haveCorner[i]) dl->AddLine(apexSp, cornerSp[i], col, 1.5f);
            }
        }

        // Far-face rectangle (perspective & ortho share this).
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            if (haveCorner[i] && haveCorner[j])
                dl->AddLine(cornerSp[i], cornerSp[j], col, 1.0f);
        }

        // "Up" indicator: small triangular tab on the top edge so the
        // camera's roll is visible at a glance.
        if (haveCorner[0] && haveCorner[1]) {
            const ImVec2 mid((cornerSp[0].x + cornerSp[1].x) * 0.5f,
                             (cornerSp[0].y + cornerSp[1].y) * 0.5f);
            const ImVec2 tab(mid.x, mid.y - 8.0f);
            dl->AddTriangleFilled(tab,
                ImVec2(mid.x - 5.0f, mid.y),
                ImVec2(mid.x + 5.0f, mid.y), col);
        }

        // Billboard camera icon at the position.
        if (haveApex) {
            const float r = 8.0f;
            dl->AddCircleFilled(apexSp, r + 1.0f, IM_COL32(15, 15, 18, 180), 16);
            drawEditorIcon(dl, EditorIcon::Camera, apexSp, r * 0.85f, col);
        }
    });

    dl->PopClipRect();
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

    // Convert to NDC [-1, 1] in viewport space.
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

    // Test against the culled visible set instead of the whole scene: the
    // visibility pass already filtered frustum/distance/size AND precomputed
    // each entity's world matrix, so picking gets it for free. Off-screen /
    // hidden meshes are not pickable - that matches what the user can see.
    EntityId hitEntity{};
    float nearestT = std::numeric_limits<float>::max();

    for (const VisibleEntity& v : ctx.visibility->entries) {
        if (!ctx.scene.has<Mesh>(v.id)) continue;
        const Mesh& mesh = ctx.scene.get<Mesh>(v.id);
        const auto& asset = ctx.resources.get(mesh.mesh);
        if (!hasValidBounds(asset.boundsMin, asset.boundsMax)) continue;

        glm::vec3 worldMin, worldMax;
        localToWorldAABB(v.model, asset.boundsMin, asset.boundsMax, worldMin, worldMax);

        float t;
        if (rayIntersectsAABB(rayOrigin, invDir, worldMin, worldMax, t) && t < nearestT) {
            nearestT = t;
            hitEntity = v.id;
        }
    }

    // Also test light entities (no mesh, just position proximity). Lights
    // aren't in the visibility set, but HierarchySystem already cached each
    // hierarchical entity's WorldTransform so we just read it.
    ctx.scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (ctx.scene.has<Mesh>(id)) return; // already tested above
        if (!light.enabled)          return; // unselectable when off, matches gizmo draw

        glm::vec3 pos = ctx.scene.has<WorldTransform>(id)
            ? glm::vec3(ctx.scene.get<WorldTransform>(id).model[3])
            : transform.position;

        // Pick AABB scales with the light's reach: big area lights stay easy
        // to hit, tiny point lights still need a near click. Directionals have
        // no radius so fall back to a fixed value.
        const float radius = (light.type == LightType::Directional)
            ? 0.5f
            : std::clamp(light.radius * 0.2f, 0.3f, 3.0f);
        const glm::vec3 lightMin = pos - glm::vec3(radius);
        const glm::vec3 lightMax = pos + glm::vec3(radius);

        float t;
        if (rayIntersectsAABB(rayOrigin, invDir, lightMin, lightMax, t) && t < nearestT) {
            nearestT = t;
            hitEntity = id;
        }
    });

    // Selection is editor UI state - it does not modify the scene. Setting
    // sceneDirty here used to prompt the user to save just for clicking.
    // hierarchyDirty is still raised so the Hierarchy panel re-highlights.
    if (hitEntity) {
        state.selectedEntity = hitEntity;
    } else {
        // Click on empty space deselects
        state.selectedEntity = {};
    }
    state.hierarchyDirty = true;
}

} // namespace Engine
