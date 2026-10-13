#include "frame/gl_occlusion_cull.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_context.h"
#include "gl_error_handle.h"

#include "gl_profiler.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "frame/gl_instance_batcher.h"
#include "frame/gl_stream_upload.h"
#include "resource/resource_manager.h"
#include "system/render/data/render_objects.h"

namespace Vkm::Engine {

namespace {

namespace ComputeGroups = GLBindings::ComputeGroups;

constexpr int SET_COUNT = static_cast<int>(GLOcclusionCull::Set::Count);

/**
 * @brief Make what the cull wrote visible to what reads it: the draws' commands and instances,
 *        the next cull's verdicts, and the next prepare()'s upload over the counts, or a test's
 *        read of them.
 */
void cullBarrier() {
    constexpr GLbitfield READERS = GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
        | GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT;
    VKM_GL_CHECK(glMemoryBarrier(READERS));
}

/// What a buffer ensureBytes() makes holds before anything writes it.
enum class Contents {
    Undefined,
    Zeroed
};

/**
 * @brief Make @p buffer hold @p bytes, replacing it with one half as large again when it is smaller.
 *
 * @param buffer   The buffer; a replacement drops what it held.
 * @param bytes    What it must hold.
 * @param contents What a replacement holds before anything writes it.
 * @param usage    A replacement's usage hint.
 */
void ensureBytes(
    std::unique_ptr<Vkm::GL::VertexBuffer>& buffer,
    uint32_t bytes,
    Contents contents,
    GLenum usage
) {
    if (buffer && bytes <= buffer->getSize()) return;
    const uint32_t capacity = bytes + bytes / 2;
    const std::vector<uint8_t> zeros(contents == Contents::Zeroed ? capacity : 0, 0);
    buffer = std::make_unique<Vkm::GL::VertexBuffer>(zeros.empty() ? nullptr : zeros.data(), capacity, usage);
}

} // namespace

GLOcclusionCull::GLOcclusionCull()
    : m_pyramidProgram("shaders/cull/pyramid")
    , m_cullProgram("shaders/cull/instances") {}

GLOcclusionCull::~GLOcclusionCull() = default;

bool GLOcclusionCull::prepare(
    const GLInstanceBatcher& batch,
    const RenderObjects& objects,
    const ResourceManager& resources
) {
    const GLDrawList&               list      = batch.list();
    const std::vector<uint32_t>&    instances = list.instances();
    const std::vector<DrawCommand>& commands  = list.commands();
    m_instanceCount = static_cast<uint32_t>(instances.size());
    m_commandCount  = static_cast<uint32_t>(commands.size());
    m_source        = list.instanceBuffer();
    if (m_instanceCount == 0 || m_commandCount == 0 || !m_source) return false;

    // A command is one mesh: its local box, empty for a run that leaves it.
    m_bounds.resize(m_commandCount);
    for (uint32_t c = 0; c < m_commandCount; ++c) {
        const ObjectDraw& draw   = objects.draws[instances[commands[c].baseInstance]];
        const MeshAsset*  mesh   = resources.tryGet(draw.mesh);
        const bool        always = !mesh || draw.skinCount > 0 || draw.waves;
        m_bounds[c].lo = glm::vec4(always ? glm::vec3(1.0f) : mesh->boundsMin, 0.0f);
        m_bounds[c].hi = glm::vec4(always ? glm::vec3(-1.0f) : mesh->boundsMax, 0.0f);
    }

    // Each set starts as the batch's commands with no instances.
    m_commands.resize(static_cast<size_t>(m_commandCount) * SET_COUNT);
    for (int set = 0; set < SET_COUNT; ++set) {
        std::copy(commands.begin(), commands.end(), m_commands.begin() + set * m_commandCount);
    }
    for (DrawCommand& command : m_commands) command.instanceCount = 0;

    growAndUpload(
        m_boundsBuffer,
        m_boundsCapacity,
        m_bounds.data(),
        static_cast<uint32_t>(m_bounds.size() * sizeof(CommandBounds))
    );
    growAndUpload(
        m_commandBuffer,
        m_commandCapacity,
        m_commands.data(),
        static_cast<uint32_t>(m_commands.size() * sizeof(DrawCommand))
    );

    // Written by the cull alone.
    const auto instanceBytes = static_cast<uint32_t>(m_instanceCount * SET_COUNT * sizeof(uint32_t));
    ensureBytes(m_instanceBuffer, instanceBytes, Contents::Undefined, GL_STREAM_DRAW);

    // A verdict an object; grown, every object starts unseen and is drawn by the second draw.
    const auto visibleBytes = static_cast<uint32_t>(objects.models.size() * sizeof(uint32_t));
    ensureBytes(m_visibleBuffer, visibleBytes, Contents::Zeroed, GL_DYNAMIC_DRAW);
    return true;
}

void GLOcclusionCull::bindBuffers() const {
    namespace SSBO = GLBindings::SSBOBindingPoints;
    VKM_GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO::CULL_BOUNDS, m_boundsBuffer->getID()));
    VKM_GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO::CULL_SOURCE, m_source->getID()));
    VKM_GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO::CULL_VISIBLE, m_visibleBuffer->getID()));
    VKM_GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO::CULL_COMMANDS, m_commandBuffer->getID()));
    VKM_GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO::CULL_INSTANCES, m_instanceBuffer->getID()));
    m_cullProgram.bind();
    m_cullProgram.setUniform1i("u_instanceCount", static_cast<int>(m_instanceCount));
    m_cullProgram.setUniform1i("u_commandCount", static_cast<int>(m_commandCount));
}

