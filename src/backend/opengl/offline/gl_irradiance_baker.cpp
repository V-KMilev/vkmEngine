#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "offline/gl_irradiance_baker.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <GL/glew.h>

#include "logger.h"

#include "gl_context.h"
#include "gl_profiler.h"
#include "gl_render_buffer.h"

#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "debug/profiler.h"
#include "offline/gl_cubemap.h"
#include "offline/gl_scene_capture.h"
#include "gl_view.h"
#include "storage/gl_ibl.h"
#include "system/render/probe_fill.h"
#include "system/render/render_backend.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

constexpr float CAPTURE_FAR = 500.0f;

/// Widest the atlas grows before it takes another row of tiles.
constexpr int MAX_ATLAS_WIDTH = static_cast<int>(BACKEND_MIN_TEXTURE_EDGE);

// The probes sit at texel centres, so a (worldPos - boxMin) / boxSize lookup interpolates them; the
// relight places them so too (shaders/irradiance/relight).
glm::vec3 probePosition(const IrradianceVolumeData& data, uint32_t cell) {
    const glm::uvec3 res(data.resolutionX, data.resolutionY, data.resolutionZ);
    const glm::uvec3 at(cell % res.x, (cell / res.x) % res.y, cell / (res.x * res.y));
    return data.center - data.halfExtents + data.halfExtents * 2.0f * (glm::vec3(at) + 0.5f) / glm::vec3(res);
}

uint32_t cellCount(const IrradianceVolumeData& data) {
    return data.resolutionX * data.resolutionY * data.resolutionZ;
}

} // namespace

GLIrradianceBaker::GLIrradianceBaker(GLSceneCapture& capture)
    : m_relight("shaders/irradiance/relight")
    , m_fill("shaders/irradiance/fill")
    , m_capture(capture) {}

GLIrradianceBaker::~GLIrradianceBaker() = default;

