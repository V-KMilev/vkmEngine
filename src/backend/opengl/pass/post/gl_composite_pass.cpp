#include "pass/post/gl_composite_pass.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <utility>
#include <vector>

#include <GL/glew.h>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/target/frame_buffer.h"
#include "vkm_opengl/texture/texture_3d.h"

#include "gl_screen_triangle.h"

#include "pass/gl_frame_context.h"
#include "storage/gl_target.h"
#include "pass/post/gl_bloom_pass.h"
#include "storage/gl_bloom.h"
#include "storage/light/gl_shadow_atlas.h"
#include "convention/gl_bindings.h"
#include "core/render_mode.h"
#include "debug/error/engine_error_log.h"
#include "io/project/roots.h"
#include "decoded_image.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

// When the file at @p path was written; for none, the earliest time, so its arrival reads as a save.
std::filesystem::file_time_type writtenAt(const std::string& path) {
    std::error_code absent;
    return std::filesystem::last_write_time(Roots::resolveProjectPath(path), absent);
}

} // namespace

GLCompositePass::GLCompositePass()
    : m_shader("shaders/opengl/post/composite") {}

GLCompositePass::~GLCompositePass() = default;

void GLCompositePass::adoptGrading(const std::string& path) {
    if (path == m_gradingPath) return;
    m_gradingPath = path;
    m_grading.reset();
    if (path.empty()) return;
    m_gradingWritten = writtenAt(path);

    // The project's to mend, as a splash image is; until then the frame goes ungraded.
    const DecodedImage strip = decodeImageRGBA(path);
    if (!strip.isValid()) {
        reportError("Project", path, "could not be decoded; the frame is not graded");
        return;
    }
    const uint32_t size = RenderSettings::gradingLutSize(strip.width, strip.height);
    if (size == 0) {
        const std::string shape = std::to_string(strip.width) + " by " + std::to_string(strip.height);
        reportError(
            "Project",
            path,
            "is " + shape + ", not a strip of N squares N texels on a side; the frame is not graded"
        );
        return;
    }

    // Square b's row g is the table's row (g, b). The rows decode bottom-up, so green rises up a square.
    const size_t row = size_t{size} * 4;
    std::vector<uint8_t> table(strip.pixels.size());
    for (uint32_t b = 0; b < size; ++b) {
        for (uint32_t g = 0; g < size; ++g) {
            const uint8_t* from = strip.pixels.data() + (size_t{g} * strip.width + size_t{b} * size) * 4;
            std::memcpy(table.data() + (size_t{b} * size + g) * row, from, row);
        }
    }

    // Plain UNORM, not sRGB: the texels are display values and are blended as they are stored, so
    // the identity table gives back what it is given.
    Vkm::OpenGL::Texture3DParams params;
    params.width          = size;
    params.height         = size;
    params.depth          = size;
    params.internalFormat = GL_RGBA8;
    params.format         = GL_RGBA;
    params.type           = GL_UNSIGNED_BYTE;
    m_grading             = std::make_unique<Vkm::OpenGL::Texture3D>("grading", params);
    m_grading->upload(table.data());
}

bool GLCompositePass::reloadChangedGrading() {
    if (m_gradingPath.empty() || writtenAt(m_gradingPath) == m_gradingWritten) return false;
    adoptGrading(std::exchange(m_gradingPath, {}));
    return true;
}

void GLCompositePass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;

    // A scene smaller than the viewport is left in the free scratch at its own size, and
    // GLUpscalePass widens it into the window; the scene's color stays colorSrc for what reads it
    // after the passes.
    const bool upscaled = view.sceneWidth() < view.viewportWidth || view.sceneHeight() < view.viewportHeight;
    if (upscaled) {
        ctx.colorDst->bind(ctx.gl);
        ctx.upscaleSource = ctx.colorDst;
    } else {
        bindBackbufferViewport(ctx);
    }
    ctx.gl.setDepthTest(false);

    // No table, or none of its color, skips the lookup rather than reading an identity.
    adoptGrading(view.settings->gradingLut);
    Display display;
    display.tonemap         = view.settings->tonemap;
    display.exposure        = std::exp2(ctx.exposure);
    display.bloomStrength   = ctx.bloomReady ? GLBloomPass::STRENGTH : 0.0f;
    display.gradingStrength = m_grading ? std::clamp(view.settings->gradingStrength, 0.0f, 1.0f) : 0.0f;
    display.mode            = view.display.mode;
    display.dither          = !upscaled;
    bindDisplay(m_shader, display, ctx.shadowAtlas);
    ctx.colorSrc->bindTexture(GLTarget::Attachment::Color, GLBindings::CompositeTextureSlots::SCENE);
    ctx.bloom.bind(GLBindings::CompositeTextureSlots::BLOOM);
    if (display.gradingStrength > 0.0f) m_grading->bindSlot(GLBindings::CompositeTextureSlots::GRADING);

    // The debug views sample the intermediate buffers.
    if (display.mode != RenderMode::Lit) {
        ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);
        ctx.sceneHDR.bindTexture(GLTarget::Attachment::GBuffer, GLBindings::PostTextureSlots::SCENE_GBUFFER);
        bindAO(ctx, m_shader);
        bindFog(ctx, m_shader);
    }

    ctx.screenTri.draw();
}

void GLCompositePass::bindDisplay(
    Vkm::OpenGL::Shader& shader,
    const Display& display,
    const GLShadowAtlas& atlas
) {
    shader.bind();
    shader.setUniform1i("u_tonemap", static_cast<int>(display.tonemap));
    shader.setUniform1f("u_exposure", display.exposure);
    shader.setUniform1f("u_bloomStrength", display.bloomStrength);
    shader.setUniform1f("u_gradingStrength", display.gradingStrength);
    shader.setUniform1i("u_renderMode", static_cast<int>(display.mode));
    shader.setUniform1i("u_dither", display.dither ? 1 : 0);
    atlas.bind2DRaw(GLBindings::ShadowTextureSlots::ATLAS_2D_RAW);
}

} // namespace Vkm::Engine
