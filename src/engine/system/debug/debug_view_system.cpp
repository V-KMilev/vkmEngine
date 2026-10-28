#include "system/debug/debug_view_system.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "debug/debug_draw.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/planar_reflector.h"
#include "ecs/component/render/reflection_probe.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/resource_manager.h"
#include "system/animation/pose_buffer.h"
#include "system/visibility/visibility.h"

namespace Vkm::Engine {

namespace {

/// Past this a volume draws its box alone, or its probes bury the view.
constexpr uint32_t MAX_DRAWN_PROBES = 4096;

/// The most of a mesh collider's triangles drawn; past it, every n-th.
constexpr uint32_t MAX_DRAWN_TRIANGLES = 2048;

/// How far up a cone's spray is drawn, metres: where its opening shows.
constexpr float CONE_REACH = 1.0f;

/// The widest a cone's opening is drawn at, so a near-flat spray stays in view.
const float CONE_WIDEST = glm::radians(80.0f);

/// The share of its color a camera's frustum draws its inner edges in - the near face and the
/// lines to the eye - so the far face and the edges out to it read as its outline.
constexpr float FRUSTUM_INFILL = 0.6f;

/// The share of an area light's color its range draws in.
constexpr float REACH_SHARE = 0.35f;

/// The share of its color a joint that is off - connected to nothing, or to itself - draws in.
constexpr float JOINT_OFF_SHARE = 0.35f;

/// One frame's drawing: who is in focus, the scene and the view.
struct View {
    const FrameContext& ctx;
    DebugDraw&          draw;
    const DebugFocus&   focus;
    EntityId            eye;  ///< The camera the frame is drawn through: no frustum or listener arrow

    /// Whether @p id draws its detail: everyone, or the focus alone while it asks.
    bool detailed(EntityId id) const { return !focus.detailForFocusOnly || focus.has(id); }

    /// @p color, or the highlight for the focus.
    DebugPalette::Color tint(EntityId id, DebugPalette::Color color) const {
        return focus.has(id) ? DebugPalette::HIGHLIGHT : color;
    }

    /// What @p id draws in, @p points wide, the focus wider by half a point.
    DebugStyle style(EntityId id, DebugPalette::Color color, DebugCategory category, float points) const {
        return DebugStyle(tint(id, color)).in(category).wide(focus.has(id) ? points + 0.5f : points);
    }
};

void drawLights(const View& view) {
    const Scene& scene = view.ctx.scene;
    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& tf) {
        if (!light.enabled) return;
        const DebugPalette::Color color = view.tint(id, DebugPalette::ofLight(light.color));
        const DebugStyle line  = DebugStyle(color).in(DebugCategory::Lights);
        const DebugStyle reach = DebugStyle(color.faded(REACH_SHARE)).in(DebugCategory::Lights).wide(1.0f);

        const glm::vec3 pos    = resolvedWorldPosition(scene, id, tf);
        const glm::quat rot    = resolvedWorldRotation(scene, id, tf);
        const glm::vec3 dir    = Math::computeForward(rot);
        const float     radius = std::max(0.05f, light.radius);
        DebugDraw&      draw   = view.draw;

        switch (light.type) {
            case LightType::Directional: {
                // A sun disc and three rays leaving it.
                constexpr float RAY_LENGTH = 1.5f;
                constexpr float SPREAD     = 0.18f;
                glm::vec3 right, up;
                Math::orthoBasis(dir, right, up);
                draw.circle(pos, right, up, 0.10f, DebugStyle(line).wide(1.5f));
                const glm::vec3 offsets[3] = {
                    glm::vec3(0.0f),
                    right * SPREAD + up * SPREAD * 0.5f,
                    right * -SPREAD + up * SPREAD * 0.5f,
                };
                for (const glm::vec3& offset : offsets) {
                    draw.arrow(pos + offset, pos + offset + dir * RAY_LENGTH, DebugStyle(line).wide(2.0f));
                }
                break;
            }
            case LightType::Point:
                if (view.detailed(id)) draw.sphere(pos, radius, DebugStyle(line).wide(1.0f));
                break;
            case LightType::Spot:
                if (!view.detailed(id)) {
                    draw.arrow(pos, pos + dir * 0.5f, line);
                    break;
                }
                draw.cone(pos, dir * radius, light.outerConeAngle, DebugStyle(line).wide(1.0f));
                break;
            case LightType::Rect:
            case LightType::Disk: {
                // The emitting face, as the backend packs it: local +X and +Y by the half-sizes.
                const bool      isRect = light.type == LightType::Rect;
                const float     halfU  = isRect ? light.areaWidth * 0.5f : light.areaRadius;
                const float     halfV  = isRect ? light.areaHeight * 0.5f : light.areaRadius;
                const glm::vec3 u      = Math::computeRight(rot) * halfU;
                const glm::vec3 v      = Math::computeUp(rot) * halfV;
                if (isRect) {
                    const glm::vec3 corners[4] = {pos - u - v, pos + u - v, pos + u + v, pos - u + v};
                    draw.polyline(corners, 4, line, true);
                } else {
                    draw.circle(pos, u, v, 1.0f, line);
                }
                // Toward the lit side, -direction, and back as well for a two-sided one.
                draw.arrow(pos, pos - dir * 0.5f, line);
                if (light.twoSided) draw.arrow(pos, pos + dir * 0.5f, line);
                // Past it the light gives nothing.
                if (view.detailed(id)) draw.sphere(pos, radius, reach);
                break;
            }
            case LightType::Count: break;
        }
    });
}

