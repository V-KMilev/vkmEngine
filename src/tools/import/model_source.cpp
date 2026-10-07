#define VKM_LOG_CATEGORY "LOADER"

#include "import/model_source.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <future>
#include <limits>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cgltf.h>
#include <meshoptimizer.h>
#include <mikktspace.h>
#include <ufbx.h>

#include <glm/gtc/type_ptr.hpp>

#include "logger.h"
#include "debug/profiler.h"
#include "cook/cook_key.h"
#include "io/asset/asset_cook.h"

namespace Vkm::Engine {

namespace {

// A mesh's log label: its index in the file, which also names its asset.
std::string meshLabel(const std::string& ref, size_t index) {
    return "Mesh " + std::to_string(index) + " of '" + ref + "'";
}

// One influence on one vertex, before the four that survive are quantised.
struct VertexInfluence {
    uint16_t bone   = 0;
    float    weight = 0.0f;
};

/**
 * @brief Quantise one vertex's influences into the four bytes a SkinVertex holds.
 *
 * The strongest four are renormalised among themselves, then rounded with the error on the
 * largest, so the bytes sum to exactly 255 and no vertex stage renormalises.
 *
 * @param influences The vertex's influences, any order, weights not normalised;
 *        sorted and truncated in place.
 * @return The quantised binding, or all-zero weights when nothing influences
 *         the vertex.
 */
SkinVertex quantiseInfluences(std::vector<VertexInfluence>& influences) {
    std::sort(
        influences.begin(),
        influences.end(),
        [](const VertexInfluence& a, const VertexInfluence& b) { return a.weight > b.weight; }
    );
    if (influences.size() > 4) influences.resize(4);

    SkinVertex skin{};
    float total = 0.0f;
    for (const VertexInfluence& influence : influences) total += influence.weight;
    if (!(total > 0.0f)) return skin;

    int quantised[4] = {0, 0, 0, 0};
    int sum = 0;
    for (size_t k = 0; k < influences.size(); ++k) {
        skin.bones[k] = influences[k].bone;
        quantised[k]  = static_cast<int>(std::lround(influences[k].weight / total * 255.0f));
        sum += quantised[k];
    }
    quantised[0] = std::clamp(quantised[0] + (255 - sum), 0, 255);
    for (int k = 0; k < 4; ++k) skin.weights[k] = static_cast<uint8_t>(quantised[k]);
    return skin;
}

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

/**
 * @brief A mesh as three corners per triangle, before tangents and welding.
 *
 * MikkTSpace wants per-corner tangents and a weld after, so disagreeing tangents split a vertex.
 */
struct Corners {
    std::vector<Vertex>     vertices;
    std::vector<SkinVertex> skin;               ///< Empty, or parallel to `vertices`.
    bool                    hasUVs      = false;
    bool                    hasTangents = false;  ///< The file authored them.
};

/**
 * @brief Give @p corners their tangents and weld them into an indexed mesh.
 *
 * Byte-equal corners (vertex and skin) become one vertex, in first-appearance order, so the
 * index buffer keeps the file's triangle order.
 *
 * @param corners The triangle list; consumed.
 * @param mesh Name the log lines report against.
 * @return The welded geometry, with no bounds, rig or recipe yet.
 */
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

// Counts of what a skin import refused or repaired, reported once per mesh.
struct SkinReport {
    unsigned outOfRange = 0;  ///< Influences naming a joint the skin does not have.
    unsigned unweighted = 0;  ///< Vertices nothing influences.
};

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

/**
 * @brief Read a glTF or GLB file with cgltf.
 *
 * Already right-handed, +Y up, metres; V is flipped as read. Meshes are numbered by primitive in
 * mesh order; a primitive without a material takes the one past the last, the spec's default.
 */
class GltfReader {
    public:
        GltfReader(const std::string& ref, cgltf_data* data)
            : m_ref(ref)
            , m_data(data)
        {}
        ~GltfReader() = default;

        GltfReader(const GltfReader& other) = delete;
        GltfReader& operator=(const GltfReader& other) = delete;

        GltfReader(GltfReader && other) = delete;
        GltfReader& operator=(GltfReader && other) = delete;

    public:
        SourceModel read() {
            readImages();
            readMaterials();
            readNodes();
            readMeshes();
            readClips();
            return std::move(m_model);
        }

    private:
        void readImages() {
            for (cgltf_size i = 0; i < m_data->images_count; ++i) {
                const cgltf_image& image = m_data->images[i];
                SourceImage out;
                if (image.buffer_view) {
                    // A view into a buffer cgltf did not load (compressed) loads as missing.
                    const cgltf_buffer_view& view = *image.buffer_view;
                    if (view.buffer->data) {
                        const auto* bytes = static_cast<const unsigned char*>(view.buffer->data);
                        out.embedded.assign(bytes + view.offset, bytes + view.offset + view.size);
                    }
                } else if (image.uri && std::strncmp(image.uri, "data:", 5) == 0) {
                    out.embedded = decodeDataUri(image.uri);
                } else if (image.uri) {
                    std::string uri = image.uri;
                    uri.resize(cgltf_decode_uri(uri.data()));
                    out.ref = uri;
                }
                if (out.ref.empty()) out.ref = "*" + std::to_string(i);
                m_model.images.push_back(std::move(out));
            }
        }

        std::vector<unsigned char> decodeDataUri(const char* uri) const {
            const char* comma = std::strchr(uri, ',');
            if (!comma || !std::strstr(uri, ";base64")) return {};
            const std::string payload = comma + 1;
            size_t size = payload.size() / 4 * 3;
            for (auto it = payload.rbegin(); it != payload.rend() && *it == '='; ++it) --size;
            cgltf_options options{};
            void* decoded = nullptr;
            if (cgltf_load_buffer_base64(&options, size, payload.c_str(), &decoded) != cgltf_result_success) {
                return {};
            }
            const auto* bytes = static_cast<const unsigned char*>(decoded);
            std::vector<unsigned char> out(bytes, bytes + size);
            std::free(decoded);
            return out;
        }

        void mapTexture(
            SourceMaterial& material,
            const cgltf_texture_view& view,
            TextureHandle MaterialAsset::* slot,
            TextureUsage usage
        ) const {
            if (!view.texture || !view.texture->image) return;
            const auto image = static_cast<uint32_t>(cgltf_image_index(m_data, view.texture->image));
            material.maps.push_back({slot, usage, image});
        }

