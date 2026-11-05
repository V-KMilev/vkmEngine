#include "frame/draw/gl_instance_batcher.h"

#include <algorithm>
#include <utility>

#include <glm/glm.hpp>

#include "vkm_opengl/core/context.h"

#include "gl_forward_programs.h"
#include "asset/gl_asset_mirror.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "base/profile/profiler.h"
#include "core/frame/render_objects.h"

namespace Vkm::Engine {

namespace {

// Program, winding, sides, then material, then mesh, so identical draws sort together and a run
// merges into one instanced call, and a pass that binds no material merges the runs of every
// one-sided material. A program is the skinned bit and the material's kind
// (GLForwardPrograms::kindOf). Ids are sparse-set slots, far below the material's 27 bits.
constexpr int SKINNED_BIT      = 63;
constexpr int KIND_SHIFT       = 61;
constexpr int MIRRORED_BIT     = 60;
constexpr int DOUBLE_SIDED_BIT = 59;

// The two bits at KIND_SHIFT: a GLForwardPrograms::Kind's value, or water's.
constexpr uint64_t KIND_WATER = 3;
constexpr uint64_t KIND_MASK  = 3;

uint64_t sortKey(
    const MaterialHandle& material,
    uint64_t kind,
    bool doubleSided,
    const MeshHandle& mesh,
    bool skinned,
    bool mirrored
) {
    return (static_cast<uint64_t>(skinned) << SKINNED_BIT)
        | (kind << KIND_SHIFT)
        | (static_cast<uint64_t>(mirrored) << MIRRORED_BIT)
        | (static_cast<uint64_t>(doubleSided) << DOUBLE_SIDED_BIT)
        | (static_cast<uint64_t>(material.id() & 0x07ffffffu) << 32)
        | static_cast<uint64_t>(mesh.id());
}

/**
 * @brief Whether @p draw draws through the skinned program this frame.
 *
 * The GPU layout must carry a skin stream and the frame must have posed the object. Asked only when
 * the frame has a palette.
 *
 * @param draw The object's draw.
 * @param mesh The mesh it draws (its level, or across a hand-over the next), null when not on the GPU.
 * @return True when the skinned program draws it.
 */
bool drawsSkinned(const ObjectDraw& draw, const GLMesh* mesh) {
    return draw.skinCount > 0 && mesh && mesh->isSkinned();
}

// The mesh @p instance of @p objects is drawn with, as @p meshes says.
const MeshHandle& meshOf(const RenderObjects& objects, uint32_t instance, MeshOf meshes) {
    const uint32_t object = instance & INSTANCE_OBJECT_MASK;
    if (meshes == MeshOf::Base) return objects.bases[object];
    return (instance & INSTANCE_FADES_IN) ? objects.fades[object].next : objects.draws[object].mesh;
}

/**
 * @brief The instances the eye draws @p object as: its level's, faded, and across a hand-over the
 *        next level's, on the samples the level gives up that it takes.
 *
 * A fade too near its start for a step draws whole. A hand-over to a level not on the GPU, or
 * between levels a lightmap reads apart (the object's lightmap is its level's), draws the level
 * alone, over the samples both would.
 *
 * @param object  Index of the object.
 * @param objects The objects it indexes.
 * @param mirror  GPU mirror the next level resolves against.
 * @return The level's instance, and the next level's or zero.
 */
std::pair<uint32_t, uint32_t> eyeInstances(
    uint32_t object,
    const RenderObjects& objects,
    const GLAssetMirror& mirror
) {
    // An instance of @p object giving up, or taking, @p share of the samples; 0 when no step's worth.
    const auto instanceOf = [object](float share) {
        const float clamped = glm::clamp(share, 0.0f, 1.0f);
        const auto  step    = static_cast<uint32_t>(clamped * static_cast<float>(INSTANCE_FADE_STEPS) + 0.5f);
        return step == 0 ? 0u : object | (std::min(step, INSTANCE_FADE_STEPS - 1) << INSTANCE_OBJECT_BITS);
    };
    const ObjectFade& fade  = objects.fades[object];
    const uint32_t    level = instanceOf(fade.amount);
    if (level == 0) return {object, 0u};
    const uint32_t next = instanceOf(fade.taken);
    if (!fade.next || next == 0) return {level, 0u};
    const ObjectDraw& draw = objects.draws[object];
    const MeshHandle& base = objects.bases[object];
    const bool apart = draw.fixed && mirror.readsChartsOf(draw.mesh, base)
        && !mirror.readsChartsOf(fade.next, base);
    if (!mirror.getMesh(fade.next) || apart) {
        const uint32_t alone = instanceOf(fade.amount - fade.taken);
        return {alone == 0 ? object : alone, 0u};
    }
    return {level, next | INSTANCE_FADES_IN};
}

// A transform that mirrors reverses every triangle's winding.
bool mirrors(const glm::mat4& model) {
    return glm::determinant(glm::mat3(model)) < 0.0f;
}

} // namespace

bool GLInstanceBatcher::beginBuild(const std::vector<uint32_t>& list) {
    m_draws.clear();
    m_list.clear();
    if (list.empty()) return false;

    m_list.instances().reserve(list.size());
    return true;
}

GLenum turnForMirror(Vkm::OpenGL::Context& gl, bool mirrored) {
    const GLenum front = gl.getFrontFace();
    if (mirrored) gl.setFrontFace(front == GL_CCW ? GL_CW : GL_CCW);
    return front;
}

void GLInstanceBatcher::draw(Vkm::OpenGL::Context& gl, const InstanceDraw& draw) const {
    const GLenum front = turnForMirror(gl, draw.mirrored);
    m_list.draw(*draw.mesh, draw.first, draw.count);
    gl.setFrontFace(front);
}

void GLInstanceBatcher::addRun(
    const GLMesh& mesh,
    const MaterialHandle& material,
    bool skinned,
    bool mirrored,
    bool water,
    uint32_t first,
    uint32_t count
) {
    std::vector<Vkm::OpenGL::DrawElementsIndirectCommand>& commands = m_list.commands();
    const InstanceDraw* last = m_draws.empty() ? nullptr : &m_draws.back();
    if (!last || last->material != material || last->skinned != skinned || last->mirrored != mirrored
        || last->water != water || last->mesh->layout() != mesh.layout()) {
        const uint32_t firstCommand = static_cast<uint32_t>(commands.size());
        m_draws.push_back({ &mesh, material, firstCommand, 0, skinned, mirrored, water });
    }
    commands.push_back(mesh.command(count, first));
    ++m_draws.back().count;
}

const std::vector<InstanceDraw>& GLInstanceBatcher::buildGrouped(
    const std::vector<uint32_t>& list,
    const RenderObjects& objects,
    const GLAssetMirror& mirror,
    uint32_t bones,
    MeshOf meshes
) {
    if (!beginBuild(list)) return m_draws;

    {
        PROFILE_SCOPE("InstanceBatch/Sort");
        const bool skinning = bones > 0;
        // A key an object, then one for each next level the eye's hand-overs draw.
        m_keying.resize(list.size());
        // Props are placed in runs, so a material is mostly the last one: it is found once a run.
        MaterialHandle    runMaterial;
        const GLMaterial* runGpu = nullptr;
        bool runSides = false;
        for (size_t i = 0; i < list.size(); ++i) {
            const uint32_t object  = list[i];
            const bool mirrored    = mirrors(objects.models[object]);
            const ObjectDraw& draw = objects.draws[object];
            if (draw.material != runMaterial) {
                runMaterial = draw.material;
                runGpu      = mirror.getMaterial(draw.material);
                runSides    = runGpu && runGpu->doubleSided();
            }
            const auto keyOf = [&](uint32_t instance) {
                const MeshHandle& mesh = meshOf(objects, instance, meshes);
                const bool skinned     = skinning && drawsSkinned(draw, mirror.getMesh(mesh));
                const uint64_t kind    = draw.water != 0
                    ? KIND_WATER
                    : static_cast<uint64_t>(GLForwardPrograms::kindOf(runGpu, skinned));
                return SortKey{ sortKey(draw.material, kind, runSides, mesh, skinned, mirrored), instance };
            };
            const auto [level, next] = meshes == MeshOf::Eye
                ? eyeInstances(object, objects, mirror)
                : std::pair<uint32_t, uint32_t>{object, 0u};
            m_keying[i] = keyOf(level);
            if (next != 0) m_keying.push_back(keyOf(next));
        }
        // The same keys in the same order sort as they did: a still view, or what moved keeping its
        // draw, keeps last build's order.
        if (m_keying != m_listed) {
            m_listed.swap(m_keying);
            m_keys = m_listed;
            std::sort(
                m_keys.begin(),
                m_keys.end(),
                [](const SortKey& a, const SortKey& b) { return a.key < b.key; }
            );
        }
    }

    // Equal keys are one run; only its head's object is looked at.
    PROFILE_SCOPE("InstanceBatch/Runs");
    std::vector<uint32_t>& instances = m_list.instances();
    size_t i = 0;
    while (i < m_keys.size()) {
        const uint64_t key = m_keys[i].key;
        size_t end = i + 1;
        while (end < m_keys.size() && m_keys[end].key == key) ++end;

        const uint32_t head = m_keys[i].instance;
        // A group with no GL mesh writes no command, so its instances stay out of the list,
        // or the next command's slice would start at them.
        if (const GLMesh* mesh = mirror.getMesh(meshOf(objects, head, meshes))) {
            const uint32_t first = static_cast<uint32_t>(instances.size());
            for (size_t j = i; j < end; ++j) instances.push_back(m_keys[j].instance);
            const bool skinned  = ((key >> SKINNED_BIT) & 1u) != 0;
            const bool mirrored = ((key >> MIRRORED_BIT) & 1u) != 0;
            const bool water    = !skinned && ((key >> KIND_SHIFT) & KIND_MASK) == KIND_WATER;
            const auto count = static_cast<uint32_t>(end - i);
            const MaterialHandle& material = objects.draws[head & INSTANCE_OBJECT_MASK].material;
            addRun(*mesh, material, skinned, mirrored, water, first, count);
        }
        i = end;
    }

    PROFILE_SCOPE("InstanceBatch/Upload");
    m_list.upload(mirror.stream());
    return m_draws;
}

const std::vector<InstanceDraw>& GLInstanceBatcher::buildSequential(
    const std::vector<uint32_t>& list,
    const RenderObjects& objects,
    const GLAssetMirror& mirror,
    uint32_t bones,
    MeshOf meshes
) {
    if (!beginBuild(list)) return m_draws;

    const bool skinning = bones > 0;
    std::vector<uint32_t>& instances = m_list.instances();
    for (const uint32_t object : list) {
        const ObjectDraw& draw = objects.draws[object];
        const bool mirrored = mirrors(objects.models[object]);
        const bool water    = draw.water != 0;
        const auto add = [&](uint32_t instance) {
            const GLMesh* mesh = mirror.getMesh(meshOf(objects, instance, meshes));
            if (!mesh) return;
            const bool skinned = skinning && drawsSkinned(draw, mesh);
            const auto first   = static_cast<uint32_t>(instances.size());
            addRun(*mesh, draw.material, skinned, mirrored, water && !skinned, first, 1);
            instances.push_back(instance);
        };
        if (meshes != MeshOf::Eye) {
            add(object);
            continue;
        }
        const auto [level, next] = eyeInstances(object, objects, mirror);
        add(level);
        if (next != 0) add(next);
    }

    m_list.upload(mirror.stream());
    return m_draws;
}

} // namespace Vkm::Engine
