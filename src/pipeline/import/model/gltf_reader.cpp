#define VKM_LOG_CATEGORY "LOADER"

#include "import/model/gltf_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cgltf.h>

#include <glm/gtc/type_ptr.hpp>

#include "vkm_log/logger.h"
#include "import/model/mesh_corners.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine::ModelReaders {

namespace {

/**
 * @brief Give each triangle's corners its face's normal, the way it winds.
 *
 * @param corners A triangle list, three corners a triangle; one of no area keeps the normals it had.
 */
void flattenNormals(std::vector<Vertex>& corners) {
    for (size_t t = 0; t + 2 < corners.size(); t += 3) {
        const glm::vec3 across = glm::cross(
            corners[t + 1].position - corners[t].position,
            corners[t + 2].position - corners[t].position
        );
        const float area = glm::length(across);
        if (!(area > 0.0f)) continue;
        for (size_t c = 0; c < 3; ++c) corners[t + c].normal = across / area;
    }
}


/**
 * @brief Read a glTF or GLB file with cgltf.
 *
 * Already right-handed, +Y up, meters; V is flipped as read. Meshes are numbered by primitive in
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

            const char* named = material->name ? material->name : "a material";
            const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
            v.albedo    = glm::make_vec4(pbr.base_color_factor);
            v.metallic  = pbr.metallic_factor;
            v.roughness = pbr.roughness_factor;
            v.emission  = glm::make_vec3(material->emissive_factor);
            mapTexture(out, pbr.base_color_texture, &MaterialAsset::albedoTexture, TextureUsage::Color);
            mapTexture(out, pbr.metallic_roughness_texture, &MaterialAsset::metallicRoughnessTexture, TextureUsage::Data);
            mapTexture(out, material->normal_texture, &MaterialAsset::normalTexture, TextureUsage::Normal);
            mapTexture(out, material->occlusion_texture, &MaterialAsset::aoTexture, TextureUsage::Data);
            mapTexture(out, material->emissive_texture, &MaterialAsset::emissionTexture, TextureUsage::Color);
            if (material->normal_texture.texture) v.normalScale = material->normal_texture.scale;

            // KHR_texture_transform: the material reads every map at one uv, so the base color's
            // transform stands for all of them - the normal map's, with no base color; a rotation
            // or a texCoord override is not carried. Its offset is glTF's, along a V that runs down
            // the image, so it is turned for the flipped V the mesh is read with.
            const cgltf_texture_view& base =
                pbr.base_color_texture.texture ? pbr.base_color_texture : material->normal_texture;
            if (base.has_transform) {
                v.uvScale  = glm::make_vec2(base.transform.scale);
                v.uvOffset = {base.transform.offset[0], 1.0f - v.uvScale.y - base.transform.offset[1]};
            }
            // A map's transform as it applies: none is the identity.
            const auto applied = [](const cgltf_texture_view& view) {
                cgltf_texture_transform identity{};
                identity.scale[0] = 1.0f;
                identity.scale[1] = 1.0f;
                return view.has_transform ? view.transform : identity;
            };
            const cgltf_texture_transform common = applied(base);
            const auto apart = [&](const cgltf_texture_view& view) {
                if (!view.texture) return false;
                if (view.has_transform && view.transform.has_texcoord) return true;
                const cgltf_texture_transform own = applied(view);
                return own.rotation != 0.0f
                    || own.scale[0] != common.scale[0] || own.scale[1] != common.scale[1]
                    || own.offset[0] != common.offset[0] || own.offset[1] != common.offset[1];
            };
            const cgltf_texture_view* views[] = {
                &pbr.base_color_texture,
                &pbr.metallic_roughness_texture,
                &material->normal_texture,
                &material->occlusion_texture,
                &material->emissive_texture
            };
            const auto differs = [&](const cgltf_texture_view* view) { return apart(*view); };
            if (std::any_of(std::begin(views), std::end(views), differs)) {
                LOG_WARNING(
                    "%s: a map's texture transform rotates, overrides its texCoord or differs from the "
                    "base color's, which stands for all of them",
                    named
                );
            }

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
            // MaterialAsset has no sheen texture: its factors come across.
            if (material->has_sheen) {
                v.sheenColor     = glm::make_vec3(material->sheen.sheen_color_factor);
                v.sheenRoughness = material->sheen.sheen_roughness_factor;
                const cgltf_sheen& sheen = material->sheen;
                if (sheen.sheen_color_texture.texture || sheen.sheen_roughness_texture.texture) {
                    LOG_WARNING("%s: its sheen textures are not imported, its sheen factors are", named);
                }
            }

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
                    SourceMesh primitive    = readPrimitive(
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
            // By set, so the influences are gathered in set order, not a hash table's.
            std::map<cgltf_int, const cgltf_accessor*> joints;
            std::map<cgltf_int, const cgltf_accessor*> weights;
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
                if (skin->joints_count > SkeletonAsset::MAX_BONES) {
                    LOG_ERROR(
                        "%s: its skin names %zu joints, past the %u a rig may have; "
                        "imported unskinned",
                        label.c_str(),
                        static_cast<size_t>(skin->joints_count),
                        SkeletonAsset::MAX_BONES
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
                            float weight[4]     = {0.0f, 0.0f, 0.0f, 0.0f};
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
                        sourceSkin[i] = quantizeInfluences(influences);
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
            // Without normals the faces are flat, as glTF says, and tangents it carries are not read.
            if (!normals || normals->count != vertexCount) {
                flattenNormals(corners.vertices);
                corners.hasTangents = false;
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
                float time     = 0.0f;
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
    // Allowed because they are read: normalized integers in place of floats (cgltf decodes them),
    // and a map's texture transform (readMaterial).
    constexpr const char* DECODED_EXTENSIONS[] = {"KHR_mesh_quantization", "KHR_texture_transform"};
    for (cgltf_size e = 0; e < data.extensions_required_count; ++e) {
        const std::string name = data.extensions_required[e];
        const auto decoded     = [&](const char* known) { return name == known; };
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

} // namespace

std::unique_ptr<SourceModel> parseGltf(const std::string& path, const std::string& ref) {
    cgltf_options options{};
    cgltf_data* data    = nullptr;
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

} // namespace Vkm::Engine::ModelReaders