void drawProbes(const View& view) {
    const Scene&              scene = view.ctx.scene;
    const DebugPalette::Color probe = DebugPalette::of(DebugCategory::Probes);

    const auto drawProbe = [&](EntityId id, const ReflectionProbe& p, const Transform& tf) {
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        view.draw.box(pos, p.halfExtents, view.style(id, probe, DebugCategory::Probes, 1.5f));
    };
    scene.forEach<ReflectionProbe, Transform>(drawProbe);

    // A planar reflector's rectangle, and the way it faces.
    const auto drawReflector = [&](EntityId id, const PlanarReflector& p, const Transform& tf) {
        const glm::vec3  pos   = resolvedWorldPosition(scene, id, tf);
        const glm::quat  rot   = resolvedWorldRotation(scene, id, tf);
        const DebugStyle style = view.style(id, probe, DebugCategory::Probes, 1.5f);
        const glm::vec3  x     = rot * glm::vec3(p.halfExtents.x, 0.0f, 0.0f);
        const glm::vec3  z     = rot * glm::vec3(0.0f, 0.0f, p.halfExtents.y);
        const glm::vec3  corners[4] = {pos - x - z, pos + x - z, pos + x + z, pos - x + z};
        view.draw.polyline(corners, 4, style, true);
        const float facing = 0.25f * std::min(p.halfExtents.x, p.halfExtents.y);
        view.draw.line(pos, pos + rot * glm::vec3(0.0f, facing, 0.0f), style);
    };
    scene.forEach<PlanarReflector, Transform>(drawReflector);

    const auto drawVolume = [&](EntityId id, const IrradianceVolume& volume, const Transform& tf) {
        const glm::vec3  pos   = resolvedWorldPosition(scene, id, tf);
        const DebugStyle style = view.style(id, DebugPalette::IRRADIANCE_VOLUME, DebugCategory::Probes, 1.5f);
        view.draw.box(pos, volume.halfExtents, style);
        if (!view.detailed(id)) return;

        // Clamped: raw, 2048 x 2048 x 1024 is 2^32, which wraps this product to zero.
        const glm::uvec3 res = IrradianceVolume::clampedResolution(volume);
        if (res.x * res.y * res.z > MAX_DRAWN_PROBES) return;
        const glm::vec3  boxMin = pos - volume.halfExtents;
        const glm::vec3  size   = volume.halfExtents * 2.0f;
        const DebugStyle dot    = DebugStyle(style).wide(1.0f);
        for (uint32_t z = 0; z < res.z; ++z) {
            for (uint32_t y = 0; y < res.y; ++y) {
                for (uint32_t x = 0; x < res.x; ++x) {
                    // Texel centres: where the bake captures.
                    view.draw.point(boxMin + size * ((glm::vec3(x, y, z) + 0.5f) / glm::vec3(res)), dot);
                }
            }
        }
    };
    scene.forEach<IrradianceVolume, Transform>(drawVolume);
}

