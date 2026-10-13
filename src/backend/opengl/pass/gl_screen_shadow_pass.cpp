#include "pass/gl_screen_shadow_pass.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_error_handle.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "frame/gl_shadow_data.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "ecs/component/render/camera.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

// The shader's group: one wavefront of pixels along a ray.
constexpr int WAVE_SIZE = static_cast<int>(GLBindings::ComputeGroups::SCREEN_SHADOW);

/// The farthest a light is placed off the screen, in pixels (buildDispatches).
constexpr float FAR_PIXELS = 1.0e6f;

// An orthographic camera's thickness is a default perspective camera's, at the distance that shows
// the same height.
const float ORTHO_AS_FIELD_OF_VIEW = Camera{}.fovY;

/**
 * @brief One dispatch of the shader: a rectangle of wavefronts on one side of the light.
 */
struct WaveDispatch {
    /// Blocks of WAVE_SIZE x WAVE_SIZE pixels across and down; each takes WAVE_SIZE groups.
    int waves[2]  = {0, 0};
    int offset[2] = {0, 0};  ///< Its first wave, in pixels from the light (u_waveOffset).
};

/**
 * @brief Every dispatch one light's shadow takes, and where the shader finds the light.
 */
struct WaveDispatches {
    glm::vec4 light{0.0f};  ///< u_light: pixel xy, depth, and +1 toward the light or -1 away.
    std::array<WaveDispatch, 8> dispatches{};
    int count = 0;
};

/**
 * @brief Bend Studio's BuildDispatchList (bend_sss_cpu.h, Apache-2.0; see the shader's header),
 *        for the whole scene.
 *
 * Four quadrants about the light's pixel, each a rectangle of wavefronts, split in two where it
 * is not square so the diagonal ray lands on a wave boundary: at most four dispatches with the
 * light off the screen, which lies in one or two of its quadrants, and up to eight with it on.
 *
 * Changed from the original: a light with w near zero - an orthographic camera's always, a
 * perspective one's when the sun lies in the image plane - is placed FAR_PIXELS out for its depth
 * and its side too, a far point light along the sun's direction, rather than at depth zero.
 *
 * @param lightClip The direction to the light (w = 0) through the view-projection, before the divide.
 * @param width     The scene's width in pixels.
 * @param height    Its height.
 * @return The dispatches, all of the same shader with no barrier between.
 */
WaveDispatches buildDispatches(glm::vec4 lightClip, int width, int height) {
    WaveDispatches result;

    // Past FAR_PIXELS a float no longer holds the light's pixel, nor a ray's step from it, to a pixel.
    const float reach = 0.5f * std::max(std::abs(lightClip.x) * width, std::abs(lightClip.y) * height);
    const float least = std::max(reach / FAR_PIXELS, 1e-12f);
    if (std::abs(lightClip.w) < least) lightClip.w = lightClip.w < 0.0f ? -least : least;

    // GL's pixel rows run up, as its clip y does; the depth is the eye's, reversed (gl_depth.h).
    result.light.x = (lightClip.x / lightClip.w * 0.5f + 0.5f) * static_cast<float>(width);
    result.light.y = (lightClip.y / lightClip.w * 0.5f + 0.5f) * static_cast<float>(height);
    result.light.z = GLDepth::eyeDepth(lightClip.z, lightClip.w);
    result.light.w = lightClip.w > 0.0f ? 1.0f : -1.0f;

    const int lightX = static_cast<int>(result.light.x + 0.5f);
    const int lightY = static_cast<int>(result.light.y + 0.5f);

    // The scene's bounds about the light, inclusive.
    const int biased[4] = {-lightX, lightY - height, width - lightX, lightY};

    // GL's rows run up, so 0 = top left, 1 = top right, 2 = bottom left, 3 = bottom right of the light.
    for (int q = 0; q < 4; ++q) {
        const bool vertical = q == 0 || q == 3;
        const int bounds[4] = {
            std::max(0, (q & 1) ? biased[0] : -biased[2]) / WAVE_SIZE,
            std::max(0, (q & 2) ? biased[1] : -biased[3]) / WAVE_SIZE,
            std::max(0, ((q & 1) ? biased[2] : -biased[0]) + WAVE_SIZE * (vertical ? 1 : 2) - 1) / WAVE_SIZE,
            std::max(0, ((q & 2) ? biased[3] : -biased[1]) + WAVE_SIZE * (vertical ? 2 : 1) - 1) / WAVE_SIZE,
        };
        if (bounds[2] - bounds[0] <= 0 || bounds[3] - bounds[1] <= 0) continue;

        const int biasX = (q == 2 || q == 3) ? 1 : 0;
        const int biasY = (q == 1 || q == 3) ? 1 : 0;

        WaveDispatch& disp = result.dispatches[static_cast<size_t>(result.count++)];
        disp.waves[0]  = bounds[2] - bounds[0];
        disp.waves[1]  = bounds[3] - bounds[1];
        disp.offset[0] = ((q & 1) ? bounds[0] : -bounds[2]) + biasX;
        disp.offset[1] = ((q & 2) ? -bounds[3] : bounds[1]) + biasY;

        // Where the diagonal ray from the light meets the quadrant's far corner.
        int axisDelta = biased[0] - biased[1];
        if (q == 1) axisDelta = biased[2] + biased[1];
        if (q == 2) axisDelta = -biased[0] - biased[3];
        if (q == 3) axisDelta = -biased[2] + biased[3];
        axisDelta = (axisDelta + WAVE_SIZE - 1) / WAVE_SIZE;
        if (axisDelta <= 0) continue;

        WaveDispatch& split = result.dispatches[static_cast<size_t>(result.count++)];
        split = disp;
        if (q == 0) {
            // Split on y; the split grows a wave on x.
            split.waves[1]   = std::min(disp.waves[1], axisDelta);
            disp.waves[1]   -= split.waves[1];
            split.offset[1]  = disp.offset[1] + disp.waves[1];
            split.offset[0] -= 1;
            split.waves[0]  += 1;
        } else if (q == 1) {
            // Split on x; the split grows a wave on y.
            split.waves[0]  = std::min(disp.waves[0], axisDelta);
            disp.waves[0]  -= split.waves[0];
            split.offset[0] = disp.offset[0] + disp.waves[0];
            split.waves[1] += 1;
        } else if (q == 2) {
            split.waves[0]   = std::min(disp.waves[0], axisDelta);
            disp.waves[0]   -= split.waves[0];
            disp.offset[0]  += split.waves[0];
            split.waves[1]  += 1;
            split.offset[1] -= 1;
        } else {
            split.waves[1]  = std::min(disp.waves[1], axisDelta);
            disp.waves[1]  -= split.waves[1];
            disp.offset[1] += split.waves[1];
            split.waves[0] += 1;
        }

        // An empty half goes: the last entry takes its place.
        if (split.waves[0] <= 0 || split.waves[1] <= 0) {
            split = result.dispatches[static_cast<size_t>(--result.count)];
        }
        if (disp.waves[0] <= 0 || disp.waves[1] <= 0) {
            disp = result.dispatches[static_cast<size_t>(--result.count)];
        }
    }

    for (int i = 0; i < result.count; ++i) {
        result.dispatches[static_cast<size_t>(i)].offset[0] *= WAVE_SIZE;
        result.dispatches[static_cast<size_t>(i)].offset[1] *= WAVE_SIZE;
    }
    return result;
}

} // namespace