        // Null is the default material, whose defaults are cgltf's for a present one.
        SourceMaterial readMaterial(const cgltf_material* material) const {
            SourceMaterial out;
            MaterialAsset& v = out.values;
            v.metallic  = 1.0f;
            v.roughness = 1.0f;
            if (!material) return out;

            const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
            v.albedo    = glm::make_vec4(pbr.base_color_factor);
            v.metallic  = pbr.metallic_factor;
            v.roughness = pbr.roughness_factor;
            v.emission  = glm::make_vec3(material->emissive_factor);
            mapTexture(out, pbr.base_color_texture, &MaterialAsset::albedoTexture, TextureUsage::Color);
            mapTexture(
                out,
                pbr.metallic_roughness_texture,
                &MaterialAsset::metallicRoughnessTexture,
                TextureUsage::Data
            );
            mapTexture(out, material->normal_texture, &MaterialAsset::normalTexture, TextureUsage::Normal);
            mapTexture(out, material->occlusion_texture, &MaterialAsset::aoTexture, TextureUsage::Data);
            mapTexture(out, material->emissive_texture, &MaterialAsset::emissionTexture, TextureUsage::Color);
            if (material->normal_texture.texture) v.normalScale = material->normal_texture.scale;

            if (material->has_emissive_strength) {
                v.emissiveStrength = material->emissive_strength.emissive_strength;
            }
            if (material->has_ior && material->ior.ior > 0.0f) v.ior = material->ior.ior;
            if (material->has_transmission) {
                v.transmission = material->transmission.transmission_factor;
                mapTexture(
                    out,
                    material->transmission.transmission_texture,
                    &MaterialAsset::transmissionTexture,
                    TextureUsage::Data
                );
            }
            // Zero thickness is thin-walled, the default. Infinite attenuation distance becomes 1.0
            // so the field stays editable.
            if (material->has_volume) {
                v.thicknessFactor  = material->volume.thickness_factor;
                v.attenuationColor = glm::make_vec3(material->volume.attenuation_color);
                const float distance = material->volume.attenuation_distance;
                if (distance > 0.0f && distance < std::numeric_limits<float>::max()) {
                    v.attenuationDistance = distance;
                }
            }
            if (material->has_clearcoat) {
                v.clearcoat          = material->clearcoat.clearcoat_factor;
                v.clearcoatRoughness = material->clearcoat.clearcoat_roughness_factor;
                mapTexture(
                    out,
                    material->clearcoat.clearcoat_texture,
                    &MaterialAsset::clearcoatTexture,
                    TextureUsage::Data
                );
            }
            if (material->has_sheen) {
                v.sheenColor     = glm::make_vec3(material->sheen.sheen_color_factor);
                v.sheenRoughness = material->sheen.sheen_roughness_factor;
            }
            if (material->has_anisotropy) v.anisotropy = material->anisotropy.anisotropy_strength;

            v.doubleSided = material->double_sided != 0;

            // Transmission glass keeps alpha at 1, so transmission classifies it Transparent; the
            // spec ignores an OPAQUE material's alpha.
            switch (material->alpha_mode) {
                case cgltf_alpha_mode_mask:
                    v.type        = MaterialType::AlphaMask;
                    v.alphaCutoff = material->alpha_cutoff;
                    break;
                case cgltf_alpha_mode_blend:
                    v.type = MaterialType::Transparent;
                    break;
                case cgltf_alpha_mode_opaque:
                case cgltf_alpha_mode_max_enum:
                    v.type = v.transmission > 0.001f ? MaterialType::Transparent : MaterialType::Opaque;
                    break;
            }
            return out;
        }

        void readMaterials() {
            for (cgltf_size i = 0; i < m_data->materials_count; ++i) {
                m_model.materials.push_back(readMaterial(&m_data->materials[i]));
            }
        }

        uint32_t defaultMaterial() {
            if (m_defaultMaterial < 0) {
                m_defaultMaterial = static_cast<int32_t>(m_model.materials.size());
                m_model.materials.push_back(readMaterial(nullptr));
            }
            return static_cast<uint32_t>(m_defaultMaterial);
        }

        // Depth-first under the default scene's root, or every parentless node when none.
        void readNodes() {
            m_nodeIndex.assign(m_data->nodes_count, -1);
            m_model.nodes.push_back(SourceNode{});
            const cgltf_scene* scene = m_data->scene
                ? m_data->scene
                : (m_data->scenes_count ? &m_data->scenes[0] : nullptr);
            if (scene) {
                for (cgltf_size i = 0; i < scene->nodes_count; ++i) addNode(scene->nodes[i], 0);
            } else {
                for (cgltf_size i = 0; i < m_data->nodes_count; ++i) {
                    if (!m_data->nodes[i].parent) addNode(&m_data->nodes[i], 0);
                }
            }
        }

        void addNode(const cgltf_node* node, uint32_t parent) {
            const auto nodeIndex = static_cast<size_t>(cgltf_node_index(m_data, node));
            if (m_nodeIndex[nodeIndex] >= 0) return;  // a node the walk reaches twice
            const auto index = static_cast<uint32_t>(m_model.nodes.size());
            m_nodeIndex[nodeIndex] = static_cast<int32_t>(index);

            SourceNode out;
            out.name   = node->name && *node->name ? node->name : "node" + std::to_string(nodeIndex);
            out.parent = static_cast<int32_t>(parent);
            if (node->has_matrix) {
                out.local = Transform::fromModelMatrix(glm::make_mat4(node->matrix));
            } else {
                if (node->has_translation) out.local.position = glm::make_vec3(node->translation);
                if (node->has_rotation) {
                    out.local.rotation = glm::quat(
                        node->rotation[3],
                        node->rotation[0],
                        node->rotation[1],
                        node->rotation[2]
                    );
                }
                if (node->has_scale) out.local.scale = glm::make_vec3(node->scale);
            }
            m_model.nodes.push_back(std::move(out));
            m_model.nodes[parent].children.push_back(index);

            // The skin is the node's; a mesh two skinned nodes share is bound by the first.
            if (node->mesh && node->skin) {
                m_skinOf.emplace(node->mesh, node->skin);
            }
            if (node->mesh) m_meshNodes.push_back({index, node->mesh});

            for (cgltf_size c = 0; c < node->children_count; ++c) addNode(node->children[c], index);
        }

