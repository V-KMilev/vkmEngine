// The GPU-side suite. It needs a live OpenGL context, and GLFW needs a window system
// no build machine has, so Vkm::Test::GLContext opens the device through EGL; with no
// GPU at all this binary skips rather than fails.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "egl_context.h"
#include "golden_tests.h"

#include "gl_vertex_buffer.h"
#include "gl_vertex_array.h"
#include "gl_vertex_buffer_layout.h"
#include "gl_index_buffer.h"
#include "gl_shader_storage_buffer.h"
#include "gl_texture.h"
#include "gl_texture_3d.h"
#include "gl_shader.h"
#include "gl_compute_shader.h"
#include "gl_shader_preprocess.h"

#include "gl_context.h"
#include "gl_view.h"
#include "gl_backend.h"
#include "asset/gl_asset_texture.h"
#include "asset/gl_mesh.h"
#include "asset/gl_mesh_pool.h"
#include "frame/gl_draw_list.h"
#include "storage/gl_shadow_atlas.h"
#include "core/engine_config.h"
#include "frame/gl_lights.h"
#include "frame/gl_shadow_data.h"
#include "frame/gl_camera.h"
#include "offline/gl_ibl_baker.h"
#include "ecs/environment.h"
#include "asset/gl_material.h"
#include "storage/gl_cluster_grid.h"
#include "storage/gl_probe_manager.h"
#include "resource/generate/mesh_generators.h"
#include "resource/asset/font_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/resource_manager.h"
#include "system/render/data/particle_data.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_view.h"
#include "platform/window/window_manager.h"
#include "platform/threading/thread_pool.h"

namespace {

int g_failures = 0;

void check(const char* what, bool ok) {
    if (!ok) ++g_failures;
    std::printf("  %-62s %s\n", what, ok ? "ok" : "<-- FAILED");
}

// A GL error left by one test fails the next, so tests that care read and clear.
bool noGlError() { return glGetError() == GL_NO_ERROR; }

void testTheContextItself(const Vkm::Test::GLContext& gl) {
    std::printf("The context the tests run against:\n");
    check("it reports a version", !gl.version().empty());
    std::printf("      %s\n      %s\n", gl.version().c_str(), gl.renderer().c_str());

    // From the constants that own the number, never a literal, so the minimum version
    // is stated once.
    constexpr GLint WANT_MAJOR = Vkm::Engine::OPENGL_MAJOR_VERSION;
    constexpr GLint WANT_MINOR = Vkm::Engine::OPENGL_MINOR_VERSION;

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    std::printf("      driver %d.%d, engine targets %d.%d\n", major, minor, WANT_MAJOR, WANT_MINOR);
    check(
        "at or above the version the engine targets",
        major > WANT_MAJOR || (major == WANT_MAJOR && minor >= WANT_MINOR)
    );

    GLint mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    check("and it is a core profile context", (mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0);
    check("  with no GL error behind either query", noGlError());
}

void testAVertexBufferRoundTrips() {
    std::printf("A vertex buffer, written and read back:\n");

    const std::vector<float> vertices = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f,
    };
    const uint32_t bytes = static_cast<uint32_t>(vertices.size() * sizeof(float));

    Vkm::GL::VertexBuffer buffer(vertices.data(), bytes);
    check("it takes an id from the driver", buffer.getID() != 0);
    check("and reports the size it was given", buffer.getSize() == bytes);
    check("with no GL error behind it", noGlError());

    // The read-back says the bytes reached the GPU, which return codes cannot.
    std::vector<float> readBack(vertices.size(), 0.0f);
    buffer.bind();
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, bytes, readBack.data());
    buffer.unbind();
    check("and the data that comes back is the data that went up", readBack == vertices);
}

void testAnIndexBufferKnowsItsCount() {
    std::printf("An index buffer:\n");

    const std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
    Vkm::GL::IndexBuffer buffer(indices.data(), static_cast<uint32_t>(indices.size()));
    check("it takes an id", buffer.getID() != 0);
    check("and counts indices rather than bytes", buffer.getCount() == indices.size());
    check("with no GL error behind it", noGlError());
}

// A VAO refed with a new buffer (a ring buffer that outgrew itself) must rewire the same
// locations, since the shader still reads 0, 1, 2. A carried-over start index would
// wire 3, 4, 5 and leave the deleted buffer on the sampled three: no GL error, just
// geometry read from freed storage.
void testAVertexArrayRefedWithANewBufferRewiresTheSameLocations() {
    std::printf("A vertex array fed a second buffer in place of its first:\n");

    const std::vector<float> data(32, 0.0f);
    const uint32_t bytes = static_cast<uint32_t>(data.size() * sizeof(float));

    Vkm::GL::VertexBufferLayout layout;
    layout.push<float>(2);
    layout.push<float>(2);

    Vkm::GL::VertexArray vao;
    Vkm::GL::VertexBuffer first(data.data(), bytes);
    vao.addBuffer(first, layout);

    Vkm::GL::VertexBuffer second(data.data(), bytes);
    vao.addBuffer(second, layout);

    vao.bind();
    GLint boundAt0 = 0;
    GLint boundAt1 = 0;
    GLint enabledAt2 = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &boundAt0);
    glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &boundAt1);
    glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabledAt2);

    check(
        "location 0 now reads the buffer that replaced the first",
        boundAt0 == static_cast<GLint>(second.getID())
    );
    check("and so does location 1", boundAt1 == static_cast<GLint>(second.getID()));
    check("nothing was wired past the layout the caller described", enabledAt2 == GL_FALSE);
    check("with no GL error behind it", noGlError());
}

void testAShaderStorageBufferBindsToItsPoint() {
    std::printf("A shader storage buffer:\n");

    const std::vector<uint32_t> payload(64, 7u);
    const uint32_t bytes = static_cast<uint32_t>(payload.size() * sizeof(uint32_t));

    Vkm::GL::ShaderStorageBuffer ssbo(payload.data(), bytes);
    check("it takes an id", ssbo.getID() != 0);

    // Binding to an indexed point fails silently when the target is wrong.
    ssbo.bindBase(3);
    check("and binds to an indexed point without error", noGlError());

    GLint bound = 0;
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 3, &bound);
    check("  and the driver agrees it is bound there", bound == static_cast<GLint>(ssbo.getID()));
}

// The irradiance bake reads the SH grid back, repairs refused probes on the CPU
// (dilateProbeGrid) and writes it again. A volume returning zeroes would look exactly
// like an unlit scene.
void testAVolumeTextureRoundTrips() {
    std::printf("A 3D texture, written and read back:\n");

    Vkm::GL::Texture3DParams params;
    params.width  = 4;
    params.height = 3;
    params.depth  = 2;
    params.internalFormat = GL_RGBA16F;   // the irradiance volume's own format

    const Vkm::GL::Texture3D volume("test_volume", params);
    const size_t texels = static_cast<size_t>(params.width) * params.height * params.depth;

    // Values a half float holds exactly, compared for equality: a tolerance could hide a
    // swapped axis.
    std::vector<float> written(texels * 4, 0.0f);
    for (size_t i = 0; i < texels; ++i) {
        written[i * 4 + 0] = static_cast<float>(i);
        written[i * 4 + 1] = static_cast<float>(i) + 0.5f;
        written[i * 4 + 2] = -static_cast<float>(i);
        written[i * 4 + 3] = (i % 2 == 0) ? 1.0f : 0.0f;
    }
    volume.upload(written.data());
    check("it uploads without error", noGlError());

    std::vector<float> readBack(texels * 4, -1.0f);
    volume.download(readBack.data());
    check("and reads back without error", noGlError());
    check("with every texel the one that went up, in the order it went up in", readBack == written);
}

