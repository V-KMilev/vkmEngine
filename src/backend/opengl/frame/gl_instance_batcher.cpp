#include "frame/gl_instance_batcher.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "gl_context.h"

#include "gl_view.h"
#include "asset/gl_mesh.h"
#include "debug/profiler.h"
#include "system/render/data/render_objects.h"

namespace Vkm::Engine {

namespace {

// Program, winding, then material, then mesh, so identical draws sort together and a run merges
// into one instanced call. Ids are sparse-set slots, far below the material's 30 bits.
constexpr int SKINNED_BIT  = 63;
constexpr int MIRRORED_BIT = 62;

uint64_t sortKey(const ObjectDraw& draw, bool skinned, bool mirrored) {
    return (static_cast<uint64_t>(skinned) << SKINNED_BIT)
        | (static_cast<uint64_t>(mirrored) << MIRRORED_BIT)
        | (static_cast<uint64_t>(draw.material.id() & 0x3fffffffu) << 32)
        | static_cast<uint64_t>(draw.mesh.id());
}

// A transform that mirrors reverses every triangle's winding.
bool mirrors(const glm::mat4& model) {
    return glm::determinant(glm::mat3(model)) < 0.0f;
}

} // namespace

bool GLInstanceBatcher::drawsSkinned(
    uint32_t object,
    const RenderObjects& objects,
    const GLView& view
) const {
    const ObjectDraw& draw = objects.draws[object];
    if (draw.skinCount == 0) return false;
    const GLMesh* mesh = view.getMesh(draw.mesh);
    return mesh && mesh->isSkinned();
}

bool GLInstanceBatcher::beginBuild(const std::vector<uint32_t>& list) {
    m_draws.clear();
    m_list.clear();
    if (list.empty()) return false;

    m_list.instances().reserve(list.size());
    return true;
}

void GLInstanceBatcher::draw(Vkm::GL::Context& gl, const InstanceDraw& draw) const {
    if (draw.mirrored) gl.setFrontFace(GL_CW);
    m_list.draw(*draw.mesh, draw.first, draw.count);
    if (draw.mirrored) gl.setFrontFace(GL_CCW);
}

void GLInstanceBatcher::addRun(
    const GLMesh& mesh,
    const MaterialHandle& material,
    bool skinned,
    bool mirrored,
    uint32_t first,
    uint32_t count
) {
    std::vector<DrawCommand>& commands = m_list.commands();
    const InstanceDraw* last = m_draws.empty() ? nullptr : &m_draws.back();
    if (!last || last->material != material || last->skinned != skinned || last->mirrored != mirrored
        || last->mesh->layout() != mesh.layout()) {
        const uint32_t firstCommand = static_cast<uint32_t>(commands.size());
        m_draws.push_back({ &mesh, material, firstCommand, 0, skinned, mirrored });
    }
    commands.push_back(mesh.command(count, first));
    ++m_draws.back().count;
}

const std::vector<InstanceDraw>& GLInstanceBatcher::buildGrouped(
    const std::vector<uint32_t>& list,
    const RenderObjects& objects,
    const GLView& view,
    uint32_t bones
) {
    if (!beginBuild(list)) return m_draws;

    {
        PROFILE_SCOPE("InstanceBatch/Sort");
        const bool skinning = bones > 0;
        m_keys.resize(list.size());
        for (size_t i = 0; i < list.size(); ++i) {
            const uint32_t object = list[i];
            const bool skinned  = skinning && drawsSkinned(object, objects, view);
            const bool mirrored = mirrors(objects.models[object]);
            m_keys[i] = { sortKey(objects.draws[object], skinned, mirrored), object };
        }
        std::sort(
            m_keys.begin(),
            m_keys.end(),
            [](const SortKey& a, const SortKey& b) { return a.key < b.key; }
        );
    }

    // Equal keys are one run; only its head's object is looked at.
    PROFILE_SCOPE("InstanceBatch/Runs");
    std::vector<uint32_t>& instances = m_list.instances();
    size_t i = 0;
    while (i < m_keys.size()) {
        const uint64_t key = m_keys[i].key;
        size_t end = i + 1;
        while (end < m_keys.size() && m_keys[end].key == key) ++end;

        const ObjectDraw& head = objects.draws[m_keys[i].object];
        // A group with no GL mesh writes no command, so its instances stay out of the list,
        // or the next command's slice would start at them.
        if (const GLMesh* mesh = view.getMesh(head.mesh)) {
            const uint32_t first = static_cast<uint32_t>(instances.size());
            for (size_t j = i; j < end; ++j) instances.push_back(m_keys[j].object);
            const bool skinned  = ((key >> SKINNED_BIT) & 1u) != 0;
            const bool mirrored = ((key >> MIRRORED_BIT) & 1u) != 0;
            addRun(*mesh, head.material, skinned, mirrored, first, static_cast<uint32_t>(end - i));
        }
        i = end;
    }

    PROFILE_SCOPE("InstanceBatch/Upload");
    m_list.upload();
    return m_draws;
}

const std::vector<InstanceDraw>& GLInstanceBatcher::buildSequential(
    const std::vector<uint32_t>& list,
    const RenderObjects& objects,
    const GLView& view,
    uint32_t bones
) {
    if (!beginBuild(list)) return m_draws;

    const bool skinning = bones > 0;
    std::vector<uint32_t>& instances = m_list.instances();
    for (const uint32_t object : list) {
        const ObjectDraw& draw = objects.draws[object];
        const GLMesh*     mesh = view.getMesh(draw.mesh);
        if (!mesh) continue;
        const bool skinned = skinning && draw.skinCount > 0 && mesh->isSkinned();
        const bool mirrored = mirrors(objects.models[object]);
        addRun(*mesh, draw.material, skinned, mirrored, static_cast<uint32_t>(instances.size()), 1);
        instances.push_back(object);
    }

    m_list.upload();
    return m_draws;
}

} // namespace Vkm::Engine