        void readMeshes() {
            std::unordered_map<const cgltf_mesh*, uint32_t> first;
            for (cgltf_size m = 0; m < m_data->meshes_count; ++m) {
                const cgltf_mesh& mesh = m_data->meshes[m];
                first[&mesh] = static_cast<uint32_t>(m_model.meshes.size());
                const auto skin = m_skinOf.find(&mesh);
                for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
                    const std::string label = meshLabel(m_ref, m_model.meshes.size());
                    SourceMesh primitive = readPrimitive(
                        mesh.primitives[p],
                        skin != m_skinOf.end() ? skin->second : nullptr,
                        label
                    );
                    m_model.meshes.push_back(std::move(primitive));
                }
            }
            for (const auto& [node, mesh] : m_meshNodes) {
                for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
                    m_model.nodes[node].meshes.push_back(first[mesh] + static_cast<uint32_t>(p));
                }
            }
        }

        // Triangles as vertex indices, whatever the mode; points and lines have none.
        static std::vector<uint32_t> triangleIndices(const cgltf_primitive& primitive, size_t vertexCount) {
            std::vector<uint32_t> list;
            const size_t count = primitive.indices ? primitive.indices->count : vertexCount;
            list.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const uint32_t index = primitive.indices
                    ? static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i))
                    : static_cast<uint32_t>(i);
                list.push_back(index);
            }
            std::vector<uint32_t> out;
            switch (primitive.type) {
                case cgltf_primitive_type_triangles:
                    out = std::move(list);
                    out.resize(out.size() / 3 * 3);
                    break;
                case cgltf_primitive_type_triangle_strip:
                    for (size_t i = 2; i < list.size(); ++i) {
                        const bool even = (i % 2) == 0;
                        out.insert(
                            out.end(),
                            {list[i - 2], even ? list[i - 1] : list[i], even ? list[i] : list[i - 1]}
                        );
                    }
                    break;
                case cgltf_primitive_type_triangle_fan:
                    for (size_t i = 2; i < list.size(); ++i) {
                        out.insert(out.end(), {list[0], list[i - 1], list[i]});
                    }
                    break;
                default:
                    break;
            }
            return out;
        }

        SourceMesh readPrimitive(
            const cgltf_primitive& primitive,
            const cgltf_skin* skin,
            const std::string& label
        ) {
            SourceMesh out;
            out.material = primitive.material
                ? static_cast<uint32_t>(cgltf_material_index(m_data, primitive.material))
                : defaultMaterial();

            const cgltf_accessor* positions = nullptr;
            const cgltf_accessor* normals   = nullptr;
            const cgltf_accessor* uvs       = nullptr;
            const cgltf_accessor* tangents  = nullptr;
            std::vector<std::pair<const cgltf_accessor*, const cgltf_accessor*>> influenceSets;
            std::unordered_map<cgltf_int, const cgltf_accessor*> joints;
            std::unordered_map<cgltf_int, const cgltf_accessor*> weights;
            for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                const cgltf_attribute& attribute = primitive.attributes[a];
                switch (attribute.type) {
                    case cgltf_attribute_type_position: positions = attribute.data; break;
                    case cgltf_attribute_type_normal:   normals   = attribute.data; break;
                    case cgltf_attribute_type_tangent:  tangents  = attribute.data; break;
                    case cgltf_attribute_type_texcoord:
                        if (attribute.index == 0) uvs = attribute.data;
                        break;
                    case cgltf_attribute_type_joints:  joints[attribute.index]  = attribute.data; break;
                    case cgltf_attribute_type_weights: weights[attribute.index] = attribute.data; break;
                    default: break;
                }
            }
            if (!positions) return out;
            const size_t vertexCount = positions->count;
            if (skin) {
                for (const auto& [set, accessor] : joints) {
                    const auto w = weights.find(set);
                    if (w != weights.end() && accessor->count == vertexCount
                        && w->second->count == vertexCount) {
                        influenceSets.push_back({accessor, w->second});
                    }
                }
            }

            // Every source vertex read once, then copied to each corner using it.
            std::vector<Vertex> source(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i) {
                Vertex& v = source[i];
                cgltf_accessor_read_float(positions, i, glm::value_ptr(v.position), 3);
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                if (normals && normals->count == vertexCount) {
                    cgltf_accessor_read_float(normals, i, glm::value_ptr(v.normal), 3);
                }
                v.uv = glm::vec2(0.0f);
                if (uvs && uvs->count == vertexCount) {
                    cgltf_accessor_read_float(uvs, i, glm::value_ptr(v.uv), 2);
                    v.uv.y = 1.0f - v.uv.y;
                }
                v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                if (tangents && tangents->count == vertexCount) {
                    cgltf_accessor_read_float(tangents, i, glm::value_ptr(v.tangent), 4);
                    v.tangent.w = v.tangent.w < 0.0f ? -1.0f : 1.0f;
                }
            }

            std::vector<SkinVertex> sourceSkin;
            SkinReport report;
            if (skin && !influenceSets.empty()) {
                if (skin->joints_count > AssetCook::MAX_SKELETON_BONES) {
                    LOG_ERROR(
                        "%s: its skin names %zu joints, past the %u the cooked format admits; "
                        "imported unskinned",
                        label.c_str(),
                        static_cast<size_t>(skin->joints_count),
                        AssetCook::MAX_SKELETON_BONES
                    );
                } else {
                    for (cgltf_size j = 0; j < skin->joints_count; ++j) {
                        SourceBone bone;
                        const auto node = static_cast<size_t>(cgltf_node_index(m_data, skin->joints[j]));
                        if (m_nodeIndex[node] < 0) {
                            LOG_ERROR(
                                "%s: its skin names joint '%s', which is in no scene the file shows; "
                                "imported unskinned",
                                label.c_str(),
                                skin->joints[j]->name ? skin->joints[j]->name : ""
                            );
                            out.bones.clear();
                            break;
                        }
                        bone.node = static_cast<uint32_t>(m_nodeIndex[node]);
                        if (skin->inverse_bind_matrices) {
                            cgltf_accessor_read_float(
                                skin->inverse_bind_matrices,
                                j,
                                glm::value_ptr(bone.inverseBind),
                                16
                            );
                        }
                        out.bones.push_back(bone);
                    }
                }
                if (!out.bones.empty()) {
                    sourceSkin.resize(vertexCount);
                    std::vector<VertexInfluence> influences;
                    for (size_t i = 0; i < vertexCount; ++i) {
                        influences.clear();
                        for (const auto& [jointAccessor, weightAccessor] : influenceSets) {
                            cgltf_uint joint[4] = {0, 0, 0, 0};
                            float      weight[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                            cgltf_accessor_read_uint(jointAccessor, i, joint, 4);
                            cgltf_accessor_read_float(weightAccessor, i, weight, 4);
                            for (int k = 0; k < 4; ++k) {
                                if (!(weight[k] > 0.0f)) continue;
                                if (joint[k] >= out.bones.size()) {
                                    ++report.outOfRange;
                                    continue;
                                }
                                influences.push_back({static_cast<uint16_t>(joint[k]), weight[k]});
                            }
                        }
                        sourceSkin[i] = quantiseInfluences(influences);
                        if (sourceSkin[i].weights[0] == 0) ++report.unweighted;
                    }
                }
            }
            logSkinReport(report, label);

            Corners corners;
            corners.hasUVs      = uvs && uvs->count == vertexCount;
            corners.hasTangents = tangents && tangents->count == vertexCount;
            const std::vector<uint32_t> triangles = triangleIndices(primitive, vertexCount);
            corners.vertices.reserve(triangles.size());
            if (!sourceSkin.empty()) corners.skin.reserve(triangles.size());
            for (const uint32_t index : triangles) {
                if (index >= vertexCount) continue;
                corners.vertices.push_back(source[index]);
                if (!sourceSkin.empty()) corners.skin.push_back(sourceSkin[index]);
            }
            if (corners.vertices.size() % 3 != 0) {
                // An index past the vertices shifts every later triangle, so none is kept.
                LOG_ERROR("%s: an index names a vertex the primitive does not have", label.c_str());
                return out;
            }
            out.geometry = weldCorners(std::move(corners), label);
            return out;
        }

        // LINEAR as read, STEP held by a key just before the next, CUBICSPLINE at its keys.
        template<typename T, size_t N>
        void readKeys(
            const cgltf_animation_sampler& sampler,
            std::vector<float>& times,
            std::vector<T>& values,
            T (*make)(const float*)
        ) {
            const cgltf_accessor* input  = sampler.input;
            const cgltf_accessor* output = sampler.output;
            const size_t stride = sampler.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1;
            if (!input || !output || output->count < input->count * stride) return;

            for (size_t k = 0; k < input->count; ++k) {
                float time = 0.0f;
                float value[N] = {};
                cgltf_accessor_read_float(input, k, &time, 1);
                cgltf_accessor_read_float(output, k * stride + (stride == 3 ? 1 : 0), value, N);
                times.push_back(time);
                values.push_back(make(value));
                if (sampler.interpolation == cgltf_interpolation_type_step && k + 1 < input->count) {
                    float next = 0.0f;
                    cgltf_accessor_read_float(input, k + 1, &next, 1);
                    times.push_back(std::nextafter(next, time));
                    values.push_back(make(value));
                }
            }
            if (stride == 3 && !m_warnedCubic) {
                LOG_WARNING(
                    "Model '%s': a CUBICSPLINE channel is sampled at its keys and interpolated "
                    "linearly between them",
                    m_ref.c_str()
                );
                m_warnedCubic = true;
            }
        }

        void readClips() {
            for (cgltf_size a = 0; a < m_data->animations_count; ++a) {
                const cgltf_animation& animation = m_data->animations[a];
                SourceClip clip;
                std::unordered_map<uint32_t, size_t> channelOf;
                for (cgltf_size c = 0; c < animation.channels_count; ++c) {
                    const cgltf_animation_channel& channel = animation.channels[c];
                    if (!channel.target_node || !channel.sampler) continue;
                    const int32_t node = m_nodeIndex[cgltf_node_index(m_data, channel.target_node)];
                    if (node < 0) continue;
                    auto [it, added] = channelOf.emplace(static_cast<uint32_t>(node), clip.channels.size());
                    if (added) {
                        clip.channels.push_back(SourceChannel{});
                        clip.channels.back().node = static_cast<uint32_t>(node);
                    }
                    SourceChannel& target = clip.channels[it->second];
                    switch (channel.target_path) {
                        case cgltf_animation_path_type_translation:
                            readKeys<glm::vec3, 3>(
                                *channel.sampler,
                                target.positionTimes,
                                target.positions,
                                [](const float* f) { return glm::make_vec3(f); }
                            );
                            break;
                        case cgltf_animation_path_type_rotation:
                            readKeys<glm::quat, 4>(
                                *channel.sampler,
                                target.rotationTimes,
                                target.rotations,
                                [](const float* f) {
                                    return glm::normalize(glm::quat(f[3], f[0], f[1], f[2]));
                                }
                            );
                            break;
                        case cgltf_animation_path_type_scale:
                            readKeys<glm::vec3, 3>(
                                *channel.sampler,
                                target.scaleTimes,
                                target.scales,
                                [](const float* f) { return glm::make_vec3(f); }
                            );
                            break;
                        default:
                            break;
                    }
                }
                for (const SourceChannel& channel : clip.channels) {
                    for (const std::vector<float>* times :
                         {&channel.positionTimes, &channel.rotationTimes, &channel.scaleTimes}) {
                        if (!times->empty()) clip.duration = std::max(clip.duration, times->back());
                    }
                }
                m_model.clips.push_back(std::move(clip));
            }
        }

    private:
        std::string m_ref;
        cgltf_data* m_data = nullptr;
        SourceModel m_model;

        std::vector<int32_t> m_nodeIndex;  ///< cgltf node index -> SourceModel node, -1 if in no scene.
        std::unordered_map<const cgltf_mesh*, const cgltf_skin*> m_skinOf;
        std::vector<std::pair<uint32_t, const cgltf_mesh*>>      m_meshNodes;
        int32_t m_defaultMaterial = -1;
        bool    m_warnedCubic     = false;
};