void GLIrradianceBaker::ensureAtlas(uint32_t probes) {
    const auto tilesPerRow = std::min<uint32_t>(probes, MAX_ATLAS_WIDTH / (6 * FACE_SIZE));
    const uint32_t rows    = (probes + tilesPerRow - 1) / tilesPerRow;
    const uint32_t width   = tilesPerRow * 6 * FACE_SIZE;
    const uint32_t height  = rows * FACE_SIZE;
    m_tilesPerRow = tilesPerRow;
    if (m_atlas && m_atlas->getWidth() == width && m_atlas->getHeight() == height) return;

    Vkm::GL::Texture2DParams params;
    params.width           = width;
    params.height          = height;
    params.internalFormat  = GL_RGBA32UI;
    params.format          = GL_RGBA_INTEGER;
    params.type            = GL_UNSIGNED_INT;
    params.minFilter       = Vkm::GL::TextureMinFilter::Nearest;
    params.magFilter       = Vkm::GL::TextureMagFilter::Nearest;
    params.generateMipmaps = false;
    m_atlas = std::make_unique<Vkm::GL::Texture2D>("irradiance_surfels", params);

    m_fbo.attachTexture(GL_COLOR_ATTACHMENT0, m_atlas->getID());
    m_fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void GLIrradianceBaker::update(
    Vkm::GL::Context& gl,
    GLIrradianceVolume& volume,
    const RenderView& view,
    GLView& glView,
    const GLIBL& globalIBL,
    const CaptureLighting& lighting
) {
    if (!view.hasIrradianceVolume) {
        release(volume);
        return;
    }
    const IrradianceVolumeData& data = view.irradianceVolume;
    const Signature want{
        data.center,
        data.halfExtents,
        data.resolutionX,
        data.resolutionY,
        data.resolutionZ,
        data.bakeVersion
    };
    const bool asked = want.bakeVersion != m_asked.last().bakeVersion;
    m_asked.see(want);
    const bool settled = asked || m_asked.settled() || !m_bakedFrom.baked();
    if (settled && m_bakedFrom.changed(want)) {
        beginBake(gl, volume, data);
        m_litBy.changed(lighting);
        m_relightAsked = false;
    }
    // A bake under way takes its slice before anything relights; the grid shown stays until it ends.
    if (m_capturing) {
        PROFILE_SCOPE("Render/IrradianceBake");
        PROFILE_GPU_SCOPE("Render/IrradianceBake");
        captureSlice(gl, volume, view, glView, globalIBL, view.bakeBudget);
        return;
    }

    // The ask holds until a relight starts. Only a ready grid of the bake's box relights; the bake
    // takes any other once it holds still, and a relight cut short by the box moving asks again.
    if (m_litBy.changed(lighting)) m_relightAsked = true;
    if (!m_relightAsked && !relighting()) return;
    if (!volume.isReady() || !want.sameGrid(m_bakedFrom.last())) {
        if (relighting()) m_relightAsked = true;
        m_nextBounce = 0;
        return;
    }
    PROFILE_SCOPE("Render/IrradianceRelight");
    PROFILE_GPU_SCOPE("Render/IrradianceRelight");
    if (!relighting()) m_relightAsked = false;
    relight(gl, volume, data, view, glView, globalIBL);
}

void GLIrradianceBaker::release(GLIrradianceVolume& volume) {
    if (!m_atlas && !volume.isReady()) return;
    // Detached first: a texture a framebuffer still holds outlives its delete.
    m_fbo.attachTexture(GL_COLOR_ATTACHMENT0, 0);
    m_fbo.attachRenderBuffer(GL_DEPTH_ATTACHMENT, 0);
    m_captureDepth.reset();
    m_capturing = false;
    m_atlas.reset();
    for (GLIrradianceVolume& scratch : m_scratch) scratch.release();
    volume.release();
    m_bakedFrom.invalidate();
    m_nextBounce   = 0;
    m_relightAsked = false;
}

void GLIrradianceBaker::beginBake(
    Vkm::GL::Context& gl,
    GLIrradianceVolume& volume,
    const IrradianceVolumeData& data
) {
    m_nextBounce = 0;
    m_capturing  = false;
    m_baking     = data;
    m_cells      = cellCount(data);
    m_captured   = 0;
    if (m_cells == 0) {
        volume.setUnbaked();
        return;
    }

    for (GLIrradianceVolume& scratch : m_scratch) {
        scratch.resize(data.resolutionX, data.resolutionY, data.resolutionZ);
    }
    ensureAtlas(m_cells);

    m_captureDepth = std::make_unique<Vkm::GL::RenderBuffer>();
    m_captureDepth->storage(
        GLDepth::EYE_FORMAT,
        static_cast<int32_t>(m_atlas->getWidth()),
        static_cast<int32_t>(m_atlas->getHeight())
    );

    // Once for every tile: zero is the sky, and each face's depth is its own tile's.
    m_fbo.attachRenderBuffer(GL_DEPTH_ATTACHMENT, m_captureDepth->getID());
    gl.setDepthWrite(true);
    const uint32_t sky[4] = {0, 0, 0, 0};
    m_fbo.clearColor(0, sky);
    m_fbo.clearDepth(GLDepth::EYE_CLEAR);
    m_capturing = true;
}

void GLIrradianceBaker::captureSlice(
    Vkm::GL::Context& gl,
    GLIrradianceVolume& volume,
    const RenderView& view,
    GLView& glView,
    const GLIBL& globalIBL,
    float budget
) {
    using Wall = std::chrono::steady_clock;
    const Wall::time_point until = Wall::now() + std::chrono::duration_cast<Wall::duration>(
        std::chrono::duration<float>(std::max(budget, 0.0f))
    );

    // The shadowed sun's map covers the volume's box: every probe sits inside it.
    const IrradianceVolumeData& data = m_baking;
    const Math::AABB box{data.center - data.halfExtents, data.center + data.halfExtents};
    m_capture.begin(gl, view, glView, globalIBL, static_cast<float>(FACE_SIZE), box);

    m_fbo.bind();
    gl.setDepthWrite(true);
    do {
        const uint32_t cell = m_captured++;
        const auto x = static_cast<int32_t>((cell % m_tilesPerRow) * 6 * FACE_SIZE);
        const auto y = static_cast<int32_t>((cell / m_tilesPerRow) * FACE_SIZE);
        const GLSceneCapture::AttachFace tile = [&](int face) {
            gl.setViewport(x + face * FACE_SIZE, y, FACE_SIZE, FACE_SIZE);
        };
        m_capture.captureSurfels(gl, probePosition(data, cell), CAPTURE_FAR, tile);
    } while (m_captured < m_cells && (budget <= 0.0f || Wall::now() < until));
    m_fbo.unbind();
    if (m_captured < m_cells) return;

    m_fbo.attachRenderBuffer(GL_DEPTH_ATTACHMENT, 0);
    m_captureDepth.reset();
    m_capturing = false;
    finishBake(volume, view, glView, globalIBL);
}

void GLIrradianceBaker::finishBake(
    GLIrradianceVolume& volume,
    const RenderView& view,
    GLView& glView,
    const GLIBL& globalIBL
) {
    const IrradianceVolumeData& data = m_baking;
    listLights(view, data, glView);
    if (!light(volume, data, globalIBL)) {
        LOG_WARNING(
            "Irradiance volume at (%.1f, %.1f, %.1f): every one of its %u probes is inside "
            "geometry. Not baked - move or shrink the volume.",
            data.center.x,
            data.center.y,
            data.center.z,
            m_cells
        );
        volume.setUnbaked();
        return;
    }
    // The grids the relights gather into trade with the frame's, so hold its size.
    for (GLIrradianceVolume& scratch : m_scratch) {
        scratch.resize(data.resolutionX, data.resolutionY, data.resolutionZ);
    }
    volume.setBaked(data);
    LOG_INFO(
        "Irradiance volume baked: %ux%ux%u probes at (%.1f, %.1f, %.1f), %d bounces; %u were inside "
        "geometry and are filled from their neighbours",
        data.resolutionX,
        data.resolutionY,
        data.resolutionZ,
        data.center.x,
        data.center.y,
        data.center.z,
        BOUNCES,
        m_refused
    );
}

void GLIrradianceBaker::relight(
    Vkm::GL::Context& gl,
    GLIrradianceVolume& volume,
    const IrradianceVolumeData& data,
    const RenderView& view,
    GLView& glView,
    const GLIBL& globalIBL
) {
    const Math::AABB box{data.center - data.halfExtents, data.center + data.halfExtents};
    m_capture.beginLighting(gl, view, glView, box);
    listLights(view, data, glView);
    gather(m_nextBounce, data, globalIBL);
    repair(m_scratch[m_nextBounce % 2]);
    if (++m_nextBounce < BOUNCES) return;

    m_nextBounce = 0;
    volume.swapGrid(m_scratch[(BOUNCES - 1) % 2]);
}

bool GLIrradianceBaker::light(
    GLIrradianceVolume& volume,
    const IrradianceVolumeData& data,
    const GLIBL& globalIBL
) {
    for (int bounce = 0; bounce < BOUNCES; ++bounce) {
        gather(bounce, data, globalIBL);
        if (bounce == 0 && recordRepair(m_scratch[0], data) == cellCount(data)) return false;
        repair(m_scratch[bounce % 2]);
    }
    volume.swapGrid(m_scratch[(BOUNCES - 1) % 2]);
    return true;
}

void GLIrradianceBaker::repair(GLIrradianceVolume& grid) {
    if (m_refused > 0) {
        m_fill.bind();
        m_fill.setUniform1i("u_targetCount", static_cast<int>(m_refused));
        m_repair->bindBase(GLBindings::SSBOBindingPoints::IRRADIANCE_FILL);
        for (int i = 0; i < GLIrradianceVolume::SH_COEFFS; ++i) {
            grid.bindImage(i, static_cast<uint32_t>(i), GL_READ_WRITE);
        }
        m_fill.dispatch(GLBindings::ComputeGroups::covering(m_refused, GLBindings::ComputeGroups::FILL));
    }
    // The next gather samples it, and the frame after the trade.
    Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void GLIrradianceBaker::gather(int bounce, const IrradianceVolumeData& data, const GLIBL& globalIBL) {
    GLIrradianceVolume&       target   = m_scratch[bounce % 2];
    const GLIrradianceVolume* previous = bounce > 0 ? &m_scratch[(bounce - 1) % 2] : nullptr;
    const bool                hasIBL   = globalIBL.isReady();

    m_relight.bind();
    m_relight.setUniform1i("u_faceSize", FACE_SIZE);
    m_relight.setUniform1i("u_tilesPerRow", static_cast<int>(m_tilesPerRow));
    m_relight.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    static const char* const FACE_TO_WORLD[6] = {
        "u_faceToWorld[0]",
        "u_faceToWorld[1]",
        "u_faceToWorld[2]",
        "u_faceToWorld[3]",
        "u_faceToWorld[4]",
        "u_faceToWorld[5]",
    };
    for (int face = 0; face < 6; ++face) {
        const glm::mat3 toWorld = glm::transpose(glm::mat3(GLCubemap::faceView(face, glm::vec3(0.0f))));
        m_relight.setUniformMatrix3fv(FACE_TO_WORLD[face], toWorld);
    }

    // At unit intensity and no fade: the frame scales the finished grid by its intensity, which a
    // gather would compound once per bounce, and a fade would let the sky onto a wall on the box's
    // face. The first gather places the box by the other grid, and reads none.
    IrradianceVolumeData lit = data;
    lit.intensity     = 1.0f;
    lit.blendDistance = 0.0f;
    (previous ? *previous : m_scratch[1]).bindForShading(m_relight, lit);
    if (!previous) m_relight.setUniform1i("u_hasIrradianceVolume", 0);
    if (hasIBL) {
        globalIBL.bindIrradiance(GLBindings::IBLTextureSlots::IRRADIANCE);
        globalIBL.bindEnvCube(GLBindings::IBLTextureSlots::ENV_CUBE);
    }
    m_atlas->bindSlot(GLBindings::IrradianceRelightSlots::SURFELS);
    for (int i = 0; i < GLIrradianceVolume::SH_COEFFS; ++i) {
        target.bindImage(i, static_cast<uint32_t>(i), GL_WRITE_ONLY);
    }
    m_relight.dispatch(cellCount(data));
    Vkm::GL::memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
}

namespace {

constexpr auto DIRECTIONAL_HEAD = static_cast<size_t>(GLIrradianceBaker::RELIGHT_DIRECTIONAL_HEAD);
constexpr auto OUTSIDE_HEAD     = static_cast<size_t>(GLIrradianceBaker::RELIGHT_OUTSIDE_HEAD);

/// Where @p cell's light count sits; where its list begins is the word after.
size_t cellHead(uint32_t cell) {
    return static_cast<size_t>(GLIrradianceBaker::RELIGHT_CELL_HEADS) + 2 * static_cast<size_t>(cell);
}

} // namespace

void GLIrradianceBaker::listLights(const RenderView& view, const IrradianceVolumeData& data, GLView& glView) {
    const glm::ivec3 grid(data.resolutionX, data.resolutionY, data.resolutionZ);
    const uint32_t   cells   = cellCount(data);
    const glm::vec3  boxMin  = data.center - data.halfExtents;
    const glm::vec3  boxMax  = data.center + data.halfExtents;
    const glm::vec3  cellLen = data.halfExtents * 2.0f / glm::vec3(grid);

    // Every (cell, light) a light's sphere meets, then a counting sort of them by cell.
    m_directional.clear();
    m_outside.clear();
    m_reaches.clear();
    const size_t count = std::min<size_t>(view.lights.size(), Config::MAX_LIGHTS);
    for (size_t i = 0; i < count; ++i) {
        const LightData& light = view.lights[i];
        const auto       index = static_cast<uint32_t>(i);
        if (light.type == LightType::Directional) {
            m_directional.push_back(index);
            continue;
        }
        const glm::vec3 lo = light.position - glm::vec3(light.radius);
        const glm::vec3 hi = light.position + glm::vec3(light.radius);
        if (glm::any(glm::lessThan(lo, boxMin)) || glm::any(glm::greaterThan(hi, boxMax))) {
            m_outside.push_back(index);
        }
        if (glm::any(glm::lessThan(hi, boxMin)) || glm::any(glm::greaterThan(lo, boxMax))) continue;

        // The cells its reach's box touches, then those its sphere does.
        const auto cellOf = [&](const glm::vec3& at) {
            return glm::clamp(glm::ivec3(glm::floor((at - boxMin) / cellLen)), glm::ivec3(0), grid - 1);
        };
        const glm::ivec3 first = cellOf(lo);
        const glm::ivec3 last  = cellOf(hi);
        for (int z = first.z; z <= last.z; ++z) {
            for (int y = first.y; y <= last.y; ++y) {
                for (int x = first.x; x <= last.x; ++x) {
                    const glm::vec3 cellMin = boxMin + cellLen * glm::vec3(x, y, z);
                    const glm::vec3 nearest = glm::clamp(light.position, cellMin, cellMin + cellLen);
                    const glm::vec3 apart   = nearest - light.position;
                    if (glm::dot(apart, apart) > light.radius * light.radius) continue;
                    m_reaches.push_back({static_cast<uint32_t>(x + grid.x * (y + grid.y * z)), index});
                }
            }
        }
    }

    // The heads, up to where a cell past the last would begin, then the lists.
    m_lightWords.assign(cellHead(cells), 0u);
    const auto append = [this](const std::vector<uint32_t>& list, size_t head) {
        m_lightWords[head]     = static_cast<uint32_t>(list.size());
        m_lightWords[head + 1] = static_cast<uint32_t>(m_lightWords.size());
        m_lightWords.insert(m_lightWords.end(), list.begin(), list.end());
    };
    append(m_directional, DIRECTIONAL_HEAD);
    append(m_outside, OUTSIDE_HEAD);

    // A cell's count, capped, then where its list begins, then the lists in light order.
    for (const auto& [cell, light] : m_reaches) {
        uint32_t& n = m_lightWords[cellHead(cell)];
        if (n < Config::MAX_LIGHTS_PER_CLUSTER) ++n;
    }
    auto next = static_cast<uint32_t>(m_lightWords.size());
    for (uint32_t cell = 0; cell < cells; ++cell) {
        m_lightWords[cellHead(cell) + 1] = next;
        next += m_lightWords[cellHead(cell)];
    }
    m_lightWords.resize(next);
    m_cellFill.assign(cells, 0u);
    for (const auto& [cell, light] : m_reaches) {
        const uint32_t n = m_lightWords[cellHead(cell)];
        uint32_t&      k = m_cellFill[cell];
        if (k < n) m_lightWords[m_lightWords[cellHead(cell) + 1] + k++] = light;
    }

    const auto bytes = static_cast<uint32_t>(m_lightWords.size() * sizeof(uint32_t));
    const Vkm::GL::BufferSpan lists = glView.stream().write(m_lightWords.data(), bytes);
    Vkm::GL::bindRange(GL_SHADER_STORAGE_BUFFER, GLBindings::SSBOBindingPoints::RELIGHT_LIGHTS, lists);
}

uint32_t GLIrradianceBaker::recordRepair(const GLIrradianceVolume& judged, const IrradianceVolumeData& data) {
    ProbeGridSH grid;
    judged.download(grid);
    const uint32_t cells = cellCount(data);
    std::vector<bool> trusted(cells);
    uint32_t refused = 0;
    for (uint32_t i = 0; i < cells; ++i) {
        trusted[i] = probeTrusted(grid[0][i]);
        if (!trusted[i]) ++refused;
    }
    m_refused = 0;
    if (refused == 0 || refused == cells) return refused;

    // A target per refused probe, three words, then its sources, two words each (see
    // shaders/irradiance/fill).
    const std::vector<ProbeFill> fills =
        probeFillWeights(trusted, data.resolutionX, data.resolutionY, data.resolutionZ);
    std::vector<uint32_t> words(3 * fills.size());
    for (size_t t = 0; t < fills.size(); ++t) {
        words[3 * t]     = fills[t].cell;
        words[3 * t + 1] = static_cast<uint32_t>(words.size());
        words[3 * t + 2] = static_cast<uint32_t>(fills[t].sources.size());
        for (const auto& [source, weight] : fills[t].sources) {
            uint32_t bits = 0;
            std::memcpy(&bits, &weight, sizeof(bits));
            words.push_back(source);
            words.push_back(bits);
        }
    }
    m_repair = std::make_unique<Vkm::GL::ShaderStorageBuffer>(
        words.data(),
        static_cast<uint32_t>(words.size() * sizeof(uint32_t)),
        GL_STATIC_DRAW
    );
    m_refused = static_cast<uint32_t>(fills.size());
    return refused;
}

} // namespace Vkm::Engine