// A texture asked for while loading is built from the pending asset's defaults; its
// pixels arrive later with the file's real format and wrap. Filled in place, it would
// keep the stub's linear RGBA8 and clamp, and an sRGB albedo would draw washed out.
void testATextureThatArrivesLateTakesItsRealFormat() {
    std::printf("A texture drawn before its pixels arrived:\n");

    const Vkm::Engine::TextureAsset pending;
    Vkm::Engine::GLTexture texture(pending);

    Vkm::Engine::TextureAsset loaded;
    loaded.params.width          = 2;
    loaded.params.height         = 2;
    loaded.params.internalFormat = Vkm::Engine::TextureInternalFormat::SRGBA8;
    loaded.params.wrapS          = Vkm::Engine::TextureWrapMode::Repeat;
    loaded.params.wrapT          = Vkm::Engine::TextureWrapMode::Repeat;
    loaded.pixelData.assign(2 * 2 * 4, 128);
    texture.update(loaded);

    texture.getTexture().bindSlot(0);
    GLint format = 0;
    GLint wrap   = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap);
    check("it updates without error", noGlError());
    check("with the colour space its pixels came in", format == GL_SRGB8_ALPHA8);
    check("and the wrap the texture asked for", wrap == GL_REPEAT);
}

// GL can build a mip chain for neither cooked kind - blocks it cannot filter, texels
// the cooker filtered in linear light - so every carried level goes up as it came.
void testACookedTextureUploadsTheLevelsItCarries() {
    std::printf("A texture that arrives with its mip chain:\n");

    // 8x8 sRGB BC7: four blocks, then one each for 4x4, 2x2 and 1x1. A first byte of
    // 0x40 makes each a mode-6 block, though GL never looks.
    Vkm::Engine::TextureAsset blocks;
    blocks.params.width          = 8;
    blocks.params.height         = 8;
    blocks.params.internalFormat = Vkm::Engine::TextureInternalFormat::BC7SRGBA;
    blocks.params.mipLevels      = 4;
    blocks.pixelData.assign(4 * 16 + 3 * 16, 0);
    for (size_t block = 0; block < blocks.pixelData.size(); block += 16) blocks.pixelData[block] = 0x40;
    Vkm::Engine::GLTexture compressed(blocks);
    check("a compressed chain uploads without error", noGlError());

    compressed.getTexture().bindSlot(0);
    GLint format    = 0;
    GLint isBlocks  = 0;
    GLint levelSize = 0;
    GLint lastWidth = 0;
    GLint maxLevel  = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &isBlocks);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &levelSize);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 3, GL_TEXTURE_WIDTH, &lastWidth);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maxLevel);
    check("  as sRGB BC7", format == GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM && isBlocks == GL_TRUE);
    check("  level 0 holding its four blocks", levelSize == 4 * 16);
    check("  down to a 1x1 level, and no further", lastWidth == 1 && maxLevel == 3);

    // 4x4 sRGB texels with their chain, a distinct value per level, so a GL-built level
    // would not read back as the carried one.
    Vkm::Engine::TextureAsset texels;
    texels.params.width          = 4;
    texels.params.height         = 4;
    texels.params.internalFormat = Vkm::Engine::TextureInternalFormat::SRGBA8;
    texels.params.mipLevels      = 3;
    texels.pixelData.assign(4 * 4 * 4, 10);
    texels.pixelData.insert(texels.pixelData.end(), 2 * 2 * 4, 120);
    texels.pixelData.insert(texels.pixelData.end(), 1 * 1 * 4, 230);
    Vkm::Engine::GLTexture chain(texels);
    check("a chain of texels uploads without error", noGlError());

    chain.getTexture().bindSlot(0);
    std::vector<uint8_t> level1(2 * 2 * 4, 0);
    std::vector<uint8_t> level2(4, 0);
    glGetTexImage(GL_TEXTURE_2D, 1, GL_RGBA, GL_UNSIGNED_BYTE, level1.data());
    glGetTexImage(GL_TEXTURE_2D, 2, GL_RGBA, GL_UNSIGNED_BYTE, level2.data());
    check(
        "  every level reads back as it was carried",
        std::all_of(level1.begin(), level1.end(), [](uint8_t v) { return v == 120; })
            && std::all_of(level2.begin(), level2.end(), [](uint8_t v) { return v == 230; })
    );
    check("  with no GL error", noGlError());
}

// A grey map is stored in one channel, and shaders read roughness from green and
// metalness from blue; unswizzled those read zero, and a rough grey map is a mirror.
void testAOneChannelMapReadsAsGrey() {
    std::printf("A one-channel map sampled for its other channels:\n");

    const auto swizzleOf = [](const Vkm::Engine::GLTexture& texture) {
        texture.getTexture().bindSlot(0);
        GLint swizzle[4] = {};
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
        return std::array<GLint, 4>{swizzle[0], swizzle[1], swizzle[2], swizzle[3]};
    };
    const std::array<GLint, 4> grey     = {GL_RED, GL_RED, GL_RED, GL_ONE};
    const std::array<GLint, 4> identity = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};

    Vkm::Engine::TextureAsset texels;
    texels.params.width          = 4;
    texels.params.height         = 4;
    texels.params.internalFormat = Vkm::Engine::TextureInternalFormat::R8;
    texels.params.format         = Vkm::Engine::TexturePixelFormat::R;
    texels.pixelData.assign(4 * 4, 200);
    Vkm::Engine::GLTexture r8(texels);
    check("an R8 map reads its red in every colour channel, opaque", swizzleOf(r8) == grey);

    Vkm::Engine::TextureAsset blocks;
    blocks.params.width          = 4;
    blocks.params.height         = 4;
    blocks.params.internalFormat = Vkm::Engine::TextureInternalFormat::BC4R;
    blocks.params.format         = Vkm::Engine::TexturePixelFormat::R;
    blocks.pixelData.assign(8, 0);
    Vkm::Engine::GLTexture bc4(blocks);
    check("  and so does a BC4 one", swizzleOf(bc4) == grey);

    Vkm::Engine::TextureAsset colour;
    colour.params.width  = 2;
    colour.params.height = 2;
    colour.pixelData.assign(2 * 2 * 4, 90);
    Vkm::Engine::GLTexture rgba(colour);
    check("  while a four-channel one reads its own", swizzleOf(rgba) == identity);
    check("  with no GL error", noGlError());
}

// RenderSystem frees a texture's CPU pixels once the backend holds them, so the answer
// is yes only for the asset as it is now: pixels changed since are not on the GPU, and
// freeing them loses them.
void testABackendHoldsOnlyThePixelsItUploaded() {
    std::printf("Whether the backend holds a texture's pixels:\n");

    Vkm::Engine::ResourceManager resources;
    Vkm::Engine::TextureAsset    asset;
    asset.params.width  = 2;
    asset.params.height = 2;
    asset.pixelData.assign(2 * 2 * 4, 77);
    const Vkm::Engine::TextureHandle handle = resources.add(std::move(asset), "held");

    Vkm::Engine::GLView view;
    const uint64_t first = resources.get(handle).version();
    check("not before it is uploaded", !view.holdsPixels(handle, first));
    view.ensureTexture(handle, resources);
    check("  but once it is", view.holdsPixels(handle, first));

    // What RenderSystem then does: the pixels go, the upload stays.
    std::vector<uint8_t>().swap(resources.edit(handle).pixelData);
    view.ensureTexture(handle, resources);
    check("a texture whose pixels were freed still draws its upload", view.getTexture(handle) != nullptr);

    resources.edit(handle).pixelData.assign(2 * 2 * 4, 200);
    resources.commit(handle);
    check("an asset changed since is not held", !view.holdsPixels(handle, resources.get(handle).version()));
    check("  and no other handle is", !view.holdsPixels(Vkm::Engine::TextureHandle{}, first));
    check("  with no GL error", noGlError());
}