const char* describe(cgltf_result result) {
    switch (result) {
        case cgltf_result_data_too_short:   return "the file is shorter than its header says";
        case cgltf_result_unknown_format:   return "not a glTF file";
        case cgltf_result_invalid_json:     return "the JSON does not parse";
        case cgltf_result_invalid_gltf:     return "the JSON is not valid glTF";
        case cgltf_result_file_not_found:   return "a file it names was not found";
        case cgltf_result_io_error:         return "a read failed";
        case cgltf_result_out_of_memory:    return "out of memory";
        case cgltf_result_legacy_gltf:      return "glTF 1.0, which is not supported";
        default:                            return "cgltf refused it";
    }
}

/**
 * @brief Why a validated glTF file still cannot be read, if it cannot.
 *
 * cgltf returns zeros for what it does not decode (sparse, compressed), which would cook as the
 * model. Morph targets are not checked, since nothing reads them.
 *
 * @param data A file cgltf parsed, loaded and validated.
 * @return Empty when it reads; otherwise the reason, for the log.
 */
std::string unreadableReason(const cgltf_data& data) {
    // Allowed because cgltf's readers decode it: normalised integers in place of floats.
    constexpr const char* DECODED_EXTENSIONS[] = {"KHR_mesh_quantization"};
    for (cgltf_size e = 0; e < data.extensions_required_count; ++e) {
        const std::string name = data.extensions_required[e];
        const auto decoded = [&](const char* known) { return name == known; };
        if (std::none_of(std::begin(DECODED_EXTENSIONS), std::end(DECODED_EXTENSIONS), decoded)) {
            return "it requires " + name + ", which this importer does not decode";
        }
    }

    const auto undecoded = [](const cgltf_accessor* accessor) {
        if (!accessor) return false;
        return accessor->is_sparse || (accessor->buffer_view && !accessor->buffer_view->buffer->data);
    };
    const char* const NOT_DECODED = "it stores geometry, a skin or an animation in a sparse or compressed "
                                    "accessor, which this importer does not decode";
    for (cgltf_size m = 0; m < data.meshes_count; ++m) {
        for (cgltf_size p = 0; p < data.meshes[m].primitives_count; ++p) {
            const cgltf_primitive& primitive = data.meshes[m].primitives[p];
            if (undecoded(primitive.indices)) return NOT_DECODED;
            for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                if (undecoded(primitive.attributes[a].data)) return NOT_DECODED;
            }
        }
    }
    for (cgltf_size s = 0; s < data.skins_count; ++s) {
        if (undecoded(data.skins[s].inverse_bind_matrices)) return NOT_DECODED;
    }
    for (cgltf_size a = 0; a < data.animations_count; ++a) {
        for (cgltf_size s = 0; s < data.animations[a].samplers_count; ++s) {
            const cgltf_animation_sampler& sampler = data.animations[a].samplers[s];
            if (undecoded(sampler.input) || undecoded(sampler.output)) return NOT_DECODED;
        }
    }
    return {};
}

