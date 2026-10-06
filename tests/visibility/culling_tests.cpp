#include "support.h"

#include <utility>

#include "core/math/frustum.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "resource/generate/mesh_generators.h"
#include "system/render/render_view.h"
#include "system/visibility/culling.h"
#include "system/visibility/host_view.h"
#include "system/visibility/visibility.h"
#include "system/visibility/visibility_system.h"

namespace {

// A camera at the origin looking down -Z, the engine's forward.
glm::mat4 lookingForward(float fovY = glm::radians(60.0f), float zNear = 0.1f, float zFar = 100.0f) {
    const glm::mat4 projection = glm::perspective(fovY, 16.0f / 9.0f, zNear, zFar);
    const glm::mat4 view       = glm::lookAt(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    return projection * view;
}

glm::vec3 boxMin(const glm::vec3& at, float half) { return at - glm::vec3(half); }
glm::vec3 boxMax(const glm::vec3& at, float half) { return at + glm::vec3(half); }

bool visible(const Math::Frustum& f, const glm::vec3& at, float half = 0.5f) {
    return Math::frustumIntersectsAABB(f, Math::AABB{boxMin(at, half), boxMax(at, half)});
}

// A culling error is silent: it deletes geometry that was there, or draws geometry
// that was not.
void testTheFrustumKeepsWhatIsInFrontOfIt() {
    std::printf("What a camera can and cannot see:\n");

    const Math::Frustum f = Math::extractFrustum(lookingForward());

    check("a box straight ahead is visible", visible(f, {0.0f, 0.0f, -10.0f}));
    check("  and one behind the camera is not", !visible(f, {0.0f, 0.0f, 10.0f}));
    check("  nor one past the far plane", !visible(f, {0.0f, 0.0f, -200.0f}));
    check("  nor one nearer than the near plane", !visible(f, {0.0f, 0.0f, -0.01f}, 0.001f));

    // Forward is -Z. A +Z-forward frustum passes the first check by symmetry and fails
    // this one, which is why both directions are asserted.
    check("far to the left is outside", !visible(f, {-100.0f, 0.0f, -10.0f}));
    check("far to the right is outside", !visible(f, {100.0f, 0.0f, -10.0f}));
    check("far above is outside", !visible(f, {0.0f, 100.0f, -10.0f}));
    check("far below is outside", !visible(f, {0.0f, -100.0f, -10.0f}));
}

void testABoxOnTheEdgeCountsAsVisible() {
    std::printf("A box the frustum only partly contains:\n");

    const Math::Frustum f = Math::extractFrustum(lookingForward());

    // Straddling a plane is visible: a wall the camera stands inside must be drawn.
    check(
        "a box straddling the near plane is visible",
        Math::frustumIntersectsAABB(
            f,
            Math::AABB{glm::vec3(-1.0f, -1.0f, -0.5f), glm::vec3(1.0f, 1.0f, 0.5f)}
        )
    );

    // Huge and centred on the camera: every plane has the centre behind it, and only
    // the half-extent term keeps it in - what a test without the radius term gets wrong.
    check(
        "a box swallowing the camera is visible",
        Math::frustumIntersectsAABB(f, Math::AABB{glm::vec3(-500.0f), glm::vec3(500.0f)})
    );

    // A degenerate box is a point, and a point in front is in.
    check("a zero-size box in front is visible", visible(f, {0.0f, 0.0f, -5.0f}, 0.0f));
}

void testTheFrustumNarrowsWithTheFieldOfView() {
    std::printf("What the field of view actually changes:\n");

    const Math::Frustum wide   = Math::extractFrustum(lookingForward(glm::radians(100.0f)));
    const Math::Frustum narrow = Math::extractFrustum(lookingForward(glm::radians(20.0f)));

    // The same world; only the frustum changed.
    const glm::vec3 offToTheSide{5.0f, 0.0f, -6.0f};
    check("a wide field of view keeps a box off to the side", visible(wide, offToTheSide));
    check("  and a narrow one drops it", !visible(narrow, offToTheSide));
    check(
        "  while both keep what is straight ahead",
        visible(wide, {0.0f, 0.0f, -6.0f}) && visible(narrow, {0.0f, 0.0f, -6.0f})
    );
}

void testEveryPlaneNormalIsAUnitVector() {
    std::printf("The planes the extraction produced:\n");

    const Math::Frustum f = Math::extractFrustum(lookingForward());

    bool normalized = true;
    bool absMatches = true;
    for (int i = 0; i < 6; ++i) {
        normalized &= nearly(glm::length(f.normals[i]), 1.0f);
        absMatches &= nearly(f.absNormals[i].x, std::abs(f.normals[i].x))
            && nearly(f.absNormals[i].y, std::abs(f.normals[i].y))
            && nearly(f.absNormals[i].z, std::abs(f.normals[i].z));
    }
    // The AABB test scales the half-extent by absNormals against a world distance;
    // an unnormalized plane would scale the radius silently.
    check("every plane normal is unit length", normalized);
    check("  and absNormals is its component-wise absolute", absMatches);
}

// Shadow softness is the light's own: the sun's penumbra and highlight follow its
// Light, not the sky's disc, which an author sizes for looks. A disc past a quarter
// turn has no tangent to size anything by, so the frame holds it inside one.
void testALightsSourceIsItsOwn() {
    std::printf("What a light's source size reaches the frame as:\n");

    Scene scene;
    TestFrame frame(scene);
    const EntityId eye = scene.createEntity();
    scene.add(eye, Transform{});
    scene.add(eye, Camera{});

    const EntityId sun = scene.createEntity();
    scene.add(sun, Transform{});
    Light key;
    key.type         = LightType::Directional;
    key.sourceRadius = 0.05f;
    scene.add(sun, std::move(key));

    const EntityId lamp = scene.createEntity();
    scene.add(lamp, Transform{});
    Light spot;
    spot.type         = LightType::Spot;
    spot.sourceRadius = 0.2f;
    scene.add(lamp, std::move(spot));

    scene.environment().sky.sunAngularRadius = 0.09f;

    VisibilitySystem visibility;
    visibility.update(frame.ctx);
    RenderView view;
    view.build(scene, *frame.ctx.visibility, nullptr, nullptr, nullptr, nullptr);

    const LightData* sunData  = lowestSlotDirectional(view.lights);
    const LightData* lampData = nullptr;
    for (const LightData& light : view.lights) {
        if (light.type == LightType::Spot) lampData = &light;
    }
    check(
        "the sun's source is its light's, not the sky's drawn disc",
        sunData && nearly(sunData->sourceRadius, 0.05f)
    );
    check("  and a spot's is its own, in metres", lampData && nearly(lampData->sourceRadius, 0.2f));

    scene.get<Light>(sun).sourceRadius = 3.0f;
    view.build(scene, *frame.ctx.visibility, nullptr, nullptr, nullptr, nullptr);
    sunData = lowestSlotDirectional(view.lights);
    check(
        "a disc wider than a quarter turn is held to one",
        sunData && nearly(sunData->sourceRadius, glm::quarter_pi<float>())
    );
}

// A probe or irradiance volume looks every way, so it cannot capture only what the
// camera sees: a floor behind the camera that casts no shadow is in neither the
// drawables nor the casters, and a bake from those lists would miss it.
void testTheFrameCarriesWhatTheCameraCannotSee() {
    std::printf("What the frame carries beyond the camera's view:\n");

    Scene scene;
    TestFrame frame(scene);
    const MeshHandle cube = frame.resources.add(generateCube(), "culling:cube");
    MaterialAsset paintAsset;
    const MaterialHandle paint = frame.resources.add(std::move(paintAsset), "culling:paint");

    // At the origin, unrotated: looking down -Z.
    const EntityId eye = scene.createEntity();
    scene.add(eye, Transform{});
    scene.add(eye, Camera{});

    const EntityId behind = scene.createEntity();
    Transform at;
    at.position = {0.0f, 0.0f, 10.0f};
    scene.add(behind, std::move(at));
    Mesh floor;
    floor.mesh        = cube;
    floor.material    = paint;
    floor.castShadows = false;
    scene.add(behind, std::move(floor));

    // Created after the floor, so a gather keeping scene order would list it second.
    const EntityId pillar = scene.createEntity();
    Transform pillarAt;
    pillarAt.position = {0.0f, 0.0f, 20.0f};
    scene.add(pillar, std::move(pillarAt));
    Mesh column;
    column.mesh     = cube;
    column.material = paint;
    scene.add(pillar, std::move(column));

    VisibilitySystem visibility;
    visibility.update(frame.ctx);
    check("the camera resolved", frame.ctx.visibility && frame.ctx.visibility->hasCamera);

    RenderView view;
    view.build(scene, *frame.ctx.visibility, nullptr, nullptr, nullptr, nullptr);

    const RenderObjects& objects = *view.objects;
    check("a mesh behind the camera is not one the camera draws", objects.visible.empty());
    const bool carried = objects.scene.size() == 2
        && objects.draws[objects.scene[1]].mesh == cube
        && objects.draws[objects.scene[1]].material == paint;
    check("  but the frame carries it, with its material, for a capture", carried);
    check(
        "  after the caster, outside the prefix the shadow pass reads",
        objects.casterCount == 1 && objects.models[objects.scene[0]][3].z == 20.0f
    );
}

// The caster count is the prefix length the shadow pass indexes scene meshes by. A
// world with a camera and no Mesh storage returns early, and a stale count would
// name casters an empty list does not have.
void testAWorldWithNoMeshesHasNoCasters() {
    std::printf("What a world with no meshes casts:\n");

    VisibilitySystem visibility;

    Scene lit;
    TestFrame litFrame(lit);
    const MeshHandle cube = litFrame.resources.add(generateCube(), "culling:caster");
    MaterialAsset paintAsset;
    const MaterialHandle paint = litFrame.resources.add(std::move(paintAsset), "culling:caster-paint");
    const EntityId litEye = lit.createEntity();
    lit.add(litEye, Transform{});
    lit.add(litEye, Camera{});
    const EntityId caster = lit.createEntity();
    Transform ahead;
    ahead.position = {0.0f, 0.0f, -5.0f};
    lit.add(caster, std::move(ahead));
    Mesh body;
    body.mesh     = cube;
    body.material = paint;
    lit.add(caster, std::move(body));

    visibility.update(litFrame.ctx);
    check("a world with a caster counts it", litFrame.ctx.visibility->objects.casterCount == 1);

    Scene bare;
    TestFrame bareFrame(bare);
    const EntityId bareEye = bare.createEntity();
    bare.add(bareEye, Transform{});
    bare.add(bareEye, Camera{});

    visibility.update(bareFrame.ctx);
    const Visibility& result = *bareFrame.ctx.visibility;
    check("the next world, with none, has a camera", result.hasCamera);
    check(
        "  and counts no casters, not the last world's",
        result.objects.casterCount == 0 && result.objects.scene.empty()
    );

    RenderView view;
    view.build(bare, result, nullptr, nullptr, nullptr, nullptr);
    check("  so the frame hands the shadow pass none", view.objects->casterCount == 0);
}

// A Mesh with no material is never drawn, so its shadow would be of nothing on
// screen. The cull decides once, for drawables and casters alike.
void testAMeshWithNoMaterialCastsNothing() {
    std::printf("What a mesh with no material does:\n");

    Scene scene;
    TestFrame frame(scene);
    const MeshHandle cube = frame.resources.add(generateCube(), "culling:bare");

    const EntityId eye = scene.createEntity();
    scene.add(eye, Transform{});
    scene.add(eye, Camera{});

    const EntityId ahead = scene.createEntity();
    Transform at;
    at.position = {0.0f, 0.0f, -5.0f};
    scene.add(ahead, std::move(at));
    Mesh bare;
    bare.mesh        = cube;
    bare.castShadows = true;
    scene.add(ahead, std::move(bare));

    VisibilitySystem visibility;
    visibility.update(frame.ctx);

    RenderView view;
    view.build(scene, *frame.ctx.visibility, nullptr, nullptr, nullptr, nullptr);
    check("a mesh in view with no material is not drawn", view.objects->visible.empty());
    check("  and casts no shadow either", view.objects->casterCount == 0 && view.objects->scene.empty());
}

// The frame's light list is in SparseSet packing order, which destroying an unrelated
// light reorders; taking its first directional would disagree with findKeyLight's
// lowest slot.
void testTheKeyLightIsTheLowestSlotNotTheFirstListed() {
    std::printf("Which directional light the frame is keyed by:\n");

    std::vector<LightData> lights(3);
    lights[0].type = LightType::Directional;
    lights[0].entitySlot = 7;
    lights[1].type = LightType::Point;
    lights[1].entitySlot = 1;
    lights[2].type = LightType::Directional;
    lights[2].entitySlot = 4;

    const LightData* key = lowestSlotDirectional(lights);
    check("the lowest-slot directional, wherever it is listed", key && key->entitySlot == 4);

    lights[2].type = LightType::Spot;
    key = lowestSlotDirectional(lights);
    check("  and the only directional left when that one is not", key && key->entitySlot == 7);

    lights[0].type = LightType::Spot;
    check("  and none when there is no directional at all", lowestSlotDirectional(lights) == nullptr);
}

// The editor picks and drags along the ray under the pointer. Cast from the camera
// position it is right only for perspective: an orthographic view's rays are
// parallel and start at the pixel, so a pick from the eye fans out and misses.
void testThePickRayHoldsUnderEitherProjection() {
    std::printf("The world ray under a point of the image:\n");

    const glm::vec3 eye(0.0f, 0.0f, 10.0f);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    Camera perspective;
    const glm::mat4 perspectiveVP = Camera::computeProjection(perspective, 2.0f) * view;
    const Math::Ray fanned = Math::rayThroughNdc(glm::inverse(perspectiveVP), {0.5f, -0.25f});
    const glm::vec3 toEye = eye - fanned.origin;
    check(
        "a perspective ray runs back through the eye",
        glm::length(toEye - fanned.direction * glm::dot(toEye, fanned.direction)) < 1e-3f
    );
    check(
        "  and starts in front of it, on the near plane",
        nearly(eye.z - fanned.origin.z, perspective.zNear)
    );

    Camera ortho;
    ortho.projection  = ProjectionType::Orthographic;
    ortho.orthoHeight = 5.0f;
    const glm::mat4 orthoVP = Camera::computeProjection(ortho, 2.0f) * view;
    const Math::Ray parallel = Math::rayThroughNdc(glm::inverse(orthoVP), {0.5f, 0.5f});
    check(
        "an orthographic ray runs along the view axis",
        sameDirection(parallel.direction, {0.0f, 0.0f, -1.0f})
    );
    check(
        "  from under the point it was cast through, not from the eye",
        nearly(parallel.origin.x, 5.0f) && nearly(parallel.origin.y, 2.5f)
    );

    float t = 0.0f;
    const Math::AABB offCentre{{4.5f, 2.0f, -1.0f}, {5.5f, 3.0f, 1.0f}};
    const glm::vec3 invDir = 1.0f / parallel.direction;
    check(
        "  so it hits the box drawn at that point",
        Math::rayIntersectsAABB(parallel.origin, invDir, offCentre, t)
    );
}

// An authoring host offers its own view on the frame. What is drawn is read off the
// Visibility product, so the override must land there and lapse the frame nobody
// offers it, or the game's camera never gets the frame back.
void testAHostViewIsRenderedThroughWhileItIsOffered() {
    std::printf("What a host's view replaces, and when:\n");

    Scene scene;
    TestFrame frame(scene);
    const MeshHandle cube = frame.resources.add(generateCube(), "culling:host-view");
    MaterialAsset paintAsset;
    const MaterialHandle paint = frame.resources.add(std::move(paintAsset), "culling:host-paint");

    // The game looks down -Z at a cube; an inactive second camera looks the other way.
    const EntityId game = scene.createEntity();
    scene.add(game, Transform{});
    scene.add(game, Camera{});
    const EntityId spare = scene.createEntity();
    Transform behind;
    behind.position = {0.0f, 0.0f, -10.0f};
    behind.rotation = Math::lookRotation(glm::vec3(0.0f, 0.0f, 1.0f));
    scene.add(spare, std::move(behind));
    Camera off;
    off.active = false;
    scene.add(spare, off);
    const EntityId target = scene.createEntity();
    Transform ahead;
    ahead.position = {0.0f, 0.0f, -5.0f};
    scene.add(target, std::move(ahead));
    scene.add(target, Mesh{cube, paint});

    VisibilitySystem visibility;
    visibility.update(frame.ctx);
    check(
        "with no view offered the frame renders through the game's camera",
        frame.ctx.visibility->hasCamera
            && frame.ctx.visibility->cameraEntity == game
            && frame.ctx.visibility->objects.visible.size() == 1
    );

    // Beside the cube, facing away: nothing the game sees is in view.
    HostView host;
    host.position = {0.0f, 3.0f, 4.0f};
    host.rotation = Math::lookRotation(glm::vec3(0.0f, 0.0f, 1.0f));
    frame.ctx.hostView = &host;
    visibility.update(frame.ctx);
    Transform hostPose;
    hostPose.position = host.position;
    hostPose.rotation = host.rotation;
    const Visibility& seen = *frame.ctx.visibility;
    check(
        "a host's free view is what the frame renders from",
        seen.hasCamera
            && seen.camera.view == Transform::computeView(hostPose)
            && seen.camera.position == host.position
    );
    check("  and names no scene camera as the eye", !seen.cameraEntity);
    check("  and the cull is measured from it", seen.objects.visible.empty());

    host.through = spare;
    visibility.update(frame.ctx);
    check(
        "a host naming a scene camera renders through it, active or not",
        seen.cameraEntity == spare && seen.camera.position == glm::vec3(0.0f, 0.0f, -10.0f)
    );

    frame.ctx.hostView = nullptr;
    visibility.update(frame.ctx);
    check(
        "the frame nobody offers a view, the game's camera has it back",
        seen.cameraEntity == game && seen.camera.position == glm::vec3(0.0f)
    );
}

// The editor needs no scene camera, so a scene with none can still be looked at,
// while the game, which has only the scene's, renders nothing.
void testASceneWithNoCameraIsSeenThroughTheHost() {
    std::printf("A scene with no camera:\n");

    Scene scene;
    TestFrame frame(scene);
    VisibilitySystem visibility;

    visibility.update(frame.ctx);
    check("renders nothing on its own", !frame.ctx.visibility->hasCamera);

    HostView host;
    frame.ctx.hostView = &host;
    visibility.update(frame.ctx);
    check("  and is seen through a host's view when one is offered", frame.ctx.visibility->hasCamera);

    // A preview whose camera went away is not a view.
    host.through = scene.createEntity();
    visibility.update(frame.ctx);
    check(
        "  but not through a scene camera that is not there",
        !frame.ctx.visibility->hasCamera && !frame.ctx.visibility->cameraEntity
    );
}

// The camera in use is held while it lives, even when a lower-slot one turns active.
// A replaced world reuses slots, so the held id could name a camera nobody chose;
// the new world renders through its own lowest-slot camera.
void testAReplacedWorldIsSeenThroughItsOwnCamera() {
    std::printf("The camera a replaced world is seen through:\n");

    const auto twoCameras = [](Scene& world) {
        std::pair<EntityId, EntityId> made;
        for (EntityId* id : {&made.first, &made.second}) {
            *id = world.createEntity();
            world.add(*id, Transform{});
            world.add(*id, Camera{});
        }
        return made;
    };

    Scene scene;
    TestFrame frame(scene);
    VisibilitySystem visibility;
    const auto [first, second] = twoCameras(scene);

    scene.get<Camera>(first).active = false;
    visibility.update(frame.ctx);
    scene.get<Camera>(first).active = true;
    visibility.update(frame.ctx);
    check("the camera in use stays in use", frame.ctx.visibility->cameraEntity == second);

    Scene staging;
    const auto [lowest, sameIdAsHeld] = twoCameras(staging);
    scene.swap(staging);
    visibility.update(frame.ctx);
    check(
        "a replaced world is seen through its lowest-slot camera",
        sameIdAsHeld == second && frame.ctx.visibility->cameraEntity == lowest
    );
}

// A camera at the origin looking down -Z and one cube in front, to move about.
struct OneCube {
    Scene     scene;
    TestFrame frame{scene};
    EntityId  cube;
    MeshHandle full;
    MeshHandle middle;
    MeshHandle coarse;

    OneCube() {
        full   = frame.resources.add(generateCube(), "culling:lod-full");
        middle = frame.resources.add(generateCube(), "culling:lod-middle");
        coarse = frame.resources.add(generateCube(), "culling:lod-coarse");
        MaterialAsset paintAsset;
        const MaterialHandle paint = frame.resources.add(std::move(paintAsset), "culling:lod-paint");

        const EntityId eye = scene.createEntity();
        scene.add(eye, Transform{});
        scene.add(eye, Camera{});

        cube = scene.createEntity();
        scene.add(cube, Transform{});
        scene.add(cube, Mesh{full, paint});
    }

    // The mesh the cull drew with the cube @p distance ahead, or nothing.
    MeshHandle drawnAt(VisibilitySystem& visibility, float distance) {
        scene.get<Transform>(cube).position = {0.0f, 0.0f, -distance};
        visibility.update(frame.ctx);
        const RenderObjects& objects = frame.ctx.visibility->objects;
        if (objects.visible.empty()) return {};
        return objects.draws[objects.visible.front()].mesh;
    }
};

// A level is chosen by distance to the bounds' centre, scaled down by the bias; past
// the last threshold the coarsest keeps drawing - removal is the distance cull's call.
void testALevelIsChosenByDistance() {
    std::printf("Which level of detail draws at which distance:\n");

    OneCube world;
    LOD lod;
    lod.levels = {{world.middle, 10.0f}, {world.coarse, 50.0f}};
    world.scene.add(world.cube, lod);
    world.frame.render.cullMaxDistance = 0.0f;
    VisibilitySystem visibility;

    check("near, the first level", world.drawnAt(visibility, 5.0f) == world.middle);
    check("  further, the next", world.drawnAt(visibility, 30.0f) == world.coarse);
    check(
        "  and past the last threshold the coarsest still",
        world.drawnAt(visibility, 400.0f) == world.coarse
    );

    world.scene.get<LOD>(world.cube).bias = 2.0f;
    check(
        "a bias of two holds a level to twice its distance",
        world.drawnAt(visibility, 15.0f) == world.middle
    );

    world.scene.remove<LOD>(world.cube);
    check("with no LOD the mesh's own geometry draws", world.drawnAt(visibility, 30.0f) == world.full);
}

// Level ranges are set for a 60-degree view; a narrower one magnifies, so a zoomed
// camera holds the finer level further out.
void testANarrowerViewHoldsALevelFurtherOut() {
    std::printf("Which level of detail draws through a narrower view:\n");

    OneCube world;
    LOD lod;
    lod.levels = {{world.middle, 10.0f}, {world.coarse, 50.0f}};
    world.scene.add(world.cube, lod);
    world.frame.render.cullMaxDistance = 0.0f;
    VisibilitySystem visibility;

    const auto fov = [&](float degrees) {
        world.scene.forEach<Camera>([&](EntityId, Camera& camera) { camera.fovY = glm::radians(degrees); });
    };
    fov(60.0f);
    check("at the reference a level ends at its range", world.drawnAt(visibility, 11.0f) == world.coarse);
    fov(30.0f);
    check("  and through half of it holds past that", world.drawnAt(visibility, 15.0f) == world.middle);
}

// The distance cull removes what is past the project's furthest distance, by bounds'
// centre; zero switches it off.
void testWhatIsTooFarIsNotDrawn() {
    std::printf("What the distance cull removes:\n");

    OneCube world;
    world.frame.render.cullMaxDistance = 50.0f;
    VisibilitySystem visibility;

    check("a cube inside the distance draws", static_cast<bool>(world.drawnAt(visibility, 20.0f)));
    check("  and one past it does not", !world.drawnAt(visibility, 100.0f));

    world.frame.render.cullMaxDistance = 0.0f;
    check(
        "with the distance at zero nothing is too far",
        static_cast<bool>(world.drawnAt(visibility, 900.0f))
    );
}

// The screen-size cull compares a world radius against a threshold growing with depth.
// Under an orthographic projection a distant box is as large as a near one, so it
// must keep everything.
void testWhatIsTooSmallIsNotDrawn() {
    std::printf("What the screen-size cull removes:\n");

    const float     fovY       = glm::radians(60.0f);
    const glm::mat4 projection = glm::perspective(fovY, 16.0f / 9.0f, 0.1f, 1000.0f);
    const float     denom      = projection[1][1] * 1080.0f;  // a 1080-pixel-high view

    VisibilityContext context{};
    context.view                  = glm::lookAt(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    context.minPixels             = 3.0f;
    context.screenSizeThresholdSq = (3.0f * 3.0f) / (denom * denom);
    context.perspective           = true;

    const auto box = [](glm::vec3 at, float half) {
        return Math::AABB{at - glm::vec3(half), at + glm::vec3(half)};
    };

    check("a metre-wide box ten metres off is kept", Culling::isLargeEnough(box({0, 0, -10}, 0.5f), context));
    check(
        "  and a centimetre one a hundred metres off is not",
        !Culling::isLargeEnough(box({0, 0, -100}, 0.005f), context)
    );
    check(
        "  while one behind the eye is left for the frustum to judge",
        Culling::isLargeEnough(box({0, 0, 5}, 0.005f), context)
    );

    context.perspective = false;
    check(
        "under an orthographic view nothing is too small to draw",
        Culling::isLargeEnough(box({0, 0, -100}, 0.005f), context)
    );

    context.perspective = true;
    context.minPixels   = 0.0f;
    check(
        "  and with no minimum size nothing is",
        Culling::isLargeEnough(box({0, 0, -100}, 0.005f), context)
    );
}

} // namespace

void runCullingTests() {
    testALightsSourceIsItsOwn();
    testTheFrustumKeepsWhatIsInFrontOfIt();
    testABoxOnTheEdgeCountsAsVisible();
    testTheFrustumNarrowsWithTheFieldOfView();
    testEveryPlaneNormalIsAUnitVector();
    testTheFrameCarriesWhatTheCameraCannotSee();
    testAWorldWithNoMeshesHasNoCasters();
    testAMeshWithNoMaterialCastsNothing();
    testTheKeyLightIsTheLowestSlotNotTheFirstListed();
    testThePickRayHoldsUnderEitherProjection();
    testAHostViewIsRenderedThroughWhileItIsOffered();
    testASceneWithNoCameraIsSeenThroughTheHost();
    testAReplacedWorldIsSeenThroughItsOwnCamera();
    testALevelIsChosenByDistance();
    testANarrowerViewHoldsALevelFurtherOut();
    testWhatIsTooFarIsNotDrawn();
    testWhatIsTooSmallIsNotDrawn();
}
