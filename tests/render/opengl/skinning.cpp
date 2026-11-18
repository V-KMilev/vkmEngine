// A skinned mesh, posed on the GPU by its rig's palette.

#include "ecs/component/animation/animator.h"
#include "resource/asset/skeleton_asset.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

TEST("a skinned mesh at rest draws as the same mesh unskinned") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Yard seen = yard(scene, r);

    // The cube bound wholly to the one bone of a rig at rest.
    SkeletonAsset rig;
    rig.bones       = {{"root", -1}};
    rig.inverseBind = {glm::mat4(1.0f)};
    rig.bindPose    = {BonePose{}};
    MeshAsset skinned = generateCube();
    skinned.skin.assign(skinned.vertices.size(), SkinVertex{{0, 0, 0, 0}, {255, 0, 0, 0}});
    skinned.skeleton = "test:rig";
    skinned.computeAndSetSkinRadius(rig);
    Animator posed;
    posed.skeleton = r.add(std::move(rig), "test:rig");
    const MeshHandle plain = scene.get<Mesh>(seen.red).mesh;
    scene.get<Mesh>(seen.red).mesh = r.add(std::move(skinned), "test:skinned");
    scene.add(seen.red, posed);

    const std::vector<Picture> frames = screen.shots({
        [&] { scene.get<Mesh>(seen.red).mesh = plain; },
    });

    REQUIRE(frames.size() == 2u);
    CHECK(lit(frames[0]) > 0.99f);
    CHECK(differing(frames[0], frames[1]) < 0.001f);
}