std::unique_ptr<SourceModel> parseGltf(const std::string& path, const std::string& ref) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result == cgltf_result_success) result = cgltf_load_buffers(&options, data, path.c_str());
    // Every accessor and view is bounds-checked before one is read.
    if (result == cgltf_result_success) result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        LOG_ERROR("Model load failed '%s': %s", ref.c_str(), describe(result));
        cgltf_free(data);
        return nullptr;
    }
    const std::string unreadable = unreadableReason(*data);
    if (!unreadable.empty()) {
        LOG_ERROR("Model load failed '%s': %s", ref.c_str(), unreadable.c_str());
        cgltf_free(data);
        return nullptr;
    }
    GltfReader reader(ref, data);
    auto model = std::make_unique<SourceModel>(reader.read());
    cgltf_free(data);
    return model;
}

/**
 * @brief Read an FBX or OBJ file with ufbx.
 *
 * Converted to right-handed, +Y up, metres, the scale folded into geometry and transforms so a
 * centimetre rig has unit scale on every joint. Pivots stay in node transforms, so a joint is
 * placed identically across exports. Meshes are numbered by node depth-first, then by material
 * in first-use order (the FBX SDK's); materials by first use along the same walk.
 */
class UfbxReader {
    public:
        UfbxReader(const std::string& ref, const ufbx_scene* scene)
            : m_ref(ref)
            , m_scene(scene)
        {}
        ~UfbxReader() = default;

        UfbxReader(const UfbxReader& other) = delete;
        UfbxReader& operator=(const UfbxReader& other) = delete;

        UfbxReader(UfbxReader && other) = delete;
        UfbxReader& operator=(UfbxReader && other) = delete;

    public:
        SourceModel read() {
            m_nodeIndex.assign(m_scene->nodes.count, -1);
            addNode(m_scene->root_node, -1);
            // After every node has its index: a skin names joints anywhere in the tree.
            for (const auto& [index, node] : m_meshNodes) m_model.nodes[index].meshes = meshesOf(node);
            readClips();
            return std::move(m_model);
        }

    private:
        static std::string text(const ufbx_string& s) { return std::string(s.data, s.length); }

