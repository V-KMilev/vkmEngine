// The backend's parts a picture would not single out: its depth convention, how far its mip chains
// halve, and the mesh pool's free space.

#include <cstdint>

#include <glm/gtc/matrix_transform.hpp>

#include "asset/gl_mesh_pool.h"
#include "convention/gl_depth.h"
#include "convention/gl_mip_levels.h"
#include "resource/asset/mesh_asset.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"

using namespace Vkm::Test;

namespace {

/// The depth @p projection gives a point @p distance ahead of the eye.
float depthAhead(const glm::mat4& projection, float distance) {
    const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
    return clip.z / clip.w;
}

/// A mesh of @p count vertices, each index naming its own.
MeshAsset meshOf(uint32_t count) {
    MeshAsset mesh;
    mesh.vertices.resize(count);
    for (uint32_t i = 0; i < count; ++i) mesh.indices.push_back(i);
    return mesh;
}

} // namespace

TEST("the eye's depth is 1 at its near plane and 0 at its far, a shadow map's 0 and 1, by the one remap") {
    const glm::mat4 eye = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.05f, 4000.0f);
    CHECK_NEAR(depthAhead(GLDepth::eyeProjection(eye), 0.05f), GLDepth::EYE_NEAR, 1e-6f);
    CHECK_NEAR(depthAhead(GLDepth::eyeProjection(eye), 4000.0f), GLDepth::EYE_CLEAR, 1e-6f);

    const glm::mat4 sun = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.5f, 300.0f);
    CHECK_NEAR(depthAhead(GLDepth::shadowProjection(sun), 0.5f), 0.0f, 1e-6f);
    CHECK_NEAR(depthAhead(GLDepth::shadowProjection(sun), 300.0f), GLDepth::SHADOW_CLEAR, 1e-6f);

    // Clip z alone is remapped, from w and z by the shares given.
    const glm::mat4 remapped = GLDepth::remapClipZ(eye, 0.25, 0.75);
    for (int column = 0; column < 4; ++column) {
        CHECK_NEAR(remapped[column][2], 0.25f * eye[column][3] + 0.75f * eye[column][2], 1e-6f);
        for (const int row : {0, 1, 3}) CHECK(remapped[column][row] == eye[column][row]);
    }
}

TEST("a mip chain halves while its shorter edge is two texels or more, up to the levels asked") {
    struct Case {
        int width;
        int height;
        int most;
        int levels;
    };
    const Case cases[] = {
        {1920, 1080, 16, 10},  // 1080 halved 9 times is 2
        {1080, 1920, 16, 10},  // either edge may be the shorter
        {1920, 1080, 4, 4},    // the caller's limit
        {2, 2, 16, 1},         // a level of 1 is under two
        {3, 512, 16, 1},
        {4, 4, 16, 2},
        {1, 1, 16, 1},
        {0, 0, 16, 1},         // never no level
    };
    for (const Case& c : cases) CHECK(mipLevelsDownToTwo(c.width, c.height, c.most) == c.levels);
}

TEST("the mesh pool puts a mesh in the first free space it fits, and space given back joins its neighbors'") {
    const Device device;
    GLMeshPool pool;
    const MeshRange a = pool.add(meshOf(30));
    const MeshRange b = pool.add(meshOf(60));
    const MeshRange c = pool.add(meshOf(30));
    CHECK(c.firstVertex == 90u);
    CHECK(c.firstIndex == 90u);

    // Where b was, ahead of the space after c.
    pool.remove(b);
    const MeshRange small = pool.add(meshOf(30));
    CHECK(small.firstVertex == b.firstVertex);
    CHECK(small.firstIndex == b.firstIndex);

    // a's, small's and the rest of b's are one run again, from the start.
    pool.remove(a);
    pool.remove(small);
    const MeshRange joined = pool.add(meshOf(90));
    CHECK(joined.firstVertex == 0u);
    CHECK(joined.firstIndex == 0u);
}
