#define VKM_LOG_CATEGORY "LOADER"

#include "import/model/mesh_corners.h"

#include <algorithm>
#include <cmath>

#include <meshoptimizer.h>
#include <mikktspace.h>

#include <glm/gtc/type_ptr.hpp>

#include "vkm_log/logger.h"

namespace Vkm::Engine::ModelReaders {

namespace {

/**
 * @brief Fill in the tangent of every corner of a triangle list with MikkTSpace.
 *
 * Bakers bake normal maps against MikkTSpace, so only its frame shades them correctly. The
 * bitangent is `cross(normal, tangent) * w`, the way V grows.
 *
 * @param corners Three vertices per triangle, positions, normals and UVs filled.
 * @return False when MikkTSpace gave up, which leaves the tangents untouched.
 */
bool generateTangents(std::vector<Vertex>& corners) {
    SMikkTSpaceInterface callbacks{};
    callbacks.m_getNumFaces = [](const SMikkTSpaceContext* context) {
        return static_cast<int>(static_cast<std::vector<Vertex>*>(context->m_pUserData)->size() / 3);
    };
    callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, const int) { return 3; };
    callbacks.m_getPosition = [](
        const SMikkTSpaceContext* context,
        float out[],
        const int face,
        const int corner
    ) {
        const Vertex& v = (*static_cast<std::vector<Vertex>*>(context->m_pUserData))[face * 3 + corner];
        std::memcpy(out, glm::value_ptr(v.position), sizeof(float) * 3);
    };
    callbacks.m_getNormal = [](
        const SMikkTSpaceContext* context,
        float out[],
        const int face,
        const int corner
    ) {
        const Vertex& v = (*static_cast<std::vector<Vertex>*>(context->m_pUserData))[face * 3 + corner];
        std::memcpy(out, glm::value_ptr(v.normal), sizeof(float) * 3);
    };
    callbacks.m_getTexCoord = [](
        const SMikkTSpaceContext* context,
        float out[],
        const int face,
        const int corner
    ) {
        const Vertex& v = (*static_cast<std::vector<Vertex>*>(context->m_pUserData))[face * 3 + corner];
        std::memcpy(out, glm::value_ptr(v.uv), sizeof(float) * 2);
    };
    callbacks.m_setTSpaceBasic = [](
        const SMikkTSpaceContext* context,
        const float tangent[],
        const float sign,
        const int face,
        const int corner
    ) {
        Vertex& v = (*static_cast<std::vector<Vertex>*>(context->m_pUserData))[face * 3 + corner];
        v.tangent = glm::vec4(tangent[0], tangent[1], tangent[2], sign < 0.0f ? -1.0f : 1.0f);
    };

    SMikkTSpaceContext context{};
    context.m_pInterface = &callbacks;
    context.m_pUserData  = &corners;
    return genTangSpaceDefault(&context) != 0;
}

} // namespace

std::string meshLabel(const std::string& ref, size_t index) {
    return "Mesh " + std::to_string(index) + " of '" + ref + "'";
}

SkinVertex quantizeInfluences(std::vector<VertexInfluence>& influences) {
    // A tie goes to the lower bone: std::sort leaves it open, and each standard library settles it
    // its own way, so the cooked bytes would differ by platform.
    std::sort(influences.begin(), influences.end(), [](const VertexInfluence& a, const VertexInfluence& b) {
        return a.weight != b.weight ? a.weight > b.weight : a.bone < b.bone;
    });
    if (influences.size() > 4) influences.resize(4);

    SkinVertex skin{};
    float total = 0.0f;
    for (const VertexInfluence& influence : influences) total += influence.weight;
    if (!(total > 0.0f)) return skin;

    int quantized[4] = {0, 0, 0, 0};
    int sum          = 0;
    for (size_t k = 0; k < influences.size(); ++k) {
        skin.bones[k] = influences[k].bone;
        quantized[k]  = static_cast<int>(std::lround(influences[k].weight / total * 255.0f));
        sum += quantized[k];
    }
    quantized[0] = std::clamp(quantized[0] + (255 - sum), 0, 255);
    for (int k = 0; k < 4; ++k) skin.weights[k] = static_cast<uint8_t>(quantized[k]);
    return skin;
}


MeshAsset weldCorners(Corners corners, const std::string& mesh) {
    MeshAsset out;
    if (corners.vertices.empty()) return out;

    // Without UVs there is no normal map to orient, and the default +X will do.
    if (!corners.hasTangents && corners.hasUVs && !generateTangents(corners.vertices)) {
        LOG_WARNING("%s: MikkTSpace could not build tangents; left at +X", mesh.c_str());
    }

    const size_t count = corners.vertices.size();
    const meshopt_Stream streams[2] = {
        {corners.vertices.data(), sizeof(Vertex), sizeof(Vertex)},
        {corners.skin.data(), sizeof(SkinVertex), sizeof(SkinVertex)},
    };
    std::vector<unsigned int> remap(count);
    const size_t unique = meshopt_generateVertexRemapMulti(
        remap.data(),
        nullptr,
        count,
        count,
        streams,
        corners.skin.empty() ? 1 : 2
    );

    out.vertices.resize(unique);
    meshopt_remapVertexBuffer(
        out.vertices.data(),
        corners.vertices.data(),
        count,
        sizeof(Vertex),
        remap.data()
    );
    if (!corners.skin.empty()) {
        out.skin.resize(unique);
        meshopt_remapVertexBuffer(
            out.skin.data(),
            corners.skin.data(),
            count,
            sizeof(SkinVertex),
            remap.data()
        );
    }
    out.indices.assign(remap.begin(), remap.end());
    return out;
}

void logSkinReport(const SkinReport& report, const std::string& mesh) {
    if (report.outOfRange) {
        LOG_WARNING(
            "%s: dropped %u influence(s) naming a joint its skin does not have",
            mesh.c_str(),
            report.outOfRange
        );
    }
    if (report.unweighted) {
        LOG_WARNING(
            "%s: %u vertex/vertices carry no influence; bound to the rig root",
            mesh.c_str(),
            report.unweighted
        );
    }
}

} // namespace Vkm::Engine::ModelReaders
