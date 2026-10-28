#include "pass/gl_loading_pass.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_screen_triangle.h"
#include "convention/gl_bindings.h"
#include "system/render/render_backend.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

/// The most seconds of a frame the water catches up on: a frame held up does not throw the ripples
/// about.
constexpr float LONGEST_FRAME = 0.1f;

/// The water is stepped at this rate, a second, whatever the frames' rate: its ripples run as fast on
/// any screen, and a step is short enough to keep them steady.
constexpr float STEPS_A_SECOND = 120.0f;

/// The grid's rows; its columns follow the screen's shape, so a cell is square.
constexpr int CELLS_HIGH = 360;

/// The grid's most columns: a cell is square on a screen up to eight times as wide as high, and on a
/// strip wider still, where no one sees it, stretches - so the grid fits any backend's textures.
constexpr int MOST_COLUMNS = CELLS_HIGH * 8;
static_assert(MOST_COLUMNS <= static_cast<int>(BACKEND_MIN_TEXTURE_EDGE), "the grid fits any backend");

/// The current, in screen heights a second: slowly across, a little up.
const glm::vec2 CURRENT{0.035f, 0.008f};

/// The grid's columns for a screen @p aspect wide over its height: a cell is square, up to MOST_COLUMNS.
int columnsFor(float aspect) {
    const float columns = std::clamp(CELLS_HIGH * aspect, 1.0f, static_cast<float>(MOST_COLUMNS));
    return static_cast<int>(std::lround(columns));
}

} // namespace

GLLoadingPass::GLLoadingPass()
    : m_step("shaders/loading/water_step")
    , m_water("shaders/loading/water")
{}

GLLoadingPass::~GLLoadingPass() = default;

void GLLoadingPass::makeWater(float aspect) {
    m_cells = glm::ivec2(columnsFor(aspect), CELLS_HIGH);
    const std::vector<float> still(static_cast<size_t>(m_cells.x) * static_cast<size_t>(m_cells.y) * 2, 0.0f);
    Vkm::GL::Texture2DParams params;
    params.width          = m_cells.x;
    params.height         = m_cells.y;
    params.internalFormat = GL_RG32F;
    params.format         = GL_RG;
    params.type           = GL_FLOAT;
    params.minFilter      = Vkm::GL::TextureMinFilter::Linear;
    params.wrapS          = Vkm::GL::TextureWrap::Repeat;
    params.wrapT          = Vkm::GL::TextureWrap::Repeat;
    params.data           = still.data();
    for (auto& grid : m_grid) grid = std::make_unique<Vkm::GL::Texture2D>("loading_water", params);
    m_current = 0;
    m_owed    = 0.0f;
}

void GLLoadingPass::step(float seconds, const glm::vec4& path, float push) {
    const glm::vec2 drift = CURRENT * static_cast<float>(CELLS_HIGH);
    const int       next  = 1 - m_current;
    m_step.bind();
    m_step.setUniform1f("u_step", seconds);
    m_step.setUniform2fv("u_drift", drift);
    m_step.setUniform4fv("u_stir", path);
    m_step.setUniform1f("u_push", push);
    m_grid[m_current]->bindSlot(GLBindings::OverlayTextureSlots::LOADING_WATER);
    m_grid[next]->bindImage(0, GL_WRITE_ONLY);
    const auto group = static_cast<int>(GLBindings::ComputeGroups::IMAGE);
    m_step.dispatch(
        static_cast<uint32_t>((m_cells.x + group - 1) / group),
        static_cast<uint32_t>((m_cells.y + group - 1) / group)
    );
    Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    m_current = next;
}

void GLLoadingPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    const bool shows = view.loadingBackdropShows();
    const bool fresh = shows && !m_shown;
    m_shown = shows;
    if (!shows) return;

    // Over the viewport, as the screen's bar and line are.
    bindBackbufferViewport(ctx);
    ctx.gl.setDepthTest(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_water.bind();
    m_water.setUniform1f("u_opacity", view.loadingOpacity);
    m_water.setUniform1i("u_black", view.loadingWater ? 0 : 1);
    if (!view.loadingWater) {
        ctx.screenTri.draw();
        return;
    }

    const float width  = static_cast<float>(std::max(view.viewportWidth, 1u));
    const float height = static_cast<float>(std::max(view.viewportHeight, 1u));
    const float aspect = width / height;
    if (fresh || m_cells.x != columnsFor(aspect)) {
        makeWater(aspect);
        m_pointer        = glm::vec2(-1.0f);
        m_pointerSeconds = 0.0f;
    }
    const float seconds = fresh ? 0.0f : std::min(view.frameSeconds, LONGEST_FRAME);
    m_time = fresh ? 0.0f : m_time + seconds;

    // Whole steps only.
    m_owed += seconds;
    const int steps = static_cast<int>(m_owed * STEPS_A_SECOND);
    m_owed -= static_cast<float>(steps) / STEPS_A_SECOND;

    // The pointer over the screen, in cells; its way since a step last took it is the path the steps
    // share out and push along, the faster it moved the harder. A frame of no step leaves it whole.
    const glm::vec2 corner(static_cast<float>(view.viewportX), static_cast<float>(view.viewportY));
    const glm::vec2 local = view.pointer - corner;
    const bool      over  = local.x >= 0.0f && local.y >= 0.0f && local.x < width && local.y < height;
    const glm::vec2 cell  = glm::vec2(local.x / width, 1.0f - local.y / height) * glm::vec2(m_cells);
    const glm::vec2 here  = over ? cell : glm::vec2(-1.0f);
    m_pointerSeconds += seconds;
    const bool      moved = over && m_pointer.x >= 0.0f && m_pointerSeconds > 0.0f;
    const float     push  = moved ? glm::distance(m_pointer, here) / m_pointerSeconds : 0.0f;
    const glm::vec2 from  = m_pointer;
    if (steps > 0 || !over) {
        m_pointer        = here;
        m_pointerSeconds = 0.0f;
    }
    for (int i = 0; i < steps; ++i) {
        glm::vec4 path(-1.0f);
        if (moved) {
            const glm::vec2 a = glm::mix(from, here, static_cast<float>(i) / static_cast<float>(steps));
            const glm::vec2 b = glm::mix(from, here, static_cast<float>(i + 1) / static_cast<float>(steps));
            path = glm::vec4(a, b);
        }
        step(1.0f / STEPS_A_SECOND, path, push);
    }

    m_grid[m_current]->bindSlot(GLBindings::OverlayTextureSlots::LOADING_WATER);
    const glm::vec2 flow = CURRENT * m_time;
    m_water.bind();
    m_water.setUniform1f("u_aspect", aspect);
    m_water.setUniform1f("u_time", m_time);
    m_water.setUniform2fv("u_flow", flow);
    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
