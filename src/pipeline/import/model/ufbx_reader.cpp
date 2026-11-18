#define VKM_LOG_CATEGORY "LOADER"

#include "import/model/ufbx_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "vkm_log/logger.h"
#include "import/model/mesh_corners.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine::ModelReaders {

namespace {

/**
 * @brief Read an FBX or OBJ file with ufbx.
 *
 * Converted to right-handed, +Y up, meters, the scale folded into geometry and transforms so a
 * centimeter rig has unit scale on every joint. Pivots stay in node transforms, so a joint is
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
            out.name           = text(node->name);
            out.parent         = parent;
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
                const uint32_t partIndex   = mesh->material_part_usage_order.data[k];
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
            if (skin && skin->clusters.count > SkeletonAsset::MAX_BONES) {
                LOG_ERROR(
                    "%s: its skin names %zu joints, past the %u a rig may have; "
                    "imported unskinned",
                    label.c_str(),
                    static_cast<size_t>(skin->clusters.count),
                    SkeletonAsset::MAX_BONES
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
            // Quantized once per control point, the first time a corner uses it.
            std::vector<SkinVertex> skinOf(skin ? mesh.num_vertices : 0);
            std::vector<bool>       quantized(skinOf.size(), false);
            std::vector<VertexInfluence> influences;
            SkinReport report;
            for (size_t f = 0; f < part.face_indices.count; ++f) {
                const ufbx_face face     = mesh.faces.data[part.face_indices.data[f]];
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
                    if (!quantized[vertex]) {
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
                        skinOf[vertex]    = quantizeInfluences(influences);
                        quantized[vertex] = true;
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
                v.type      = v.albedo.a < 0.999f ? MaterialType::Transparent : MaterialType::Opaque;

                mapTexture(out, pbr.base_color.texture, &MaterialAsset::albedoTexture, TextureUsage::Color);
                mapTexture(out, pbr.normal_map.texture ? pbr.normal_map.texture : material->fbx.bump.texture, &MaterialAsset::normalTexture, TextureUsage::Normal);
                mapTexture(out, pbr.emission_color.texture, &MaterialAsset::emissionTexture, TextureUsage::Color);
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
        std::string       m_ref;
        const ufbx_scene* m_scene = nullptr;
        SourceModel       m_model;

        std::vector<int32_t> m_nodeIndex;  ///< ufbx node typed id -> SourceModel node.
        std::unordered_map<const ufbx_mesh*, std::vector<uint32_t>> m_meshes;
        std::unordered_map<const ufbx_material*, uint32_t>          m_materials;
        std::unordered_map<const ufbx_texture*, uint32_t>           m_images;
        std::vector<std::pair<uint32_t, const ufbx_node*>>          m_meshNodes;  ///< In walk order.
};

} // namespace

ufbx_load_opts ufbxOptions() {
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
    return options;
}

std::unique_ptr<SourceModel> parseUfbx(const std::string& path, const std::string& ref) {
    const ufbx_load_opts options = ufbxOptions();
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

} // namespace Vkm::Engine::ModelReaders