// Text is drawn far smaller than its atlas, minified through only as many levels as the
// baker's gutter allows, or the smallest would average two glyphs into one texel.
void testAFontAtlasKeepsTheLevelsItsGutterAllows() {
    std::printf("A font atlas:\n");

    Vkm::Engine::FontAsset font;
    font.atlasSize = 64;
    font.atlasPixels.assign(64 * 64, 200);
    Vkm::Engine::GLTexture atlas(font);
    check("it uploads without error", noGlError());

    atlas.getTexture().bindSlot(0);
    const GLint lastLevel = static_cast<GLint>(Vkm::Engine::FontAsset::MIP_LEVELS) - 1;
    GLint maxLevel  = 0;
    GLint minFilter = 0;
    GLint lastWidth = 0;
    GLint pastWidth = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maxLevel);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, lastLevel, GL_TEXTURE_WIDTH, &lastWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, lastLevel + 1, GL_TEXTURE_WIDTH, &pastWidth);
    check("  is sampled through its levels", minFilter == GL_LINEAR_MIPMAP_LINEAR);
    check("  down to the one the gutter allows", maxLevel == lastLevel && lastWidth == (64 >> lastLevel));
    check("  and builds none past it", pastWidth == 0);

    std::vector<uint8_t> smallest(static_cast<size_t>(lastWidth * lastWidth), 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, lastLevel, GL_RED, GL_UNSIGNED_BYTE, smallest.data());
    check(
        "  whose texels are the field's, not empty",
        !smallest.empty() && std::all_of(smallest.begin(), smallest.end(), [](uint8_t v) { return v == 200; })
    );
    check("  with no GL error", noGlError());
}

// A held shadow tile stays while its signature matches; it names a caster's mesh by
// handle and by upload. A re-cook keeps the handle, so a mesh re-uploaded in place
// must answer a new upload, or its tile keeps the old shadow.
void testAReuploadedMeshIsANewPicture() {
    std::printf("A mesh uploaded again under the same handle:\n");

    Vkm::Engine::GLMeshPool pool;
    Vkm::Engine::GLMesh mesh(pool, Vkm::Engine::generateCube());
    const uint64_t first = mesh.uploadId();
    mesh.update(Vkm::Engine::generateCube());
    check("it answers an upload the first one did not", mesh.uploadId() != first);

    const Vkm::Engine::GLMesh other(pool, Vkm::Engine::generateCube());
    check(
        "  and so does a second mirror of the same asset",
        other.uploadId() != first && other.uploadId() != mesh.uploadId()
    );
    check("  with no GL error behind any of it", noGlError());
}

// A replaced world can hash to a held signature while being made of something else, so
// the atlas forgets every tile when the world or asset graph goes.
void testAnAtlasThatForgetsDrawsEveryTileAgain() {
    std::printf("A shadow atlas told to forget what it holds:\n");

    Vkm::GL::Context gl;
    Vkm::Engine::GLShadowAtlas atlas;
    atlas.init(gl, Vkm::Engine::SHADOW_ATLAS_MIN_TILE_RES);

    constexpr uint64_t PICTURE = 42;
    atlas.begin2D(gl);
    atlas.beginTile(gl, 0, PICTURE);
    atlas.beginCubeFace(gl, 0, 0, PICTURE);
    check("a tile drawn with a picture holds it", atlas.tileHolds(0, PICTURE));
    check("  and so does a cube face", atlas.faceHolds(0, 0, PICTURE));

    atlas.forgetHeld();
    check("once forgotten, the tile is drawn again", !atlas.tileHolds(0, PICTURE));
    check("  and the face", !atlas.faceHolds(0, 0, PICTURE));
    check("with no GL error behind any of it", noGlError());
}

// A tile is held only once something drew it. A pass that asks and then draws nothing
// (every batch empty) must not record the tile as holding a picture, or a spot with no
// casters keeps that unwritten tile while nothing moves.
void testAnAtlasHoldsOnlyWhatWasDrawn() {
    std::printf("A shadow atlas asked about a picture it was never given:\n");

    Vkm::GL::Context gl;
    Vkm::Engine::GLShadowAtlas atlas;
    atlas.init(gl, Vkm::Engine::SHADOW_ATLAS_MIN_TILE_RES);

    constexpr uint64_t PICTURE = 7;
    atlas.tileHolds(1, PICTURE);
    atlas.faceHolds(1, 2, PICTURE);
    check("a tile asked about twice and never drawn does not hold it", !atlas.tileHolds(1, PICTURE));
    check("  nor does a cube face", !atlas.faceHolds(1, 2, PICTURE));
    atlas.begin2D(gl);
    atlas.beginTile(gl, 1, 0);
    check("a tile drawn unsigned holds nothing", !atlas.tileHolds(1, 0));
    check("with no GL error behind any of it", noGlError());
}

// An undrawn tile or face can still be sampled, so a new atlas reads as the far plane,
// lit, not as leftover allocation.
void testANewAtlasReadsAsTheFarPlane() {
    std::printf("A shadow atlas before anything is drawn into it:\n");

    Vkm::GL::Context gl;
    Vkm::Engine::GLShadowAtlas atlas;
    atlas.init(gl, Vkm::Engine::SHADOW_ATLAS_MIN_TILE_RES);

    constexpr uint32_t UNIT = 3;
    const auto allFar = [](const std::vector<float>& depth) {
        return !depth.empty() && std::all_of(depth.begin(), depth.end(), [](float d) { return d == 1.0f; });
    };

    const uint32_t tile = atlas.tileResolution();
    std::vector<float> atlasDepth(
        static_cast<size_t>(tile) * tile * Vkm::Engine::SHADOW_ATLAS_COLS * Vkm::Engine::SHADOW_ATLAS_ROWS,
        0.0f
    );
    atlas.bind2D(UNIT);
    glActiveTexture(GL_TEXTURE0 + UNIT);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, atlasDepth.data());
    check("every tile of the 2D atlas is at the far plane", allFar(atlasDepth));

    bool facesFar = true;
    std::vector<float> faceDepth(
        static_cast<size_t>(Vkm::Engine::SHADOW_CUBE_RES) * Vkm::Engine::SHADOW_CUBE_RES
    );
    for (uint32_t slot = 0; slot < Vkm::Engine::Config::MAX_SHADOW_CASTERS_CUBE; ++slot) {
        atlas.bindCube(slot, UNIT);
        for (int face = 0; face < 6; ++face) {
            std::fill(faceDepth.begin(), faceDepth.end(), 0.0f);
            glGetTexImage(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                0,
                GL_DEPTH_COMPONENT,
                GL_FLOAT,
                faceDepth.data()
            );
            facesFar = facesFar && allFar(faceDepth);
        }
    }
    glBindSampler(UNIT, 0);
    check("  and so is every face of every cube", facesFar);
    check("with no GL error behind any of it", noGlError());
}