GLScreenShadowPass::GLScreenShadowPass()
    : m_shader("shaders/screen_shadow") {}

GLScreenShadowPass::~GLScreenShadowPass() = default;

void GLScreenShadowPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (!view.settings.screenShadows) return;

    // The cascaded sun's alone, as the forward pass applies it: a light that casts no shadow casts
    // none here either.
    const int sun = ctx.shadowData.sunLight();
    if (sun < 0) return;
    const LightData& key = view.lights[static_cast<size_t>(sun)];

    const int width  = static_cast<int>(view.sceneWidth());
    const int height = static_cast<int>(view.sceneHeight());

    // Without GTAO the target is this pass's alone: unoccluded, the bent normal straight out of
    // the screen, every pixel lit until a wavefront writes it.
    if (!ctx.aoReady) {
        ctx.ao.resize(view.sceneWidth(), view.sceneHeight());
        ctx.ao.bind(ctx.gl);
        const GLfloat open[4] = {1.0f, 0.5f, 0.5f, 1.0f};
        VKM_GL_CHECK(glClearBufferfv(GL_COLOR, 0, open));
    }

    const CameraData& camera = view.camera;
    const WaveDispatches waves = buildDispatches(
        camera.viewProjection * glm::vec4(-key.direction, 0.0f),
        width,
        height
    );

    // A perspective camera's thickness follows each depth's distance to the far plane; an
    // orthographic one's depth is linear, so it is one span for the whole view.
    const bool  orthographic = camera.projection[3][3] != 0.0f;
    const float viewHeight   = 2.0f / camera.projection[1][1];
    const float asDistance   = viewHeight / (2.0f * std::tan(0.5f * ORTHO_AS_FIELD_OF_VIEW));
    const float orthoSpan    = orthographic ? asDistance / (camera.zFar - camera.zNear) : 0.0f;

    m_shader.bind();
    m_shader.setUniform4fv("u_light", waves.light);
    m_shader.setUniform1f("u_orthoSpan", orthoSpan);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);
    ctx.ao.bindImage(GLTarget::Attachment::Color, 0, GL_READ_WRITE);

    // GTAO's denoise wrote the target through an image.
    VKM_GL_CHECK(glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT));
    for (int i = 0; i < waves.count; ++i) {
        const WaveDispatch& d = waves.dispatches[static_cast<size_t>(i)];
        m_shader.setUniform2i("u_waveOffset", d.offset[0], d.offset[1]);
        m_shader.dispatch(
            static_cast<uint32_t>(WAVE_SIZE),
            static_cast<uint32_t>(d.waves[0]),
            static_cast<uint32_t>(d.waves[1])
        );
    }
    VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));

    ctx.screenShadowReady = true;
}

} // namespace Vkm::Engine
