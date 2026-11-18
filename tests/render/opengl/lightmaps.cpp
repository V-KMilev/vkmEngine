// The lightmaps' sets as the bakes hand them over: a set let go of outlives the frames in flight that
// still draw with it.

#include <GL/glew.h>

#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/texture/texture_2d_array.h"

#include "asset/gl_asset_mirror.h"
#include "storage/light/gl_lightmaps.h"
#include "core/frame/render_objects.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"

using namespace Vkm::Test;

TEST("a lightmap set let go of is kept its frames, though another is let go of within them") {
    const Device device;
    GLLightmaps maps;
    const GLAssetMirror mirror;
    const RenderObjects objects;

    // A bake shown, then another shown over it: the first is let go of.
    maps.beginBake(objects, mirror, {}, 1, {});
    const GLuint first = maps.bakingPages().getID();
    maps.finishBake();
    maps.beginBake(objects, mirror, {}, 1, {});
    maps.finishBake();

    // A third dropped at once, a frame on: the first still has its frames to run.
    maps.prepare(objects, mirror);
    maps.beginBake(objects, mirror, {}, 1, {});
    maps.dropBake();
    CHECK(glIsTexture(first) == GL_TRUE);

    for (uint32_t frame = 1; frame < GLLightmaps::RETIRE_FRAMES; ++frame) maps.prepare(objects, mirror);
    CHECK(glIsTexture(first) == GL_FALSE);
}