void GLOcclusionCull::cullSeen() {
    PROFILE_GPU_SCOPE("Occlusion/Seen");
    bindBuffers();
    m_cullProgram.setUniform1i("u_againstDepth", 0);
    m_cullProgram.dispatch(ComputeGroups::covering(m_instanceCount, ComputeGroups::CULL));
    cullBarrier();
}

void GLOcclusionCull::ensurePyramid(uint32_t width, uint32_t height) {
    const uint32_t w = std::max((width + 1) / 2, 1u);
    const uint32_t h = std::max((height + 1) / 2, 1u);
    if (m_pyramid && m_pyramid->getWidth() == w && m_pyramid->getHeight() == h) return;

    // Every level allocated, by the mip generation of an empty level 0.
    Vkm::GL::Texture2DParams params;
    params.width           = w;
    params.height          = h;
    params.internalFormat  = GL_R32F;
    params.format          = GL_RED;
    params.type            = GL_FLOAT;
    params.minFilter       = Vkm::GL::TextureMinFilter::NearestMipmapNearest;
    params.magFilter       = Vkm::GL::TextureMagFilter::Nearest;
    params.generateMipmaps = true;
    m_pyramid = std::make_unique<Vkm::GL::Texture2D>("cull_pyramid", params);
}

void GLOcclusionCull::cullAgainst(const GLTarget& scene, uint32_t width, uint32_t height) {
    ensurePyramid(width, height);
    buildPyramid(scene);

    PROFILE_GPU_SCOPE("Occlusion/Cull");
    bindBuffers();
    m_cullProgram.setUniform2f("u_sceneSize", static_cast<float>(width), static_cast<float>(height));
    m_cullProgram.setUniform1i("u_againstDepth", 1);
    m_cullProgram.dispatch(ComputeGroups::covering(m_instanceCount, ComputeGroups::CULL));
    cullBarrier();
}

void GLOcclusionCull::buildPyramid(const GLTarget& scene) {
    namespace Slots = GLBindings::CullTextureSlots;
    PROFILE_GPU_SCOPE("Occlusion/Pyramid");
    const uint32_t samples = scene.samples();
    scene.bindTexture(GLTarget::Attachment::Depth, samples > 1 ? Slots::DEPTH_MS : Slots::DEPTH);
    m_pyramid->bindSlot(Slots::PYRAMID);
    m_pyramidProgram.bind();
    m_pyramidProgram.setUniform1i("u_samples", static_cast<int>(samples));

    uint32_t w = m_pyramid->getWidth();
    uint32_t h = m_pyramid->getHeight();
    for (int level = 0; w > 0 && h > 0; ++level) {
        VKM_GL_CHECK(glBindImageTexture(0, m_pyramid->getID(), level, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F));
        m_pyramidProgram.setUniform1i("u_level", level);
        m_pyramidProgram.dispatch(
            ComputeGroups::covering(w, ComputeGroups::IMAGE),
            ComputeGroups::covering(h, ComputeGroups::IMAGE)
        );
        // The next level reads this one.
        VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));
        if (w == 1 && h == 1) break;
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
    }
}

void GLOcclusionCull::draw(Vkm::GL::Context& gl, const InstanceDraw& draw, Set set) const {
    const auto index = static_cast<uint32_t>(set);
    if (draw.mirrored) gl.setFrontFace(GL_CW);
    draw.mesh->pool().drawIndirect(
        draw.mesh->layout(),
        *m_instanceBuffer,
        *m_commandBuffer,
        index * m_commandCount + draw.first,
        draw.count,
        index * m_instanceCount
    );
    if (draw.mirrored) gl.setFrontFace(GL_CCW);
}

uint32_t GLOcclusionCull::countIn(Set set) const {
    if (!m_commandBuffer || m_commandCount == 0) return 0;
    std::vector<DrawCommand> commands(m_commandCount);
    const auto bytes  = static_cast<GLsizeiptr>(commands.size() * sizeof(DrawCommand));
    const auto first  = static_cast<size_t>(set) * m_commandCount;
    const auto offset = static_cast<GLintptr>(first * sizeof(DrawCommand));
    m_commandBuffer->bind(GL_COPY_READ_BUFFER);
    VKM_GL_CHECK(glGetBufferSubData(GL_COPY_READ_BUFFER, offset, bytes, commands.data()));
    uint32_t count = 0;
    for (const DrawCommand& command : commands) count += command.instanceCount;
    return count;
}

} // namespace Vkm::Engine