        static glm::vec3 vec3(const ufbx_vec3& v) {
            return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
        }
        static glm::quat quat(const ufbx_quat& q) {
            return glm::quat(
                static_cast<float>(q.w),
                static_cast<float>(q.x),
                static_cast<float>(q.y),
                static_cast<float>(q.z)
            );
        }
        static glm::mat4 mat4(const ufbx_matrix& m) {
            glm::mat4 out(1.0f);
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 3; ++r) out[c][r] = static_cast<float>(m.cols[c].v[r]);
            }
            return out;
        }

        void addNode(const ufbx_node* node, int32_t parent) {
            const auto index = static_cast<uint32_t>(m_model.nodes.size());
            m_nodeIndex[node->typed_id] = static_cast<int32_t>(index);

            SourceNode out;
            out.name   = text(node->name);
            out.parent = parent;
            out.local.position = vec3(node->local_transform.translation);
            out.local.rotation = quat(node->local_transform.rotation);
            out.local.scale    = vec3(node->local_transform.scale);
            m_model.nodes.push_back(std::move(out));
            if (parent >= 0) m_model.nodes[static_cast<size_t>(parent)].children.push_back(index);

            if (node->mesh) m_meshNodes.push_back({index, node});
            for (size_t c = 0; c < node->children.count; ++c) {
                addNode(node->children.data[c], static_cast<int32_t>(index));
            }
        }

        // One SourceMesh per material part, made at first sight; later instances share them.
        std::vector<uint32_t> meshesOf(const ufbx_node* node) {
            const ufbx_mesh* mesh = node->mesh;
            const auto known = m_meshes.find(mesh);
            if (known != m_meshes.end()) return known->second;

            std::vector<uint32_t> out;
            for (size_t k = 0; k < mesh->material_part_usage_order.count; ++k) {
                const uint32_t partIndex = mesh->material_part_usage_order.data[k];
                const ufbx_mesh_part& part = mesh->material_parts.data[partIndex];
                if (part.num_faces == 0) continue;
                const ufbx_material* material = part.index < node->materials.count
                    ? node->materials.data[part.index]
                    : nullptr;
                out.push_back(static_cast<uint32_t>(m_model.meshes.size()));
                const std::string label = meshLabel(m_ref, m_model.meshes.size());
                m_model.meshes.push_back(readPart(*mesh, part, materialIndex(material), label));
            }
            m_meshes.emplace(mesh, out);
            return out;
        }

        SourceMesh readPart(
            const ufbx_mesh& mesh,
            const ufbx_mesh_part& part,
            uint32_t material,
            const std::string& label
        ) {
            SourceMesh out;
            out.material = material;

            const ufbx_skin_deformer* skin = mesh.skin_deformers.count
                ? mesh.skin_deformers.data[0]
                : nullptr;
            if (mesh.skin_deformers.count > 1) {
                LOG_WARNING(
                    "%s: %zu skins; the first is imported",
                    label.c_str(),
                    static_cast<size_t>(mesh.skin_deformers.count)
                );
            }
            if (skin && skin->clusters.count > AssetCook::MAX_SKELETON_BONES) {
                LOG_ERROR(
                    "%s: its skin names %zu joints, past the %u the cooked format admits; "
                    "imported unskinned",
                    label.c_str(),
                    static_cast<size_t>(skin->clusters.count),
                    AssetCook::MAX_SKELETON_BONES
                );
                skin = nullptr;
            }
            if (skin) {
                for (size_t c = 0; c < skin->clusters.count; ++c) {
                    const ufbx_skin_cluster* cluster = skin->clusters.data[c];
                    out.bones.push_back({
                        static_cast<uint32_t>(m_nodeIndex[cluster->bone_node->typed_id]),
                        mat4(cluster->geometry_to_bone)
                    });
                }
            }

            Corners corners;
            corners.hasUVs      = mesh.vertex_uv.exists;
            corners.hasTangents = mesh.vertex_tangent.exists && mesh.vertex_bitangent.exists;
            std::vector<uint32_t> triangle(mesh.max_face_triangles * 3);
            // Quantised once per control point, the first time a corner uses it.
            std::vector<SkinVertex> skinOf(skin ? mesh.num_vertices : 0);
            std::vector<bool>       quantised(skinOf.size(), false);
            std::vector<VertexInfluence> influences;
            SkinReport report;
            for (size_t f = 0; f < part.face_indices.count; ++f) {
                const ufbx_face face = mesh.faces.data[part.face_indices.data[f]];
                const uint32_t triangles = ufbx_triangulate_face(
                    triangle.data(),
                    triangle.size(),
                    &mesh,
                    face
                );
                for (uint32_t c = 0; c < triangles * 3; ++c) {
                    const uint32_t ix = triangle[c];
                    Vertex v;
                    v.position = vec3(ufbx_get_vertex_vec3(&mesh.vertex_position, ix));
                    v.normal   = mesh.vertex_normal.exists
                        ? vec3(ufbx_get_vertex_vec3(&mesh.vertex_normal, ix))
                        : glm::vec3(0.0f, 1.0f, 0.0f);
                    v.uv = glm::vec2(0.0f);
                    if (mesh.vertex_uv.exists) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, ix);
                        v.uv = glm::vec2(static_cast<float>(uv.x), static_cast<float>(uv.y));
                    }
                    v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    if (corners.hasTangents) {
                        const glm::vec3 t = vec3(ufbx_get_vertex_vec3(&mesh.vertex_tangent, ix));
                        const glm::vec3 b = vec3(ufbx_get_vertex_vec3(&mesh.vertex_bitangent, ix));
                        v.tangent = glm::vec4(t, glm::dot(glm::cross(v.normal, t), b) < 0.0f ? -1.0f : 1.0f);
                    }
                    corners.vertices.push_back(v);

                    if (!skin) continue;
                    const uint32_t vertex = mesh.vertex_indices.data[ix];
                    if (!quantised[vertex]) {
                        influences.clear();
                        if (vertex < skin->vertices.count) {
                            const ufbx_skin_vertex& weights = skin->vertices.data[vertex];
                            for (uint32_t w = 0; w < weights.num_weights; ++w) {
                                const ufbx_skin_weight& weight = skin->weights.data[weights.weight_begin + w];
                                influences.push_back({
                                    static_cast<uint16_t>(weight.cluster_index),
                                    static_cast<float>(weight.weight)
                                });
                            }
                        }
                        skinOf[vertex]    = quantiseInfluences(influences);
                        quantised[vertex] = true;
                        if (skinOf[vertex].weights[0] == 0) ++report.unweighted;
                    }
                    corners.skin.push_back(skinOf[vertex]);
                }
            }
            logSkinReport(report, label);
            out.geometry = weldCorners(std::move(corners), label);
            return out;
        }

        uint32_t imageIndex(const ufbx_texture* texture) {
            if (texture->type != UFBX_TEXTURE_FILE && texture->file_textures.count) {
                texture = texture->file_textures.data[0];
            }
            const auto known = m_images.find(texture);
            if (known != m_images.end()) return known->second;

            SourceImage out;
            if (texture->content.size) {
                const auto* bytes = static_cast<const unsigned char*>(texture->content.data);
                out.embedded.assign(bytes, bytes + texture->content.size);
                out.ref = "*" + std::to_string(texture->typed_id);
            } else {
                const ufbx_string& file = texture->relative_filename.length
                    ? texture->relative_filename
                    : texture->absolute_filename;
                out.ref = text(file);
                std::replace(out.ref.begin(), out.ref.end(), '\\', '/');
            }
            const auto index = static_cast<uint32_t>(m_model.images.size());
            m_model.images.push_back(std::move(out));
            m_images.emplace(texture, index);
            return index;
        }

        void mapTexture(
            SourceMaterial& material,
            const ufbx_texture* texture,
            TextureHandle MaterialAsset::* slot,
            TextureUsage usage
        ) {
            if (texture) material.maps.push_back({slot, usage, imageIndex(texture)});
        }

        uint32_t materialIndex(const ufbx_material* material) {
            const auto known = m_materials.find(material);
            if (known != m_materials.end()) return known->second;

            SourceMaterial out;
            if (material) {
                MaterialAsset& v = out.values;
                const ufbx_material_pbr_maps& pbr = material->pbr;
                // Unstated values keep the engine's default, not ufbx's stand-in.
                const auto stated = [](const ufbx_material_map& map, float fallback) {
                    return map.has_value ? static_cast<float>(map.value_real) : fallback;
                };
                v.albedo = glm::vec4(
                    vec3(pbr.base_color.value_vec3) * static_cast<float>(pbr.base_factor.value_real),
                    stated(pbr.opacity, 1.0f)
                );
                v.emission  = vec3(pbr.emission_color.value_vec3)
                    * static_cast<float>(pbr.emission_factor.value_real);
                v.metallic  = stated(pbr.metalness, v.metallic);
                v.roughness = stated(pbr.roughness, v.roughness);
                v.type = v.albedo.a < 0.999f ? MaterialType::Transparent : MaterialType::Opaque;

                mapTexture(out, pbr.base_color.texture, &MaterialAsset::albedoTexture, TextureUsage::Color);
                mapTexture(
                    out,
                    pbr.normal_map.texture ? pbr.normal_map.texture : material->fbx.bump.texture,
                    &MaterialAsset::normalTexture,
                    TextureUsage::Normal
                );
                mapTexture(
                    out,
                    pbr.emission_color.texture,
                    &MaterialAsset::emissionTexture,
                    TextureUsage::Color
                );
                mapTexture(out, pbr.ambient_occlusion.texture, &MaterialAsset::aoTexture, TextureUsage::Data);
                mapTexture(out, pbr.metalness.texture, &MaterialAsset::metallicTexture, TextureUsage::Data);
                mapTexture(out, pbr.roughness.texture, &MaterialAsset::roughnessTexture, TextureUsage::Data);
            }
            const auto index = static_cast<uint32_t>(m_model.materials.size());
            m_model.materials.push_back(std::move(out));
            m_materials.emplace(material, index);
            return index;
        }

        // Every take that animates anything, resampled by ufbx for a linear sampler with pivots and
        // units applied. An empty take ("Take 001") is not a clip; counting it would renumber the rest.
        void readClips() {
            for (size_t s = 0; s < m_scene->anim_stacks.count; ++s) {
                const ufbx_anim_stack* stack = m_scene->anim_stacks.data[s];
                size_t animated = 0;
                for (size_t l = 0; l < stack->layers.count; ++l) {
                    animated += stack->layers.data[l]->anim_props.count;
                }
                if (animated == 0) continue;
                ufbx_bake_opts options{};
                options.trim_start_time = true;
                ufbx_error error{};
                ufbx_baked_anim* baked = ufbx_bake_anim(m_scene, stack->anim, &options, &error);
                SourceClip clip;
                if (!baked) {
                    char message[256];
                    ufbx_format_error(message, sizeof(message), &error);
                    LOG_ERROR(
                        "Model '%s': take '%s' did not bake: %s",
                        m_ref.c_str(),
                        text(stack->name).c_str(),
                        message
                    );
                    m_model.clips.push_back(std::move(clip));
                    continue;
                }
                clip.duration = static_cast<float>(baked->playback_duration);
                for (size_t n = 0; n < baked->nodes.count; ++n) {
                    const ufbx_baked_node& node = baked->nodes.data[n];
                    const int32_t index = m_nodeIndex[node.typed_id];
                    if (index < 0) continue;
                    SourceChannel channel;
                    channel.node = static_cast<uint32_t>(index);
                    for (size_t k = 0; k < node.translation_keys.count; ++k) {
                        const ufbx_baked_vec3& key = node.translation_keys.data[k];
                        channel.positionTimes.push_back(static_cast<float>(key.time));
                        channel.positions.push_back(vec3(key.value));
                    }
                    for (size_t k = 0; k < node.rotation_keys.count; ++k) {
                        channel.rotationTimes.push_back(static_cast<float>(node.rotation_keys.data[k].time));
                        channel.rotations.push_back(quat(node.rotation_keys.data[k].value));
                    }
                    for (size_t k = 0; k < node.scale_keys.count; ++k) {
                        channel.scaleTimes.push_back(static_cast<float>(node.scale_keys.data[k].time));
                        channel.scales.push_back(vec3(node.scale_keys.data[k].value));
                    }
                    clip.channels.push_back(std::move(channel));
                }
                ufbx_free_baked_anim(baked);
                m_model.clips.push_back(std::move(clip));
            }
        }

    private:
        std::string        m_ref;
        const ufbx_scene*  m_scene = nullptr;
        SourceModel        m_model;

        std::vector<int32_t> m_nodeIndex;  ///< ufbx node typed id -> SourceModel node.
        std::unordered_map<const ufbx_mesh*, std::vector<uint32_t>> m_meshes;
        std::unordered_map<const ufbx_material*, uint32_t>          m_materials;
        std::unordered_map<const ufbx_texture*, uint32_t>           m_images;
        std::vector<std::pair<uint32_t, const ufbx_node*>>          m_meshNodes;  ///< In walk order.
};