/// Where @p emitter births its particles, as ParticleSystem places them: turned with it, never scaled.
void drawEmitShape(
    DebugDraw& draw,
    const ParticleEmitter& emitter,
    const glm::vec3& pos,
    const glm::quat& rot,
    const DebugStyle& style
) {
    const float     r = emitter.radius;
    const glm::vec3 x = rot * Math::WORLD_AXIS_X;
    const glm::vec3 y = rot * Math::WORLD_AXIS_Y;
    const glm::vec3 z = rot * Math::WORLD_AXIS_Z;
    switch (emitter.shape) {
        case EmitShape::Sphere:
            draw.circle(pos, x, z, r, style);
            draw.circle(pos, x, y, r, style);
            draw.circle(pos, z, y, r, style);
            break;
        case EmitShape::Hemisphere:
            draw.circle(pos, x, z, r, style);
            draw.arc(pos, x, y, r, 0.0f, glm::pi<float>(), style);
            draw.arc(pos, z, y, r, 0.0f, glm::pi<float>(), style);
            break;
        case EmitShape::Disc:
            draw.circle(pos, x, z, r, style);
            break;
        case EmitShape::Cone: {
            // The disc it leaves from, and how wide the spray has opened a reach up.
            const glm::vec3 up  = pos + y * CONE_REACH;
            const float     top = r + CONE_REACH * std::tan(std::min(emitter.angle, CONE_WIDEST));
            draw.circle(pos, x, z, r, style);
            draw.circle(up, x, z, top, style);
            for (const glm::vec3& side : {x, -x, z, -z}) draw.line(pos + side * r, up + side * top, style);
            break;
        }
        case EmitShape::Box:
            draw.box(pos, emitter.halfExtents, rot, style);
            break;
        case EmitShape::Point:
        case EmitShape::Count:
            break;
    }
}

void drawEffects(const View& view) {
    const Scene& scene = view.ctx.scene;

    scene.forEach<Decal, Transform>([&](EntityId id, const Decal&, const Transform& tf) {
        // The Transform's scale is the projection box, a unit cube, as the decal pass projects it.
        const glm::vec3  pos   = resolvedWorldPosition(scene, id, tf);
        const glm::quat  rot   = resolvedWorldRotation(scene, id, tf);
        const glm::vec3  scale = resolvedWorldScale(scene, id, tf);
        const DebugStyle style = view.style(id, DebugPalette::DECAL, DebugCategory::Effects, 1.5f);
        view.draw.box(pos, scale * 0.5f, rot, style);
        // It projects along its forward.
        view.draw.line(pos, pos + Math::computeForward(rot) * (scale.z * 0.75f), style);
    });

    const DebugPalette::Color emitterColor = DebugPalette::of(DebugCategory::Effects);
    const auto drawEmitter = [&](EntityId id, const ParticleEmitter& emitter, const Transform& tf) {
        const glm::mat4  model = resolvedWorldMatrix(scene, id, tf);
        const glm::vec3  pos(model[3]);
        const DebugStyle style = view.style(id, emitterColor, DebugCategory::Effects, 1.5f);
        if (view.detailed(id)) drawEmitShape(view.draw, emitter, pos, Math::worldRotationOf(model), style);

        // The way a cone, a box or a hemisphere sends its particles: its +Y.
        const glm::vec3 up(model[1]);
        const bool aimed = emitter.shape == EmitShape::Cone || emitter.shape == EmitShape::Box
            || emitter.shape == EmitShape::Hemisphere;
        if (aimed && glm::length(up) > 1e-4f) view.draw.line(pos, pos + glm::normalize(up) * 0.75f, style);
    };
    scene.forEach<ParticleEmitter, Transform>(drawEmitter);
}