// Many meshes as one glMultiDrawElementsIndirect: one vertex array and index buffer,
// each command finding its mesh by firstIndex and baseVertex and its instances by
// baseInstance. A full pool grows by GPU copy, so pooled meshes must draw the same after.
void testMeshesInOnePoolDrawAsOneMultiDraw() {
    using namespace Vkm::Engine;
    std::printf("Two meshes in one pool, drawn as one multi-draw:\n");

    Vkm::GL::setShaderPrelude(OPENGL_GLSL_VERSION, GLBackend::shaderConstants());

    // Each covers half of a two-pixel target, so a pixel says which mesh and instance
    // drew it.
    const auto halfQuad = [](float left) {
        MeshAsset quad;
        const glm::vec2 corners[] = {glm::vec2(0, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(0, 1)};
        for (const glm::vec2 corner : corners) {
            Vertex v{};
            v.position = glm::vec3(left + corner.x, corner.y, 0.0f);
            quad.vertices.push_back(v);
        }
        quad.indices = {0, 1, 2, 0, 2, 3};
        return quad;
    };
    GLMeshPool pool;
    const GLMesh left(pool, halfQuad(-1.0f));
    const GLMesh right(pool, halfQuad(0.0f));

    // More than the pool starts with, of both, so both streams grow under the two
    // meshes already in them.
    MeshAsset filler;
    filler.vertices.resize(70000);
    filler.indices.assign(300000, 0);
    const GLMesh big(pool, filler);

    GLDrawList list;
    list.instances() = {9, 3, 7};
    list.commands()  = {left.command(1, 1), right.command(1, 2)};
    list.upload();

    GLuint target = 0;
    GLuint fbo    = 0;
    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, 2, 1);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    const GLuint zero[4] = {0, 0, 0, 0};
    glClearBufferuiv(GL_COLOR, 0, zero);
    glViewport(0, 0, 2, 1);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    const Vkm::GL::Shader program("tests/render/shaders/object_index");
    check("the probe compiles", program.isValid());
    program.bind();
    list.draw(left, 0, 2);

    GLuint drawn[2] = {0, 0};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, 2, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, drawn);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &target);

    std::printf("      left %u, right %u\n", drawn[0], drawn[1]);
    check("each mesh drew its own half after the pool grew", drawn[0] != 0 && drawn[1] != 0);
    check("  each with the instance its baseInstance names", drawn[0] == 3 + 1 && drawn[1] == 7 + 1);
    check("with no GL error behind any of it", noGlError());
}

// A point light's cube is compared in hardware through a samplerCubeShadow, which
// returns undefined values unless the texture compares - so the unit
// GLShadowAtlas::bindCube binds must carry a comparing sampler.
void testAShadowCubeIsBoundForComparison() {
    std::printf("A point light's shadow cube, bound for the forward pass:\n");

    Vkm::GL::Context gl;
    Vkm::Engine::GLShadowAtlas atlas;
    atlas.init(gl, Vkm::Engine::SHADOW_ATLAS_MIN_TILE_RES);

    constexpr uint32_t UNIT = 5;
    atlas.bindCube(0, UNIT);
    glActiveTexture(GL_TEXTURE0 + UNIT);
    GLint sampler = 0;
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
    GLint mode = GL_NONE;
    if (sampler != 0) glGetSamplerParameteriv(static_cast<GLuint>(sampler), GL_TEXTURE_COMPARE_MODE, &mode);
    glBindSampler(UNIT, 0);

    check("the unit carries a sampler", sampler != 0);
    check("  and it compares against the stored depth", mode == GL_COMPARE_REF_TO_TEXTURE);
    check("with no GL error behind it", noGlError());
}

// Every shipped shader, compiled and linked as the backend does - same loader, prelude
// and GL version. CMake does not compile shaders and their first reader is GLBackend's
// constructor, so without this a renamed C++ constant or a dropped .glsl #include
// reaches a viewport before anything can report it.
void testEveryShippedShaderCompiles() {
    std::printf("Every shader the engine ships:\n");

    std::error_code ec;

    // The backend's own prelude: its #version and the constants it writes from C++.
    Vkm::GL::setShaderPrelude(Vkm::Engine::OPENGL_GLSL_VERSION, Vkm::Engine::GLBackend::shaderConstants());

    // Walked, not listed: a list restates the tree, and the shader it forgets is the
    // one nothing compiles - the very failure this catches.
    std::vector<std::string> graphics;
    std::vector<std::string> compute;
    namespace fs = std::filesystem;
    for (fs::recursive_directory_iterator it("shaders", ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_directory(entryEc) || entryEc) continue;
        const fs::path& dir = it->path();
        if (fs::exists(dir / "compute.shader", entryEc)) {
            compute.push_back(dir.generic_string());
        } else if (fs::exists(dir / "vertex.shader", entryEc)
            && fs::exists(dir / "fragment.shader", entryEc)) {
            graphics.push_back(dir.generic_string());
        }
    }
    std::sort(graphics.begin(), graphics.end());
    std::sort(compute.begin(), compute.end());

    int failed = 0;
    for (const std::string& path : graphics) {
        try {
            const Vkm::GL::Shader program(path);
            if (!program.isValid()) {
                std::printf("      %s did not link\n", path.c_str());
                ++failed;
            }
        } catch (const std::exception& e) {
            std::printf("      %s: %s\n", path.c_str(), e.what());
            ++failed;
        }
    }
    for (const std::string& path : compute) {
        try {
            const Vkm::GL::ComputeShader program(path);
            if (!program.isValid()) {
                std::printf("      %s did not link\n", path.c_str());
                ++failed;
            }
        } catch (const std::exception& e) {
            std::printf("      %s: %s\n", path.c_str(), e.what());
            ++failed;
        }
    }

    std::printf("      %zu graphics + %zu compute programs\n", graphics.size(), compute.size());
    // A walk finding nothing would compile nothing and pass.
    check("the shader tree was walked", !graphics.empty() && !compute.empty());
    check("all of them compile and link against the backend's own prelude", failed == 0);
}

// A shader's constants must be installed before anything compiles, and a backend
// builds programs in its members' constructors, so installing in init() is too late.
// The suite above sets the prelude itself; a host that does not gets shaders missing
// every constant.
void testABackendInstallsItsConstantsBeforeItCompilesAnything() {
    std::printf("A backend built the way a host builds one:\n");

    Vkm::GL::setShaderPrelude(Vkm::Engine::OPENGL_GLSL_VERSION, {});

    bool built = true;
    try {
        const Vkm::Engine::GLBackend backend;
        (void)backend;
    } catch (const std::exception& e) {
        std::printf("      %s\n", e.what());
        built = false;
    }
    check("its own programs compile, with no host having set the prelude", built);
}

// GL starts with seamless cube filtering off; a linear read near a face edge then
// clamps to that face, and the cube shows its seams.
void testABackendFiltersCubesAcrossTheirSeams() {
    std::printf("A backend's cube maps, read near a face edge:\n");

    glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    Vkm::Engine::WindowManager window;
    Vkm::Engine::GLBackend backend;
    const bool started = backend.init(window);
    check("the backend starts", started);
    check("  and filters across the faces", glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS) == GL_TRUE);
    check("with no GL error behind it", noGlError());
}

