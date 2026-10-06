#include "pass/gl_shadow_pass.h"

#include <GL/glew.h>

#include "gl_context.h"
#include "gl_shader.h"
#include "gl_error_handle.h"

#include "gl_frame_context.h"
#include "gl_view.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "convention/gl_bindings.h"
#include "debug/profiler.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_shadow_data.h"
#include "frame/gl_skin_palette.h"
#include "storage/gl_shadow_atlas.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

/// TileDraws::first of a tile or face held from an earlier frame, so not drawn.
constexpr uint32_t HELD = UINT32_MAX;

} // namespace

GLShadowPass::GLShadowPass()
    : m_programs{
        {Vkm::GL::Shader("shaders/shadow/depth")},
        {Vkm::GL::Shader("shaders/shadow/depth_skinned")},
        {Vkm::GL::Shader("shaders/shadow/depth_masked")},
        {Vkm::GL::Shader("shaders/shadow/depth_masked_skinned")}
    }
{}

GLShadowPass::~GLShadowPass() = default;

void GLShadowPass::execute(GLFrameContext& ctx) {
    if (ctx.shadowData.jobs2D().empty() && ctx.shadowData.jobsCube().empty()) return;

    m_tilesDrawn = 0;
    if (!uploadCasters(ctx)) {
        PROFILE_PLOT("Shadow/TilesDrawn", static_cast<int64_t>(m_tilesDrawn));
        return;
    }

    ctx.gl.setDepthFunc(GL_LESS);

    // Uploaded before the pass loop; a frame that posed nothing has none.
    if (ctx.skinPalette.count() > 0) ctx.skinPalette.bind();

    // Reset per frame: other passes bind their own programs, and a hot reload can replace the GL
    // program behind one of these objects without the object moving.
    m_bound = nullptr;

    render2D(ctx);
    renderCube(ctx);
    PROFILE_PLOT("Shadow/TilesDrawn", static_cast<int64_t>(m_tilesDrawn));
}

bool GLShadowPass::uploadCasters(GLFrameContext& ctx) {
    PROFILE_SCOPE("ShadowCasters/Upload");
    const GLShadowData& plan = ctx.shadowData;

    // Which tiles and faces draw is asked once, here. The atlas records a tile's contents when
    // render2D and renderCube clear it, so every tile not held is cleared below, casters or none.
    m_list.clear();
    m_draws.clear();
    std::vector<uint32_t>& instances = m_list.instances();
    const auto place = [&](const ShadowCasterBatch& batch, bool held) {
        if (held) return TileDraws{HELD, HELD};
        const uint32_t first = static_cast<uint32_t>(instances.size());
        instances.insert(instances.end(), batch.order.begin(), batch.order.end());
        ++m_tilesDrawn;
        return addCasters(ctx, batch, first);
    };

    const std::vector<Shadow2DJob>& jobs2D = plan.jobs2D();
    m_tiles2D.resize(jobs2D.size());
    for (size_t j = 0; j < jobs2D.size(); ++j) {
        const ShadowCasterBatch& batch = plan.batch2D(j);
        m_tiles2D[j] = place(batch, ctx.shadowAtlas.tileHolds(jobs2D[j].slot, batch.signature));
    }
    const std::vector<ShadowCubeJob>& jobsCube = plan.jobsCube();
    m_tilesCube.resize(jobsCube.size() * 6);
    for (size_t j = 0; j < jobsCube.size(); ++j) {
        for (uint32_t f = 0; f < 6; ++f) {
            const ShadowCasterBatch& batch = plan.batchCube(j, f);
            m_tilesCube[j * 6 + f] = place(
                batch,
                ctx.shadowAtlas.faceHolds(jobsCube[j].slot, f, batch.signature)
            );
        }
    }
    if (m_tilesDrawn == 0) return false;

    // Object indices; every transform went up once for the frame, so nothing else uploads here.
    m_list.upload();
    ctx.objects.bind();
    return true;
}