void drawAudio(const View& view) {
    const Scene&              scene  = view.ctx.scene;
    const DebugPalette::Color audio  = DebugPalette::of(DebugCategory::Audio);
    const DebugPalette::Color silent = DebugPalette::QUIET_AUDIO;
    const DebugStyle          inner  = DebugStyle(audio).in(DebugCategory::Audio).wide(1.0f);
    const DebugStyle          outer  = DebugStyle(silent).in(DebugCategory::Audio).wide(1.0f);

    // Joined on Transform, unlike AudioSystem: a poseless source plays at the origin, a place no one picked.
    scene.forEach<AudioSource, Transform>([&](EntityId id, const AudioSource& source, const Transform& tf) {
        if (!source.spatial || !view.detailed(id)) return;
        // Not clamped: a max at or under the min turns the falloff off, which the spheres show.
        const glm::vec3 pos = resolvedWorldPosition(scene, id, tf);
        view.draw.sphere(pos, std::max(0.05f, source.minDistance), inner);
        view.draw.sphere(pos, std::max(0.05f, source.maxDistance), outer);
    });

    const EntityId ear = findActiveListener(scene);
    scene.forEach<AudioListener, Transform>([&](EntityId id, const AudioListener&, const Transform& tf) {
        if (id == view.eye) return;
        // Full for the ear findActiveListener picks; the others hear nothing.
        const DebugPalette::Color color = id == ear ? audio : silent;
        const glm::vec3           pos   = resolvedWorldPosition(scene, id, tf);
        const glm::quat           rot   = resolvedWorldRotation(scene, id, tf);
        // Facing decides which speaker a source lands in.
        const DebugStyle style = view.style(id, color, DebugCategory::Audio, 1.5f);
        view.draw.arrow(pos, pos + Math::computeForward(rot) * 0.8f, style);
    });
}

void drawCameras(const View& view) {
    const Scene&              scene  = view.ctx.scene;
    const DebugPalette::Color camera = DebugPalette::of(DebugCategory::Cameras);
    const glm::mat4& shown = view.ctx.visibility->camera.projection;
    // The view's own shape stands in for the camera's: its aspect is the one it would draw at here.
    const float aspect = shown[0][0] != 0.0f ? shown[1][1] / shown[0][0] : 16.0f / 9.0f;

    scene.forEach<Camera, Transform>([&](EntityId id, const Camera& cam, const Transform& tf) {
        if (id == view.eye) return;
        const DebugStyle outline = view.style(id, camera, DebugCategory::Cameras, 1.0f);
        const DebugStyle dim     = DebugStyle(view.tint(id, camera).faded(FRUSTUM_INFILL))
            .in(DebugCategory::Cameras)
            .wide(1.0f);

        Transform pose = tf;
        pose.position  = resolvedWorldPosition(scene, id, tf);
        pose.rotation  = resolvedWorldRotation(scene, id, tf);
        const glm::mat4 lens    = Camera::computeProjection(cam, aspect);
        const glm::mat4 toWorld = glm::inverse(lens * Transform::computeView(pose));
        const auto corner = [&](float x, float y, float z) {
            const glm::vec4 world = toWorld * glm::vec4(x, y, z, 1.0f);
            return glm::vec3(world) / world.w;
        };
        // Top right, top left, bottom left, bottom right.
        glm::vec3 nearFace[4];
        glm::vec3 farFace[4];
        const glm::vec2 corners[4] = {{1.0f, 1.0f}, {-1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f}};
        for (int i = 0; i < 4; ++i) {
            nearFace[i] = corner(corners[i].x, corners[i].y, -1.0f);
            farFace[i]  = corner(corners[i].x, corners[i].y, 1.0f);
        }

        DebugDraw& draw = view.draw;
        if (cam.projection == ProjectionType::Perspective) {
            for (const glm::vec3& c : nearFace) draw.line(pose.position, c, dim);
        }
        draw.polyline(nearFace, 4, dim, true);
        for (int i = 0; i < 4; ++i) draw.line(nearFace[i], farFace[i], outline);
        draw.polyline(farFace, 4, outline, true);

        // A tab over the far face's top edge shows which way is up: the camera's roll.
        const glm::vec3 top    = (farFace[0] + farFace[1]) * 0.5f;
        const glm::vec3 across = (farFace[0] - farFace[1]) * 0.08f;
        const glm::vec3 up     = (farFace[1] - farFace[2]) * 0.12f;
        const glm::vec3 tab[3] = {top - across, top + up, top + across};
        draw.polyline(tab, 3, outline, true);
    });
}

