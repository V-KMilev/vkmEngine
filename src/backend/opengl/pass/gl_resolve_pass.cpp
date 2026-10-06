#include "pass/gl_resolve_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "gl_screen_triangle.h"

namespace Vkm::Engine {

namespace {

namespace Post         = GLBindings::PostTextureSlots;
namespace ReflectSlots = GLBindings::ReflectionTextureSlots;

using Attachment = GLTarget::Attachment;

constexpr const char* GEOMETRY_SHADER = "shaders/resolve/geometry";
constexpr const char* COLOR_SHADER    = "shaders/resolve/color";

} // namespace

GLResolvePass::GLResolvePass(Scope scope)
    : m_scope(scope)
    , m_shader(scope == Scope::Geometry ? GEOMETRY_SHADER : COLOR_SHADER) {}

GLResolvePass::~GLResolvePass() = default;

void GLResolvePass::execute(GLFrameContext& ctx) {
    // MSAA off: the render and resolved targets are one object.
    if (&ctx.sceneRender == &ctx.sceneHDR) return;

    // Depth is written through gl_FragDepth, which needs the test on to land.
    ctx.gl.setDepthFunc(GL_ALWAYS);
    m_shader.bind();

    if (m_scope == Scope::Geometry) {
        ctx.sceneHDR.bindGBufferPass(ctx.gl);
        ctx.sceneRender.bindTexture(Attachment::Depth,   Post::SCENE_DEPTH);
        ctx.sceneRender.bindTexture(Attachment::GBuffer, Post::SCENE_GBUFFER);
    } else {
        ctx.sceneHDR.bindForwardPass(ctx.gl);
        // Only alpha-masked geometry writes depth after the geometry resolve;
        // a frame with none keeps the depth that resolve wrote.
        if (ctx.alphaMask.empty()) ctx.gl.setDepthTest(false);
        m_shader.setUniform1i("u_samples", static_cast<int>(ctx.sceneRender.samples()));
        m_shader.setUniform1i("u_reflectInputs", ctx.sceneRender.hasReflectInputs() ? 1 : 0);
        ctx.sceneRender.bindTexture(Attachment::Color,         Post::SCENE_COLOR);
        ctx.sceneRender.bindTexture(Attachment::Depth,         Post::SCENE_DEPTH);
        ctx.sceneRender.bindTexture(Attachment::ReflectWeight, ReflectSlots::WEIGHT);
        ctx.sceneRender.bindTexture(Attachment::ReflectEnv,    ReflectSlots::ENV);
    }

    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