std::unique_ptr<SourceModel> parseUfbx(const std::string& path, const std::string& ref) {
    ufbx_load_opts options{};
    options.target_axes                 = ufbx_axes_right_handed_y_up;
    options.target_unit_meters          = 1.0f;
    options.space_conversion            = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    options.inherit_mode_handling       = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    options.pivot_handling              = UFBX_PIVOT_HANDLING_RETAIN;
    options.generate_missing_normals    = true;
    options.clean_skin_weights          = true;
    // An OBJ's materials are in the .mtl beside it.
    options.load_external_files           = true;
    options.ignore_missing_external_files = true;
    options.obj_search_mtl_by_filename    = true;

    ufbx_error error{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &options, &error);
    if (!scene) {
        char message[512];
        ufbx_format_error(message, sizeof(message), &error);
        LOG_ERROR("Model load failed '%s': %s", ref.c_str(), message);
        return nullptr;
    }
    UfbxReader reader(ref, scene);
    auto model = std::make_unique<SourceModel>(reader.read());
    ufbx_free_scene(scene);
    return model;
}

} // namespace

std::unique_ptr<SourceModel> parseModel(const std::string& path, const std::string& ref) {
    PROFILE_SCOPE("ModelParse");
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    if (extension == ".gltf" || extension == ".glb") return parseGltf(path, ref);
    return parseUfbx(path, ref);
}