void drawColliders(const View& view) {
    const Scene&              scene    = view.ctx.scene;
    const DebugPalette::Color collider = DebugPalette::of(DebugCategory::Colliders);

    // As physics places them: world position and rotation, no scale.
    scene.forEach<Collider, Transform>([&](EntityId id, const Collider& col, const Transform& tf) {
        if (!col.enabled) return;  // an inert collider collides with nothing
        const glm::vec3  pos   = resolvedWorldPosition(scene, id, tf);
        const glm::quat  rot   = resolvedWorldRotation(scene, id, tf);
        const glm::mat3  turn  = glm::mat3_cast(rot);
        const DebugStyle style = view.style(id, collider, DebugCategory::Colliders, 1.5f);
        for (const ColliderPart& part : col.parts) {
            const glm::vec3 centre = pos + turn * part.center;
            switch (part.shape) {
                case ColliderShape::Box:
                    view.draw.box(centre, part.halfExtents, rot, style);
                    break;
                case ColliderShape::Capsule:
                    view.draw.capsule(centre, rot, part.radius, part.halfHeight, style);
                    break;
                case ColliderShape::Mesh: {
                    const uint32_t   triangles = static_cast<uint32_t>(col.meshPoints.size() / 3);
                    const uint32_t   step      = triangles / MAX_DRAWN_TRIANGLES + 1;
                    const DebugStyle fine      = DebugStyle(style).wide(1.0f);
                    for (uint32_t t = 0; t < triangles; t += step) {
                        const glm::vec3 corners[3] = {
                            centre + turn * col.meshPoints[t * 3 + 0],
                            centre + turn * col.meshPoints[t * 3 + 1],
                            centre + turn * col.meshPoints[t * 3 + 2],
                        };
                        view.draw.polyline(corners, 3, fine, true);
                    }
                    break;
                }
                // The terrain it is shows where it collides.
                case ColliderShape::Terrain:
                case ColliderShape::Count:
                    break;
            }
        }
    });

    scene.forEach<Joint, Transform>([&](EntityId id, const Joint& joint, const Transform& tf) {
        const bool focused = view.focus.has(id) || (joint.connected && view.focus.has(joint.connected));
        // Connected to nothing, or to itself, the joint is off: its anchor faint, and no rope.
        const Transform* other = joint.connected != id ? scene.tryGet<Transform>(joint.connected) : nullptr;
        const DebugPalette::Color color = focused ? DebugPalette::HIGHLIGHT : DebugPalette::JOINT;
        const DebugStyle style = DebugStyle(other ? color : color.faded(JOINT_OFF_SHARE))
            .in(DebugCategory::Colliders);

        // As the solver reads them: each in its body's frame, position and rotation only. One with no
        // Rigidbody is the fixed point in the world the body is held to.
        const glm::vec3 a = resolvedWorldPosition(scene, id, tf)
            + resolvedWorldRotation(scene, id, tf) * joint.anchor;
        view.draw.point(a, style);
        if (!other) return;
        const glm::vec3 b = resolvedWorldPosition(scene, joint.connected, *other)
            + resolvedWorldRotation(scene, joint.connected, *other) * joint.connectedAnchor;
        view.draw.line(a, b, style);
        view.draw.point(b, style);
    });
}