// The editor's thumbnails (GLPreview) draw one mesh outside the pass list, between
// frames, so no golden frame would notice them drawing nothing.
void testAPreviewDrawsItsMesh() {
    using namespace Vkm::Engine;
    std::printf("A material preview, rendered the way the editor asks for one:\n");

    WindowManager window;
    GLBackend backend;
    check("the backend starts", backend.init(window));

    ResourceManager resources;
    MaterialAsset white;
    white.albedo = glm::vec4(1.0f);
    PreviewRequest request;
    request.key      = 1;
    request.size     = 64;
    request.mesh     = resources.add(generateCube(), "test:cube");
    request.material = resources.add(std::move(white), "test:white");

    const GpuTextureId id = backend.editorHooks()->renderPreview(request, resources);
    check("it hands back a texture", id != 0);

    std::vector<uint8_t> pixels(64 * 64 * 4, 0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto luma = [&](int x, int y) {
        const uint8_t* p = &pixels[static_cast<size_t>((y * 64 + x) * 4)];
        return p[0] + p[1] + p[2];
    };
    std::printf("      centre %d, corner %d\n", luma(32, 32), luma(1, 1));
    check("  with the lit mesh in front of the backdrop", luma(32, 32) > luma(1, 1) + 60);
    check("with no GL error behind it", noGlError());
}

// The GGX peak, 1 / (PI alpha^2), is a polished surface's sun glint. A 1e-7 floor under
// the denominator would cut it below roughness ~0.115, by 1893x at MIN_ROUGHNESS.
void testTheGgxLobeKeepsItsPeak() {
    std::printf("The shared GGX lobe at its peak:\n");

    // The first is the smoothest the forward pass shades with, which the probe takes
    // from brdf.glsl and writes back.
    float buffer[8] = {0.0f, 0.1f, 0.3f, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f};

    Vkm::GL::ShaderStorageBuffer peaks(buffer, sizeof(buffer));
    const Vkm::GL::ComputeShader probe("tests/render/shaders/ggx_peak");
    check("the probe compiles", probe.isValid());

    peaks.bindBase(0);
    probe.bind();
    probe.dispatch(1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    peaks.bind();
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(buffer), buffer);

    bool exact = true;
    for (int i = 0; i < 4; ++i) {
        const float alpha = buffer[i] * buffer[i];
        const float want  = 1.0f / (glm::pi<float>() * alpha * alpha);
        const float got   = buffer[4 + i];
        std::printf("      roughness %.3f: %.6g, want %.6g\n", buffer[i], got, want);
        if (!(std::abs(got - want) <= want * 0.02f)) exact = false;
    }
    check("at every roughness it is 1 / (PI alpha^2)", exact);
    check("with no GL error behind it", noGlError());
}

// An area light's diffuse is a form factor in a frame about the shading normal. Built
// from the view direction, that frame has no tangent when the eye looks straight down
// the normal, and the NaN there is spread by bloom.
void testAnAreaLightsFormFactorHoldsStraightDownTheNormal() {
    std::printf("An area light's diffuse form factor:\n");

    struct Case {
        glm::vec4 normal[2];
        float     halfSize;
        float     height;
        float     pad[2];
        float     formFactor[2];
    };
    Case data{};
    data.normal[0] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    data.normal[1] = glm::vec4(0.0f, 0.70710678f, 0.70710678f, 0.0f);
    data.halfSize  = 1.0f;
    data.height    = 2.0f;

    Vkm::GL::ShaderStorageBuffer buffer(&data, sizeof(data));
    const Vkm::GL::ComputeShader probe("tests/render/shaders/area_form_factor");
    check("the probe compiles", probe.isValid());

    buffer.bindBase(0);
    probe.bind();
    probe.dispatch(1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    buffer.bind();
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(data), &data);

    // A point below the centre of a parallel square, by its four quadrants.
    const float x = data.halfSize / data.height;
    const float rim = std::sqrt(1.0f + x * x);
    const float want = 4.0f / (2.0f * glm::pi<float>()) * 2.0f * (x / rim) * std::atan(x / rim);

    const float straight = data.formFactor[0];
    const float tilted   = data.formFactor[1];
    std::printf("      facing it %.5f (closed form %.5f), turned 45 degrees %.5f\n", straight, want, tilted);
    check("facing the emitter it is the square's form factor", std::abs(straight - want) <= want * 0.02f);
    check("  and turned away a little it is less, and still a number", tilted > 0.0f && tilted < straight);
    check("with no GL error behind it", noGlError());
}

// The backbuffer takes display values, encoded by linearToSrgb in color.glsl. A 2.2
// power instead would not return an sRGB texture's byte as itself - up to nine steps
// off in the darks - so no tonemap built to hold albedo could.
void testTheDisplayEncodeInvertsTheSrgbDecode() {
    std::printf("The display encode, against the sampler's sRGB decode:\n");

    std::vector<uint8_t> bytes(256 * 4);
    for (int i = 0; i < 256; ++i) {
        bytes[i * 4 + 0] = bytes[i * 4 + 1] = bytes[i * 4 + 2] = static_cast<uint8_t>(i);
        bytes[i * 4 + 3] = 255;
    }
    Vkm::GL::Texture2DParams params;
    params.width           = 256;
    params.height          = 1;
    params.internalFormat  = GL_SRGB8_ALPHA8;
    params.format          = GL_RGBA;
    params.type            = GL_UNSIGNED_BYTE;
    params.minFilter       = Vkm::GL::TextureMinFilter::Nearest;
    params.magFilter       = Vkm::GL::TextureMagFilter::Nearest;
    params.generateMipmaps = false;
    params.data            = bytes.data();
    const Vkm::GL::Texture2D texture("srgb_bytes", params);

    std::vector<float> encoded(256, -1.0f);
    Vkm::GL::ShaderStorageBuffer out(encoded.data(), static_cast<uint32_t>(encoded.size() * sizeof(float)));
    const Vkm::GL::ComputeShader probe("tests/render/shaders/srgb_round_trip");
    check("the probe compiles", probe.isValid());

    texture.bindSlot(0);
    out.bindBase(0);
    probe.bind();
    probe.dispatch(1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    out.bind();
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, encoded.size() * sizeof(float), encoded.data());

    int worst = 0;
    for (int i = 0; i < 256; ++i) {
        const int back = static_cast<int>(std::lround(encoded[i] * 255.0f));
        worst = std::max(worst, std::abs(back - i));
    }
    std::printf("      worst byte off by %d\n", worst);
    check("every byte comes back as itself", worst == 0);
    check("with no GL error behind it", noGlError());
}

// A cutout's shadow is cut by its albedo map, so the map is part of a held tile: a map
// re-uploaded under the same handle must re-sign it. And cutouts sharing a mesh but not
// a material are two runs (a run binds one material); opaque casters sharing a mesh are one.
void testACutoutsMapIsPartOfItsShadow() {
    using namespace Vkm::Engine;
    std::printf("Alpha-masked shadow casters under a spot light:\n");

    ResourceManager resources;
    TextureAsset map;
    map.params.width  = 2;
    map.params.height = 2;
    map.pixelData.assign(2 * 2 * 4, 255);
    const TextureHandle holes = resources.add(std::move(map), "test:holes");

    const auto material = [&](const char* name, MaterialType type) {
        MaterialAsset asset;
        asset.type          = type;
        asset.albedoTexture = holes;
        return resources.add(std::move(asset), name);
    };
    const MaterialHandle leafA  = material("test:leafA", MaterialType::AlphaMask);
    const MaterialHandle leafB  = material("test:leafB", MaterialType::AlphaMask);
    const MaterialHandle stoneA = material("test:stoneA", MaterialType::Opaque);
    const MaterialHandle stoneB = material("test:stoneB", MaterialType::Opaque);
    const MeshHandle     cube   = resources.add(generateCube(), "test:cube");

    RenderObjects objects;
    const MaterialHandle materials[4] = {leafA, leafB, stoneA, stoneB};
    for (uint32_t i = 0; i < 4; ++i) {
        const glm::vec3 at(static_cast<float>(i) - 1.5f, 0.5f, 0.0f);
        objects.models.push_back(glm::translate(glm::mat4(1.0f), at));
        objects.bounds.push_back({at - glm::vec3(0.5f), at + glm::vec3(0.5f)});
        objects.draws.push_back({cube, materials[i], 0});
        objects.skinFirst.push_back(0);
        objects.visible.push_back(i);
        objects.scene.push_back(i);
    }
    objects.casterCount = 4;

    RenderView view;
    view.objects           = &objects;
    view.camera = Vkm::Engine::CameraData::from(
        glm::lookAt(glm::vec3(0.0f, 3.0f, 8.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f),
        glm::vec3(0.0f, 3.0f, 8.0f)
    );
    LightData spot{};
    spot.type           = LightType::Spot;
    spot.position       = glm::vec3(0.0f, 6.0f, 0.0f);
    spot.direction      = glm::vec3(0.0f, -1.0f, 0.0f);
    spot.radius         = 20.0f;
    spot.innerConeAngle = 0.6f;
    spot.outerConeAngle = 0.9f;
    spot.castShadows    = true;
    spot.shadowBias     = 0.001f;
    view.lights.push_back(spot);

    GLView       glView;
    GLShadowData plan;
    const auto signature = [&]() {
        glView.sync(view, resources);
        plan.build(view, glView, SHADOW_ATLAS_MIN_TILE_RES);
        plan.finishCull();
        return plan.batch2D(0).signature;
    };

    const uint64_t first = signature();
    check("the spot's tile is signed", first != 0 && plan.jobs2D().size() == 1);
    check("  and the same picture signs the same", signature() == first);
    check("two cutouts on one mesh are two runs, two opaque casters one", plan.batch2D(0).runs.size() == 3);

    resources.edit(holes).pixelData[3] = 0;
    resources.commit(holes);
    check("a new cut in the map is a new picture", signature() != first);
    check("with no GL error behind any of it", noGlError());
}

namespace ShadowPlan {

using namespace Vkm::Engine;

// Casters for GLShadowData to plan, under one light, seen from a camera above the
// origin looking down -Z.
struct Frame {
    ResourceManager resources;
    RenderObjects   objects;
    RenderView      view;
    GLView          glView;
    GLShadowData    plan;

    Frame() {
        view.objects = &objects;
        const glm::vec3 eye(0.0f, 2.0f, 0.0f);
        view.camera = CameraData::from(
            glm::lookAt(eye, eye + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f),
            eye
        );
    }

    void cast(MeshHandle mesh, MaterialHandle material, glm::vec3 at, uint32_t bones = 0) {
        const uint32_t object = static_cast<uint32_t>(objects.draws.size());
        objects.models.push_back(glm::translate(glm::mat4(1.0f), at));
        objects.bounds.push_back({at - glm::vec3(0.5f), at + glm::vec3(0.5f)});
        objects.draws.push_back({mesh, material, bones});
        objects.skinFirst.push_back(0);
        objects.visible.push_back(object);
        objects.scene.push_back(object);
        objects.casterCount = static_cast<uint32_t>(objects.scene.size());
    }

    void spot() {
        LightData light{};
        light.type           = LightType::Spot;
        light.position       = glm::vec3(0.0f, 6.0f, 0.0f);
        light.direction      = glm::vec3(0.0f, -1.0f, 0.0f);
        light.radius         = 20.0f;
        light.innerConeAngle = 0.4f;
        light.outerConeAngle = 0.5f;
        light.castShadows    = true;
        view.lights.push_back(light);
    }

    void plan2D() {
        glView.sync(view, resources);
        plan.build(view, glView, SHADOW_ATLAS_MIN_TILE_RES);
        plan.finishCull();
    }
};

} // namespace ShadowPlan

// A tile's runs group by vertex layout and pose before mesh, so each is one stretch of
// multi-draws: a skinned mesh whose id sits between two static ones draws after both.
void testATilesRunsKeepEachLayoutTogether() {
    using namespace Vkm::Engine;
    std::printf("The runs of a shadow tile:\n");

    ShadowPlan::Frame frame;
    const MaterialHandle stone = frame.resources.add(MaterialAsset{}, "test:stone");
    const MeshHandle     first = frame.resources.add(generateCube(), "test:first");
    MeshAsset rigged = generateCube();
    rigged.skin.resize(rigged.vertices.size());
    const MeshHandle skinned = frame.resources.add(std::move(rigged), "test:skinned");
    const MeshHandle last    = frame.resources.add(generateCube(), "test:last");

    frame.cast(first, stone, {-1.5f, 0.5f, 0.0f});
    frame.cast(skinned, stone, {0.0f, 0.5f, 0.0f}, 4);
    frame.cast(last, stone, {1.5f, 0.5f, 0.0f});
    frame.spot();
    frame.plan2D();

    const std::vector<ShadowRun>& runs = frame.plan.batch2D(0).runs;
    bool ordered = runs.size() == 3;
    for (size_t r = 1; r < runs.size(); ++r) ordered = ordered && runs[r - 1].key < runs[r].key;
    check("a run per mesh", runs.size() == 3);
    check("  the posed one last", ordered && runs.back().posed && !runs.front().posed && !runs[1].posed);
}

// A held tile is signed by its contents; a cutout elsewhere is not in it, though it
// renumbers the keys cutouts are grouped by.
void testACutoutElsewhereLeavesAHeldTileHeld() {
    using namespace Vkm::Engine;
    std::printf("A spot's held tile, when a cutout appears elsewhere:\n");

    ShadowPlan::Frame frame;
    const auto cutout = [&](const char* name) {
        MaterialAsset asset;
        asset.type = MaterialType::AlphaMask;
        return frame.resources.add(std::move(asset), name);
    };
    const MaterialHandle early = cutout("test:early");
    const MaterialHandle leaf  = cutout("test:leaf");
    const MeshHandle     cube  = frame.resources.add(generateCube(), "test:card");

    frame.cast(cube, leaf, {0.0f, 0.5f, 0.0f});
    frame.spot();
    frame.plan2D();
    const uint64_t before = frame.plan.batch2D(0).signature;

    // Sorted ahead of the leaf's pair, far outside the spot's cone.
    frame.cast(cube, early, {80.0f, 0.5f, 0.0f});
    frame.plan2D();
    check("the tile is signed", before != 0);
    check("  and signed the same", frame.plan.batch2D(0).signature == before);
}

// Cascades draw with depth clamped, so a caster nearer the sun than a cascade's near
// plane still shadows - only if the cull keeps it rather than rejecting it there.
void testACasterAboveTheCascadesIsKept() {
    using namespace Vkm::Engine;
    std::printf("A caster far up toward the sun:\n");

    ShadowPlan::Frame frame;
    const MaterialHandle stone = frame.resources.add(MaterialAsset{}, "test:stone");
    const MeshHandle     cube  = frame.resources.add(generateCube(), "test:cube");
    frame.cast(cube, stone, {0.0f, 400.0f, -3.0f});

    LightData sun{};
    sun.type           = LightType::Directional;
    sun.direction      = glm::vec3(0.0f, -1.0f, 0.0f);
    sun.castShadows    = true;
    sun.shadowDistance = 50.0f;
    frame.view.lights.push_back(sun);
    frame.plan2D();

    bool kept = false;
    for (size_t j = 0; j < frame.plan.jobs2D().size(); ++j) {
        kept = kept || (frame.plan.jobs2D()[j].cascade && !frame.plan.batch2D(j).order.empty());
    }
    check("a cascade keeps it", kept);
}

// A skin stream is uploaded per vertex; one shorter than the vertices would be read
// past its end, so the mesh is refused.
void testAMeshWhoseSkinIsShortIsRefused() {
    using namespace Vkm::Engine;
    std::printf("A skinned mesh whose skin does not cover its vertices:\n");

    GLMeshPool pool;
    MeshAsset whole = generateCube();
    whole.skin.resize(whole.vertices.size());
    MeshAsset shortSkin = whole;
    shortSkin.skin.pop_back();

    const MeshRange taken   = pool.add(whole);
    const MeshRange refused = pool.add(shortSkin);
    check("one with a skin entry per vertex is taken", taken.indexCount > 0);
    check("  and one with fewer is refused", refused.indexCount == 0);
    pool.remove(taken);
    check("with no GL error behind any of it", noGlError());
}

// Filtering is pushed to every texture only when the setting moves, so a texture
// uploaded later must take it at build, or keep its upload's until the next change.
void testATextureUploadedLaterTakesTheFiltering() {
    using namespace Vkm::Engine;
    std::printf("A texture uploaded after the filtering was set:\n");

    ResourceManager resources;
    TextureAsset checker;
    checker.params.width  = 4;
    checker.params.height = 4;
    checker.pixelData.assign(4 * 4 * 4, 200);
    const TextureHandle handle = resources.add(std::move(checker), "test:filtered");

    GLView view;
    view.setTextureFiltering(TextureFiltering::Nearest, 1.0f);
    view.ensureTexture(handle, resources);

    const Vkm::GL::Texture2D* texture = view.getTexture(handle);
    check("it uploads", texture != nullptr);
    GLint minFilter = 0;
    GLint magFilter = 0;
    if (texture) {
        texture->bindSlot(0);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
    }
    check(
        "  filtered as the setting says, not as its upload left it",
        minFilter == GL_NEAREST_MIPMAP_NEAREST || minFilter == GL_NEAREST
    );
    check("  close up too", magFilter == GL_NEAREST);
    check("with no GL error behind it", noGlError());
}

// A re-import's stub stands in until its pixels land, while the old upload draws. The
// stub describes pixels not yet here: filtering the old upload through levels it lacks
// leaves it incomplete, which samples black.
void testAStubKeepsTheFilteringOfTheUploadItReplaces() {
    using namespace Vkm::Engine;
    std::printf("A texture re-imported with levels, before its pixels land:\n");

    ResourceManager resources;
    TextureAsset flat;
    flat.params.width           = 4;
    flat.params.height          = 4;
    flat.params.generateMipmaps = false;
    flat.pixelData.assign(4 * 4 * 4, 200);
    const TextureHandle handle = resources.add(std::move(flat), "test:reimported");

    GLView view;
    view.ensureTexture(handle, resources);

    TextureAsset& stub = resources.edit(handle);
    stub.params.generateMipmaps = true;
    std::vector<uint8_t>().swap(stub.pixelData);
    resources.commit(handle);
    view.ensureTexture(handle, resources);

    const Vkm::GL::Texture2D* texture = view.getTexture(handle);
    check("the upload it had still draws", texture != nullptr);
    GLint minFilter = 0;
    if (texture) {
        texture->bindSlot(0);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    }
    check(
        "  filtered as the level it has, not the chain the stub asks for",
        minFilter == GL_LINEAR || minFilter == GL_NEAREST
    );
    check("with no GL error behind it", noGlError());
}

// A handle names a slot and a generation: after the slot is freed and reused, an old
// handle must find nothing, not the stranger's upload.
void testAStaleHandleFindsNothing() {
    using namespace Vkm::Engine;
    std::printf("A handle kept past its asset:\n");

    ResourceManager resources;
    GLView view;

    TextureAsset first;
    first.params.width  = 2;
    first.params.height = 2;
    first.pixelData.assign(2 * 2 * 4, 10);
    const TextureHandle gone = resources.add(std::move(first), "test:gone");
    view.ensureTexture(gone, resources);
    resources.remove(gone);

    TextureAsset second;
    second.params.width  = 2;
    second.params.height = 2;
    second.pixelData.assign(2 * 2 * 4, 250);
    const TextureHandle taken = resources.add(std::move(second), "test:taken");
    view.ensureTexture(taken, resources);

    check("the new asset reuses the slot", taken.id() == gone.id());
    check("  and is found by its own handle", view.getTexture(taken) != nullptr);
    check("  but not by the one kept from before", view.getTexture(gone) == nullptr);
    check("  which holds no pixels either", !view.holdsPixels(gone, resources.get(taken).version()));
    check("with no GL error behind it", noGlError());
}

// One property of a named resource of the linked program; -1 when there is none.
GLint programResource(GLuint program, GLenum iface, const char* name, GLenum property) {
    const GLuint index = glGetProgramResourceIndex(program, iface, name);
    if (index == GL_INVALID_INDEX) return -1;
    GLint value = -1;
    glGetProgramResourceiv(program, iface, index, 1, &property, 1, nullptr, &value);
    return value;
}

GLuint linkedProgram(const Vkm::GL::Shader& shader) {
    shader.bind();
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    return static_cast<GLuint>(program);
}

// Blocks a C++ struct fills and a shader reads are laid out by two compilers that never
// meet: a member moved on one side reads wrong bytes on the other, silently. Asked of
// the linked program, the layout the driver really uses.
void testTheSharedBlocksAgreeWithTheirStructs() {
    using namespace Vkm::Engine;
    std::printf("The blocks C++ fills and the shaders read:\n");

    const Vkm::GL::Shader pbr("shaders/forward/pbr");
    const GLuint forward = linkedProgram(pbr);

    const auto offsetOf = [&](GLenum iface, const char* name) {
        return programResource(forward, iface, name, GL_OFFSET);
    };
    const auto uniformAt = [&](const char* name, size_t offset) {
        return offsetOf(GL_UNIFORM, name) == static_cast<GLint>(offset);
    };
    const auto strideOf = [](GLuint program, const char* name) {
        return programResource(program, GL_BUFFER_VARIABLE, name, GL_TOP_LEVEL_ARRAY_STRIDE);
    };

    check(
        "the shadow block is the size of ShadowUBOData",
        programResource(forward, GL_UNIFORM_BLOCK, "ShadowBlock", GL_BUFFER_DATA_SIZE)
            == static_cast<GLint>(sizeof(ShadowUBOData))
    );
    check(
        "  its splits and its cascade run where the struct has them",
        uniformAt("ShadowBlock.cascadeSplits", offsetof(ShadowUBOData, cascadeSplits))
            && uniformAt("ShadowBlock.csmBase", offsetof(ShadowUBOData, csmBase))
    );
    check(
        "  a 2D tile's parameters where the struct has them",
        uniformAt("ShadowBlock.s2d[0].params", offsetof(ShadowUBOData, s2d) + offsetof(Shadow2DGPU, params))
    );
    check(
        "  every 2D tile's last member where the struct has it",
        uniformAt(
            "ShadowBlock.s2d[1].shape",
            offsetof(ShadowUBOData, s2d) + sizeof(Shadow2DGPU) + offsetof(Shadow2DGPU, shape)
        )
    );
    check(
        "  and the cubes after them",
        uniformAt(
            "ShadowBlock.scube[0].params",
            offsetof(ShadowUBOData, scube) + offsetof(ShadowCubeGPU, params)
        )
    );

    check(
        "a light is a GpuLight wide",
        strideOf(forward, "LightsBlock.lights[0].position") == static_cast<GLint>(sizeof(GpuLight))
    );
    check(
        "  the list starts where LightsBuffer's does",
        programResource(forward, GL_BUFFER_VARIABLE, "LightsBlock.lights[0].position", GL_OFFSET)
            == static_cast<GLint>(offsetof(LightsBuffer, lights))
    );
    check(
        "  its shadow slot where GpuLight has it",
        programResource(forward, GL_BUFFER_VARIABLE, "LightsBlock.lights[0].spot", GL_OFFSET)
            == static_cast<GLint>(offsetof(LightsBuffer, lights) + offsetof(GpuLight, spot))
    );
    check(
        "  and its last member is where GpuLight has it",
        programResource(forward, GL_BUFFER_VARIABLE, "LightsBlock.lights[0].axisV", GL_OFFSET)
            == static_cast<GLint>(offsetof(LightsBuffer, lights) + offsetof(GpuLight, axisV))
    );

    check(
        "the camera block is the size of CameraUBO",
        programResource(forward, GL_UNIFORM_BLOCK, "CameraBlock", GL_BUFFER_DATA_SIZE)
            == static_cast<GLint>(sizeof(CameraUBO))
    );
    check(
        "  its position, viewport and planes where the struct has them",
        uniformAt("CameraBlock.cameraPosition", offsetof(CameraUBO, cameraPosition))
            && uniformAt("CameraBlock.viewport", offsetof(CameraUBO, viewport))
            && uniformAt("CameraBlock.zNear", offsetof(CameraUBO, zNear))
            && uniformAt("CameraBlock.zFar", offsetof(CameraUBO, zFar))
    );

    check(
        "the material block is the size of MaterialUBO",
        programResource(forward, GL_UNIFORM_BLOCK, "MaterialBlock", GL_BUFFER_DATA_SIZE)
            == static_cast<GLint>(sizeof(MaterialUBO))
    );
    check(
        "  its colours and its scalar tail where the struct has them",
        uniformAt("MaterialBlock.attenuationColor", offsetof(MaterialUBO, attenuationColor))
            && uniformAt("MaterialBlock.metallic", offsetof(MaterialUBO, metallic))
            && uniformAt("MaterialBlock.alphaCutoff", offsetof(MaterialUBO, alphaCutoff))
            && uniformAt("MaterialBlock.textureFlags", offsetof(MaterialUBO, textureFlags))
    );

    check(
        "the probe block is the size of ProbeBlock",
        programResource(forward, GL_UNIFORM_BLOCK, "ProbeBlock", GL_BUFFER_DATA_SIZE)
            == static_cast<GLint>(sizeof(ProbeBlock))
    );
    check(
        "  a probe's parameters where GpuProbe has them, a GpuProbe apart",
        uniformAt("ProbeBlock.probes[0].params", offsetof(GpuProbe, params))
            && uniformAt("ProbeBlock.probes[1].center", sizeof(GpuProbe))
    );

    check(
        "a cluster's list is CLUSTER_STRIDE wide",
        strideOf(forward, "ClusterGrid.clusters[0].count")
            == static_cast<GLint>(GLClusterGrid::CLUSTER_STRIDE)
    );

    const Vkm::GL::Shader particle("shaders/particle");
    const GLuint billboards = linkedProgram(particle);
    check(
        "a particle is a ParticleData wide",
        strideOf(billboards, "ParticleBlock.particles[0].positionSize")
            == static_cast<GLint>(sizeof(ParticleData))
    );
    check(
        "  with its parameters where ParticleData has them",
        programResource(billboards, GL_BUFFER_VARIABLE, "ParticleBlock.particles[0].params", GL_OFFSET)
            == static_cast<GLint>(offsetof(ParticleData, params))
    );
    check("with no GL error behind any of it", noGlError());
}

// An animated sun moves a fraction of a degree a frame, the moon with it; both stay
// within one bake until the sky visibly moves, or the sky re-bakes every frame.
void testASkyBakesAgainOnlyOnceItsSunHasMoved() {
    std::printf("A procedural sky's bake signature:\n");
    using namespace Vkm::Engine;

    const auto at = [](float elevation) {
        Environment env;
        env.sky.sunElevation = elevation;
        SkyParams sky;
        sky.sunDir  = env.sunDirection();
        sky.moonDir = env.moonDirection();
        return sky;
    };
    check("a sun a hundredth of a degree on bakes the same", at(30.0f) == at(30.01f));
    check("  and one a degree on does not", !(at(30.0f) == at(31.0f)));
}

int runTests() {
    Vkm::Log::Logger::init("/tmp/vkm_render_tests.log", "VKM-RENDER-TESTS", Vkm::Log::LogLevel::ERROR);

    // Pinned here, not in a test: the shader loader resolves paths against the working
    // directory, and a test setting it would make later tests depend on order.
    std::error_code rootEc;
    std::filesystem::current_path(VKM_ENGINE_ROOT, rootEc);
    if (rootEc) {
        std::printf("Cannot reach the engine root '%s': %s\n", VKM_ENGINE_ROOT, rootEc.message().c_str());
        return 1;
    }

    Vkm::Test::GLContext gl;
    if (!gl.available()) {
        // Not a failure: this binary is expected to run without a GPU.
        std::printf("No GL context: %s\nSkipping the GPU suite.\n", gl.reason().c_str());
        return 0;
    }

    testTheContextItself(gl);
    testAVertexBufferRoundTrips();
    testAnIndexBufferKnowsItsCount();
    testAShaderStorageBufferBindsToItsPoint();
    testAVertexArrayRefedWithANewBufferRewiresTheSameLocations();
    testAVolumeTextureRoundTrips();
    testATextureThatArrivesLateTakesItsRealFormat();
    testACookedTextureUploadsTheLevelsItCarries();
    testAOneChannelMapReadsAsGrey();
    testABackendHoldsOnlyThePixelsItUploaded();
    testAFontAtlasKeepsTheLevelsItsGutterAllows();
    testAReuploadedMeshIsANewPicture();
    testAnAtlasThatForgetsDrawsEveryTileAgain();
    testAnAtlasHoldsOnlyWhatWasDrawn();
    testANewAtlasReadsAsTheFarPlane();
    testAShadowCubeIsBoundForComparison();
    testEveryShippedShaderCompiles();
    testABackendInstallsItsConstantsBeforeItCompilesAnything();
    testABackendFiltersCubesAcrossTheirSeams();
    testAPreviewDrawsItsMesh();
    testTheGgxLobeKeepsItsPeak();
    testTheDisplayEncodeInvertsTheSrgbDecode();
    testAnAreaLightsFormFactorHoldsStraightDownTheNormal();
    testACutoutsMapIsPartOfItsShadow();
    testATilesRunsKeepEachLayoutTogether();
    testACutoutElsewhereLeavesAHeldTileHeld();
    testACasterAboveTheCascadesIsKept();
    testAMeshWhoseSkinIsShortIsRefused();
    testATextureUploadedLaterTakesTheFiltering();
    testAStubKeepsTheFilteringOfTheUploadItReplaces();
    testAStaleHandleFindsNothing();
    testTheSharedBlocksAgreeWithTheirStructs();
    testMeshesInOnePoolDrawAsOneMultiDraw();
    testASkyBakesAgainOnlyOnceItsSunHasMoved();
    // Last: it gives the context a surface, which the tests above do not want.
    Vkm::Test::runGoldenTests(gl, g_failures);

    std::printf(g_failures ? "\n%d FAILURE(S)\n" : "\nALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}

} // namespace

int main() {
    const int status = runTests();
    // See ThreadPool::shutdown.
    Vkm::Engine::ThreadPool::get().shutdown();
    return status;
}