namespace {

/**
 * @brief When one file on disk was last written, as far as the cache can tell.
 */
struct FileStamp {
    std::uintmax_t                  size = 0;
    std::filesystem::file_time_type written{};

    bool operator==(const FileStamp& other) const {
        return size == other.size && written == other.written;
    }
};

// Every file the import reads, stamped; an unstattable file is stamped zero, which no real one is.
std::vector<FileStamp> stampFiles(const std::vector<std::filesystem::path>& files) {
    std::vector<FileStamp> stamps;
    stamps.reserve(files.size());
    for (const std::filesystem::path& file : files) {
        std::error_code ec;
        FileStamp stamp;
        stamp.size    = std::filesystem::file_size(file, ec);
        if (ec) stamp.size = 0;
        stamp.written = std::filesystem::last_write_time(file, ec);
        if (ec) stamp.written = {};
        stamps.push_back(stamp);
    }
    return stamps;
}

using ParsedModel = std::shared_ptr<const SourceModel>;

/**
 * @brief LRU cache of parsed model files, keyed by path and by the stamps of the
 *        files the parse read.
 *
 * Holds a future, so a request waits for another worker's parse of the same file while other
 * files parse outside the lock.
 */
class ModelCache {
    public:
        ModelCache() = default;
        ~ModelCache() = default;

        ModelCache(const ModelCache& other) = delete;
        ModelCache& operator=(const ModelCache& other) = delete;

        ModelCache(ModelCache && other) = delete;
        ModelCache& operator=(ModelCache && other) = delete;

    public:
        /**
         * @brief The parsed file, parsed now if these bytes have not been.
         *
         * @param path The model file on this machine.
         * @param ref Its project reference, which the log lines name.
         * @return The model, or null when the file does not parse (logged).
         */
        ParsedModel get(const std::string& path, const std::string& ref) {
            std::error_code ec;
            const auto canonicalPath = std::filesystem::weakly_canonical(path, ec);
            const std::string canonical = ec ? path : canonicalPath.string();

            std::promise<ParsedModel>       promise;
            std::shared_future<ParsedModel> cached;
            uint64_t                        ticket = 0;
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                const auto it = std::find_if(
                    m_entries.begin(),
                    m_entries.end(),
                    [&](const Entry& e) { return e.path == canonical; }
                );
                // Against every file the parse listed, not only the .gltf's own bytes. An entry with
                // none is a parse still running, to wait for.
                if (it != m_entries.end() && (it->files.empty() || stampFiles(it->files) == it->stamps)) {
                    cached = it->model;
                    std::rotate(m_entries.begin(), it, it + 1);
                } else {
                    if (it != m_entries.end()) m_entries.erase(it);
                    ticket = ++m_lastTicket;
                    Entry entry{canonical, {}, {}, ticket, promise.get_future().share()};
                    m_entries.insert(m_entries.begin(), std::move(entry));
                    if (m_entries.size() > MAX_CACHED) {
                        m_entries.erase(m_entries.begin() + MAX_CACHED, m_entries.end());
                    }
                }
            }
            // Outside the lock: the parse takes it again to record its stamps.
            if (cached.valid()) return cached.get();

            // Stamped before the parse, so a write during it leaves a stale stamp, not a fresh one
            // over old bytes.
            const std::vector<std::filesystem::path> files = AssetCooker::sourceFilesAt(path);
            std::vector<FileStamp> stamps = stampFiles(files);
            ParsedModel model;
            try {
                model = parseModel(path, ref);
            } catch (...) {
                forget(ticket);
                promise.set_exception(std::current_exception());
                throw;
            }

            if (!model) {
                forget(ticket);
            } else {
                const std::lock_guard<std::mutex> lock(m_mutex);
                for (Entry& entry : m_entries) {
                    if (entry.ticket != ticket) continue;
                    entry.files  = files;
                    entry.stamps = std::move(stamps);
                }
            }
            promise.set_value(model);
            return model;
        }

    private:
        struct Entry {
            std::string                        path;
            std::vector<std::filesystem::path> files;   ///< Empty while its parse runs.
            std::vector<FileStamp>             stamps;  ///< One per file, as the parse found them.
            uint64_t                           ticket = 0;
            std::shared_future<ParsedModel>    model;
        };

        // Sized for an import batch while keeping retained memory bounded.
        static constexpr size_t MAX_CACHED = 8;

    private:
        // A file that did not parse is not cached: the next request tries again.
        void forget(uint64_t ticket) {
            const std::lock_guard<std::mutex> lock(m_mutex);
            const auto finished = std::remove_if(
                m_entries.begin(),
                m_entries.end(),
                [&](const Entry& e) { return e.ticket == ticket; }
            );
            m_entries.erase(finished, m_entries.end());
        }

    private:
        std::vector<Entry> m_entries;
        uint64_t           m_lastTicket = 0;
        std::mutex         m_mutex;
};

} // namespace

std::shared_ptr<const SourceModel> loadSourceModel(const std::string& path, const std::string& ref) {
    static ModelCache s_cache;
    return s_cache.get(path, ref);
}

} // namespace Vkm::Engine