void drawSkeletons(const View& view, std::vector<glm::mat4>& world) {
    const PoseBuffer* poses = view.ctx.poses;
    if (!poses) return;
    const Scene&                  scene     = view.ctx.scene;
    const ResourceManager&        resources = view.ctx.resources;
    const std::vector<glm::mat4>& global    = poses->global();
    const DebugPalette::Color     rig       = DebugPalette::of(DebugCategory::Skeletons);

    scene.forEach<Animator, Transform>([&](EntityId id, const Animator& animator, const Transform& tf) {
        const PoseSlice* slice = poses->sliceOf(id);
        if (!slice || slice->count == 0) return;
        if (!animator.skeleton || !resources.isAlive(animator.skeleton)) return;
        const SkeletonAsset& skeleton = resources.get(animator.skeleton);
        if (skeleton.bones.size() != slice->count) return;

        // The pose is in the rig's model space; its world matrix places it, as for skinned vertices.
        const glm::mat4  model = resolvedWorldMatrix(scene, id, tf);
        const DebugStyle bone  = view.style(id, rig, DebugCategory::Skeletons, 1.5f);
        const DebugStyle joint = DebugStyle(bone).wide(1.25f);
        world.resize(slice->count);
        glm::vec3 lowest(std::numeric_limits<float>::max());
        glm::vec3 highest(std::numeric_limits<float>::lowest());
        for (uint32_t b = 0; b < slice->count; ++b) {
            world[b] = model * global[slice->first + b];
            const glm::vec3 origin(world[b][3]);
            lowest  = glm::min(lowest, origin);
            highest = glm::max(highest, origin);
            const int32_t parent = skeleton.bones[b].parent;
            if (parent >= 0) view.draw.line(glm::vec3(world[parent][3]), origin, bone);
            // A dot at each joint as well: a bone with no parent draws no segment.
            view.draw.point(origin, joint);
        }

        // Axes show facing, which composition or bind-inverse mistakes break.
        if (!view.detailed(id)) return;
        const glm::vec3  extent = highest - lowest;
        const float      length = std::max(0.05f * std::max({extent.x, extent.y, extent.z}), 1e-3f);
        const DebugStyle axes   = DebugStyle(bone).wide(1.5f);
        for (uint32_t b = 0; b < slice->count; ++b) view.draw.axes(world[b], length, axes);
    });
}

void drawBounds(const View& view) {
    const RenderObjects& objects = view.ctx.visibility->objects;
    const DebugStyle     style   = DebugStyle(DebugCategory::Bounds);
    for (const uint32_t object : objects.visible) {
        const Math::AABB& box = objects.bounds[object];
        if (box.min != box.max) view.draw.bounds(box.min, box.max, style);
    }
}

void drawSelection(const View& view) {
    if (view.focus.entities.empty()) return;
    const Visibility& visibility = *view.ctx.visibility;
    const DebugStyle  primary    = DebugStyle(DebugCategory::Selection);
    const DebugStyle  other      = DebugStyle(DebugPalette::SECONDARY).in(DebugCategory::Selection);
    for (const uint32_t object : visibility.objects.visible) {
        const Math::AABB& box = visibility.objects.bounds[object];
        const EntityId    id  = visibility.entities[object];
        if (box.min == box.max || !view.focus.has(id)) continue;
        view.draw.bounds(box.min, box.max, id == view.focus.primary ? primary : other);
    }
}

} // namespace

void DebugViewSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("DebugViewSystem");
    if (!ctx.debug || !ctx.visibility || !ctx.visibility->hasCamera) return;

    DebugDraw& draw = *ctx.debug;
    const View view{ctx, draw, draw.focus(), ctx.visibility->cameraEntity};
    if (draw.shown(DebugCategory::Lights))    drawLights(view);
    if (draw.shown(DebugCategory::Probes))    drawProbes(view);
    if (draw.shown(DebugCategory::Effects))   drawEffects(view);
    if (draw.shown(DebugCategory::Audio))     drawAudio(view);
    if (draw.shown(DebugCategory::Cameras))   drawCameras(view);
    if (draw.shown(DebugCategory::Colliders)) drawColliders(view);
    if (draw.shown(DebugCategory::Skeletons)) drawSkeletons(view, m_bones);
    if (draw.shown(DebugCategory::Bounds))    drawBounds(view);
    if (draw.shown(DebugCategory::Selection)) drawSelection(view);
}

} // namespace Vkm::Engine
