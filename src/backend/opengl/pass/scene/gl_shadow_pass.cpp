#include "pass/scene/gl_shadow_pass.h"

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"

#include "pass/gl_frame_context.h"
#include "asset/gl_asset_mirror.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "base/profile/profiler.h"
#include "frame/gl_object_buffer.h"
#include "frame/light/gl_shadow_plan.h"
#include "frame/gl_skin_palette.h"
#include "storage/light/gl_shadow_atlas.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

constexpr uint32_t HELD = UINT32_MAX;  ///< TileDraws::first of a tile or face held from an earlier frame, so
                                       ///< not drawn.

} // namespace

GLShadowPass::GLShadowPass()
    : m_programs{
        {Vkm::OpenGL::Shader("shaders/opengl/scene/shadow"), false},
        {Vkm::OpenGL::Shader("shaders/opengl/scene/shadow", {"SKINNED"}), false},
        {Vkm::OpenGL::Shader("shaders/opengl/scene/shadow/masked"), true},
        {Vkm::OpenGL::Shader("shaders/opengl/scene/shadow/masked", {"SKINNED"}), true}
    }
{}

GLShadowPass::~GLShadowPass() = default;

void GLShadowPass::execute(GLFrameContext& ctx) {
    // The plan forked the caster cull onto the pool; the frame's build and the passes before this
    // one ran beside it.
    {
        PROFILE_SCOPE("Shadow/Join");
        ctx.shadowPlan.finishCull();
    }
    if (ctx.shadowPlan.jobs2D().empty() && ctx.shadowPlan.jobsCube().empty()) return;

    m_tilesDrawn = 0;
    if (!uploadCasters(ctx)) {
        PROFILE_PLOT("Shadow/TilesDrawn", static_cast<int64_t>(m_tilesDrawn));
        return;
    }

    ctx.gl.setDepthFunc(GLDepth::SHADOW_TEST);

    // Uploaded before the pass loop; a frame that posed nothing has none.
    if (ctx.skinPalette.count() > 0) ctx.skinPalette.bind();

    // Reset per frame: other passes bind their own programs, and a hot reload can replace the GL
    // program behind one of these objects without the object moving.
    m_bound = nullptr;

    render2D(ctx);
    renderHeld(ctx);
    renderCube(ctx);
    PROFILE_PLOT("Shadow/TilesDrawn", static_cast<int64_t>(m_tilesDrawn));
}

bool GLShadowPass::uploadCasters(GLFrameContext& ctx) {
    PROFILE_SCOPE("ShadowCasters/Upload");
    const GLShadowPlan& plan = ctx.shadowPlan;

    // Which tiles and faces draw is asked once, here. The atlas records a tile's contents when
    // render2D and renderCube begin it, so every tile not held is drawn below, casters or none.
    m_list.clear();
    m_draws.clear();
    std::vector<uint32_t>& instances = m_list.instances();
    const auto take = [&](const ShadowCasterBatch& batch) {
        const uint32_t first = static_cast<uint32_t>(instances.size());
        instances.insert(instances.end(), batch.order.begin(), batch.order.end());
        ++m_tilesDrawn;
        return first;
    };
    const auto place = [&](const ShadowCasterBatch& batch, bool held) {
        if (held) return TileDraws{HELD, HELD};
        return addCasters(ctx, batch, take(batch));
    };

    const std::vector<Shadow2DJob>& jobs2D = plan.jobs2D();
    m_tiles2D.resize(jobs2D.size());
    for (size_t j = 0; j < jobs2D.size(); ++j) {
        const ShadowCasterBatch& batch = plan.batch2D(j);
        m_tiles2D[j] = place(batch, ctx.shadowAtlas.tileHolds(jobs2D[j].slot, batch.signature));
    }
    m_rectsHeld.resize(plan.jobsHeld().size());
    for (size_t j = 0; j < m_rectsHeld.size(); ++j) m_rectsHeld[j] = place(plan.batchHeld(j), false);
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
    m_list.upload(ctx.mirror.stream());
    ctx.objects.bind();
    return true;
}