GLShadowPass::TileDraws GLShadowPass::addCasters(
    const GLFrameContext& ctx,
    const ShadowCasterBatch& batch,
    uint32_t first
) {
    const GLView&                  glView = ctx.resources;
    const std::vector<ObjectDraw>& draws  = ctx.view.objects->draws;

    // With no palette uploaded, no skinned program binds and casters draw stored vertices.
    const bool framePosed = ctx.skinPalette.count() > 0;

    std::vector<DrawCommand>& commands = m_list.commands();
    const uint32_t tileFirst = static_cast<uint32_t>(m_draws.size());
    for (const ShadowRun& run : batch.runs) {
        const ObjectDraw& draw = draws[batch.order[run.first]];
        const GLMesh*     mesh = glView.getMesh(draw.mesh);
        if (!mesh) continue;
        // A posed caster without a skin stream draws its stored vertices, as the static program does.
        const bool posed = framePosed && run.posed && mesh->isSkinned();

        // Every caster of a masked run shares its material (ShadowRun::key).
        const GLMaterial* material = glView.getMaterial(draw.material);
        const GLMaterial* cutout   = material && material->getType() == MaterialType::AlphaMask
            ? material
            : nullptr;
        const uint32_t    program  = (cutout ? 2 : 0) + (posed ? 1 : 0);

        const ShadowDraw* last = m_draws.size() > tileFirst ? &m_draws.back() : nullptr;
        if (!last || last->program != program || last->material != cutout
            || last->mesh->layout() != mesh->layout()) {
            m_draws.push_back({ mesh, cutout, program, static_cast<uint32_t>(commands.size()), 0 });
        }
        commands.push_back(mesh->command(run.count, first + run.first));
        ++m_draws.back().count;
    }
    return { tileFirst, static_cast<uint32_t>(m_draws.size()) };
}

void GLShadowPass::render2D(GLFrameContext& ctx) {
    const std::vector<Shadow2DJob>& jobs = ctx.shadowData.jobs2D();
    if (jobs.empty()) return;

    // A cascade's casters nearer the sun than its near plane are flattened onto
    // it rather than clipped (Shadow2DJob::cascade). Enabled raw: the Context
    // does not model depth clamping, and it is off again before the cubes.
    bool clamped = false;
    const auto clampDepth = [&](bool clamp) {
        if (clamp == clamped) return;
        if (clamp) VKM_GL_CHECK(glEnable(GL_DEPTH_CLAMP));
        else       VKM_GL_CHECK(glDisable(GL_DEPTH_CLAMP));
        clamped = clamp;
    };

    ctx.shadowAtlas.begin2D(ctx.gl);
    for (size_t j = 0; j < jobs.size(); ++j) {
        if (m_tiles2D[j].first == HELD) continue;
        clampDepth(jobs[j].cascade);
        ctx.shadowAtlas.beginTile(ctx.gl, jobs[j].slot, ctx.shadowData.batch2D(j).signature);
        beginTile(jobs[j].lightVP);
        drawTile(ctx.resources, m_tiles2D[j]);
    }
    clampDepth(false);
}

void GLShadowPass::renderCube(GLFrameContext& ctx) {
    const std::vector<ShadowCubeJob>& jobs = ctx.shadowData.jobsCube();

    for (size_t j = 0; j < jobs.size(); ++j) {
        const ShadowCubeJob& job = jobs[j];
        for (uint32_t f = 0; f < 6; ++f) {
            const TileDraws& tile = m_tilesCube[j * 6 + f];
            if (tile.first == HELD) continue;
            ctx.shadowAtlas.beginCubeFace(ctx.gl, job.slot, f, ctx.shadowData.batchCube(j, f).signature);
            beginTile(job.faceVP[f]);
            drawTile(ctx.resources, tile);
        }
    }
}

void GLShadowPass::beginTile(const glm::mat4& lightVP) {
    m_lightVP = lightVP;
    ++m_tile;
}

void GLShadowPass::drawTile(const GLView& glView, const TileDraws& tile) {
    PROFILE_SCOPE("ShadowCasters/Draw");
    const GLMaterial* boundMaterial = nullptr;
    for (uint32_t d = tile.first; d < tile.end; ++d) {
        const ShadowDraw& draw = m_draws[d];
        useProgram(m_programs[draw.program]);
        if (draw.material && draw.material != boundMaterial) {
            draw.material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            draw.material->bindTextures(glView);
            boundMaterial = draw.material;
        }
        m_list.draw(*draw.mesh, draw.first, draw.count);
    }
}

void GLShadowPass::useProgram(DepthProgram& program) {
    if (m_bound != &program.shader) {
        program.shader.bind();
        m_bound = &program.shader;
    }
    // Uniform state is per program, so each gets a tile's matrix the first time it draws it.
    if (program.tile != m_tile) {
        program.shader.setUniformMatrix4fv("u_lightVP", m_lightVP);
        program.tile = m_tile;
    }
}

} // namespace Vkm::Engine