GLShadowPass::TileDraws GLShadowPass::addCasters(
    const GLFrameContext& ctx,
    const ShadowCasterBatch& batch,
    uint32_t first
) {
    const GLAssetMirror& mirror = ctx.mirror;
    const std::vector<ObjectDraw>& draws = ctx.view.objects->draws;

    // With no palette uploaded, no skinned program binds and casters draw stored vertices.
    const bool framePosed = ctx.skinPalette.count() > 0;

    std::vector<Vkm::OpenGL::DrawElementsIndirectCommand>& commands = m_list.commands();
    const uint32_t tileFirst = static_cast<uint32_t>(m_draws.size());
    for (const ShadowRun& run : batch.runs) {
        const ObjectDraw& draw = draws[batch.order[run.first]];
        const GLMesh*     mesh = mirror.getMesh(draw.mesh);
        if (!mesh) continue;
        // A posed caster without a skin stream draws its stored vertices, as the static program does.
        const bool posed = framePosed && run.posed && mesh->isSkinned();

        // Every caster of a cut run shares its material (ShadowRun::key).
        const GLMaterial* material = mirror.getMaterial(draw.material);
        const GLMaterial* cut      = material && material->cutsItsShadow() ? material : nullptr;
        const uint32_t program = (cut ? 2 : 0) + (posed ? 1 : 0);

        const ShadowDraw* last = m_draws.size() > tileFirst ? &m_draws.back() : nullptr;
        if (!last || last->program != program || last->material != cut
            || last->mesh->layout() != mesh->layout()) {
            m_draws.push_back({ mesh, cut, program, static_cast<uint32_t>(commands.size()), 0 });
        }
        commands.push_back(mesh->command(run.count, first + run.first));
        ++m_draws.back().count;
    }
    return { tileFirst, static_cast<uint32_t>(m_draws.size()) };
}

void GLShadowPass::render2D(GLFrameContext& ctx) {
    const std::vector<Shadow2DJob>& jobs = ctx.shadowPlan.jobs2D();
    if (jobs.empty()) return;

    // What the sun lights nearer than its near plane is flattened onto it rather than clipped
    // (Shadow2DJob::cascade); it is off again before the cubes.
    const auto clampDepth = [&](bool clamp) { ctx.gl.setDepthClamp(clamp); };

    ctx.shadowAtlas.begin2D(ctx.gl);
    for (size_t j = 0; j < jobs.size(); ++j) {
        if (m_tiles2D[j].first == HELD) continue;
        const Shadow2DJob& job = jobs[j];
        const ShadowCasterBatch& batch = ctx.shadowPlan.batch2D(j);
        clampDepth(job.cascade);
        ctx.shadowAtlas.beginTile(ctx.gl, job.slot, batch.signature);
        // gl_FragCoord counts from the atlas's corner, the grid from the tile's.
        beginTile(job.lightVP, job.ditherOrigin - ctx.shadowAtlas.tileCorner(job.slot));
        drawTile(ctx.mirror, m_tiles2D[j]);
    }
    clampDepth(false);
}

void GLShadowPass::renderHeld(GLFrameContext& ctx) {
    const std::vector<ShadowHeldJob>& jobs = ctx.shadowPlan.jobsHeld();
    if (jobs.empty()) return;
    ctx.gl.setDepthClamp(true);
    for (size_t j = 0; j < jobs.size(); ++j) {
        ctx.shadowAtlas.beginHeld(ctx.gl, jobs[j].layer, jobs[j].rect);
        // A texel of the layer is a place in the light's grid, so gl_FragCoord keys the dither as it is.
        beginTile(jobs[j].lightVP, glm::vec2(0.0f));
        drawTile(ctx.mirror, m_rectsHeld[j]);
    }
    ctx.gl.setDepthClamp(false);
}

void GLShadowPass::renderCube(GLFrameContext& ctx) {
    const std::vector<ShadowCubeJob>& jobs = ctx.shadowPlan.jobsCube();

    for (size_t j = 0; j < jobs.size(); ++j) {
        const ShadowCubeJob& job = jobs[j];
        for (uint32_t f = 0; f < 6; ++f) {
            const TileDraws& tile = m_tilesCube[j * 6 + f];
            if (tile.first == HELD) continue;
            ctx.shadowAtlas.beginCubeFace(ctx.gl, job.slot, f, ctx.shadowPlan.batchCube(j, f).signature);
            beginTile(job.faceVP[f], glm::vec2(0.0f));
            drawTile(ctx.mirror, tile);
        }
    }
}

void GLShadowPass::beginTile(const glm::mat4& lightVP, const glm::vec2& ditherShift) {
    m_lightVP     = GLDepth::shadowProjection(lightVP);
    m_ditherShift = ditherShift;
    ++m_tile;
}

void GLShadowPass::drawTile(const GLAssetMirror& mirror, const TileDraws& tile) {
    PROFILE_SCOPE("ShadowCasters/Draw");
    const GLMaterial* boundMaterial = nullptr;
    for (uint32_t d = tile.first; d < tile.end; ++d) {
        const ShadowDraw& draw = m_draws[d];
        useProgram(m_programs[draw.program]);
        if (draw.material && draw.material != boundMaterial) {
            draw.material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            draw.material->bindTextures(mirror);
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
        if (program.cuts) program.shader.setUniform2f("u_ditherShift", m_ditherShift.x, m_ditherShift.y);
        program.tile = m_tile;
    }
}

} // namespace Vkm::Engine
