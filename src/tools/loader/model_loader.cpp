#include "model_loader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>   // AI_MATKEY_GLTF_ALPHAMODE / ALPHACUTOFF

// Assimp's headers can drag in platform headers that #define ERROR / near /
// far (wingdi) which collide with logger.h's LogLevel::ERROR. Guard before
// including any engine header.
#ifdef ERROR
  #undef ERROR
#endif
#ifdef near
  #undef near
#endif
#ifdef far
  #undef far
#endif

#include "stb_image.h"   // declarations only; impl is in texture_loaders.cpp

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"
#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/name.h"
#include "ecs/component/mesh.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine {

namespace {

    // Identical between live import and the persistence factories so Assimp's
    // global mesh/material indices stay stable for a given file.
    constexpr unsigned kPostProcess =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenUVCoords |
        aiProcess_ImproveCacheLocality;

    std::string stemOf(const std::string& path) {
        return std::filesystem::path(path).stem().string();
    }

    std::string meshName(const std::string& path, int idx) {
        return stemOf(path) + ":mesh" + std::to_string(idx);
    }
    std::string materialName(const std::string& path, int idx) {
        return stemOf(path) + (idx >= 0
            ? ":mat" + std::to_string(idx) : std::string(":mat_default"));
    }

    glm::vec4 toVec4(const aiColor4D& c) { return {c.r, c.g, c.b, c.a}; }
    glm::vec3 toVec3(const aiColor3D& c) { return {c.r, c.g, c.b}; }

    MeshAsset buildMesh(const aiScene* scene, const std::string& path, int meshIdx) {
        MeshAsset out;
        if (!scene || meshIdx < 0 || meshIdx >= static_cast<int>(scene->mNumMeshes))
            return out;
        const aiMesh* m = scene->mMeshes[meshIdx];
        if (!m || m->mNumVertices == 0) return out;

        const bool hasN = m->HasNormals();
        const bool hasU = m->HasTextureCoords(0);
        const bool hasT = m->HasTangentsAndBitangents();

        out.vertices.resize(m->mNumVertices);
        for (unsigned i = 0; i < m->mNumVertices; ++i) {
            Vertex& v = out.vertices[i];
            const aiVector3D& p = m->mVertices[i];
            v.position = glm::vec3(p.x, p.y, p.z);
            if (hasN) {
                const aiVector3D& n = m->mNormals[i];
                v.normal = glm::vec3(n.x, n.y, n.z);
            } else {
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            if (hasU) {
                // Raw Assimp UV. Imported textures use REPEAT wrap, so
                // negative V (Assimp's glTF importer sometimes emits it) and
                // tiled UVs both resolve correctly in hardware - no munging.
                const aiVector3D& t = m->mTextureCoords[0][i];
                v.uv = glm::vec2(t.x, t.y);
            } else {
                v.uv = glm::vec2(0.0f);
            }
            if (hasT) {
                const aiVector3D& tg = m->mTangents[i];
                const aiVector3D& bt = m->mBitangents[i];
                const glm::vec3 T(tg.x, tg.y, tg.z);
                const glm::vec3 B(bt.x, bt.y, bt.z);
                const float w = glm::dot(glm::cross(v.normal, T), B) < 0.0f
                    ? -1.0f : 1.0f;
                v.tangent = glm::vec4(T, w);
            } else {
                v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
        }

        out.indices.reserve(static_cast<size_t>(m->mNumFaces) * 3);
        for (unsigned f = 0; f < m->mNumFaces; ++f) {
            const aiFace& face = m->mFaces[f];
            for (unsigned k = 0; k < face.mNumIndices; ++k)
                out.indices.push_back(face.mIndices[k]);
        }

        out.name   = meshName(path, meshIdx);
        out.source = { {"kind", "model"}, {"path", path}, {"mesh", meshIdx} };
        out.computeAndSetBounds();
        return out;
    }

    // Decode raw RGBA8 bytes into a (cached, idempotent) TextureAsset.
    TextureHandle addTexture(ResourceManager& res, const std::string& name,
                             int w, int h, const unsigned char* rgba, bool srgb) {
        if (TextureHandle e = res.findByName<TextureAsset>(name)) return e;
        TextureAsset tex;
        tex.params.width          = static_cast<uint32_t>(w);
        tex.params.height         = static_cast<uint32_t>(h);
        tex.params.internalFormat = srgb ? TextureInternalFormat::SRGBA8
                                         : TextureInternalFormat::RGBA8;
        tex.params.format          = TexturePixelFormat::RGBA;
        tex.params.type            = TexturePixelType::UnsignedByte;
        // Models routinely tile and reference UVs outside [0,1] (and Assimp's
        // glTF importer can emit negative V); REPEAT resolves all of that in
        // hardware. Default ClampToEdge would smear/clamp instead.
        tex.params.wrapS           = TextureWrapMode::Repeat;
        tex.params.wrapT           = TextureWrapMode::Repeat;
        tex.params.generateMipmaps = true;
        tex.srgb     = srgb;
        tex.name     = name;
        tex.pixelData.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
        tex.source   = { {"kind", "model-image"}, {"name", name} };
        return res.add(std::move(tex));
    }

    // Resolve one material texture slot (embedded or external file) to a
    // TextureHandle, decoded with the engine's flip convention (see below).
    TextureHandle textureFor(const aiScene* scene, const aiMaterial* mat,
                             aiTextureType type, bool srgb,
                             const std::string& modelPath, ResourceManager& res,
                             std::unordered_map<std::string, TextureHandle>& cache) {
        if (mat->GetTextureCount(type) == 0) return {};
        aiString ref;
        if (mat->GetTexture(type, 0, &ref) != AI_SUCCESS) return {};
        const std::string key = std::string(ref.C_Str()) + (srgb ? "#s" : "#l");
        if (auto it = cache.find(key); it != cache.end()) return it->second;

        const std::string stem = stemOf(modelPath);
        // Decode flipped, exactly like the engine's loadTexture(): every
        // other texture in the engine uses this convention and the shader
        // samples raw UV, so imported models must match it.
        stbi_set_flip_vertically_on_load(true);

        // Embedded (GLB / FBX): "*<i>" or a name resolvable in mTextures.
        if (const aiTexture* emb = scene->GetEmbeddedTexture(ref.C_Str())) {
            const std::string nm = stem + ":emb:" + key;
            TextureHandle h;
            if (emb->mHeight == 0) {  // compressed blob (PNG/JPG/...)
                int w = 0, hh = 0, n = 0;
                unsigned char* px = stbi_load_from_memory(
                    reinterpret_cast<const unsigned char*>(emb->pcData),
                    static_cast<int>(emb->mWidth), &w, &hh, &n, 4);
                if (px) { h = addTexture(res, nm, w, hh, px, srgb); stbi_image_free(px); }
            } else {                  // raw aiTexel BGRA grid
                const int w = static_cast<int>(emb->mWidth);
                const int hh = static_cast<int>(emb->mHeight);
                std::vector<unsigned char> rgba(static_cast<size_t>(w) * hh * 4);
                for (size_t i = 0; i < static_cast<size_t>(w) * hh; ++i) {
                    rgba[i * 4 + 0] = emb->pcData[i].r;
                    rgba[i * 4 + 1] = emb->pcData[i].g;
                    rgba[i * 4 + 2] = emb->pcData[i].b;
                    rgba[i * 4 + 3] = emb->pcData[i].a;
                }
                h = addTexture(res, nm, w, hh, rgba.data(), srgb);
            }
            cache[key] = h;
            return h;
        }

        // External file, relative to the model directory.
        std::filesystem::path p(ref.C_Str());
        if (p.is_relative())
            p = std::filesystem::path(modelPath).parent_path() / p;
        const std::string abs = p.lexically_normal().string();
        int w = 0, hh = 0, n = 0;
        unsigned char* px = stbi_load(abs.c_str(), &w, &hh, &n, 4);
        if (!px) {
            LOG_WARNING("model texture not found: %s", abs.c_str());
            cache[key] = {};
            return {};
        }
        TextureHandle h = addTexture(res, stem + ":file:" + key, w, hh, px, srgb);
        stbi_image_free(px);
        cache[key] = h;
        return h;
    }

    MaterialHandle buildMaterial(const aiScene* scene, const std::string& path,
                                 int matIdx, ResourceManager& res) {
        const std::string nm = materialName(path, matIdx);
        if (MaterialHandle e = res.findByName<MaterialAsset>(nm)) return e;

        MaterialAsset out;
        out.name   = nm;
        out.source = { {"kind", "model"}, {"path", path}, {"material", matIdx} };

        if (scene && matIdx >= 0 && matIdx < static_cast<int>(scene->mNumMaterials)) {
            const aiMaterial* mt = scene->mMaterials[matIdx];

            aiColor4D base;
            if (mt->Get(AI_MATKEY_BASE_COLOR, base) == AI_SUCCESS)
                out.albedo = toVec4(base);
            else if (mt->Get(AI_MATKEY_COLOR_DIFFUSE, base) == AI_SUCCESS)
                out.albedo = toVec4(base);

            float f = 0.0f;
            if (mt->Get(AI_MATKEY_METALLIC_FACTOR, f) == AI_SUCCESS)  out.metallic  = f;
            if (mt->Get(AI_MATKEY_ROUGHNESS_FACTOR, f) == AI_SUCCESS) out.roughness = f;

            aiColor3D em;
            if (mt->Get(AI_MATKEY_COLOR_EMISSIVE, em) == AI_SUCCESS)
                out.emission = toVec3(em);

            float opacity = 1.0f;
            if (mt->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
                out.alpha = opacity;
            out.alpha = glm::min(out.alpha, out.albedo.a);

            // Advanced KHR PBR factors. Each is AI_SUCCESS-guarded so a
            // model that does not specify one keeps the MaterialAsset
            // default (all of which are no-ops in the shader).
            if (mt->Get(AI_MATKEY_REFRACTI, f) == AI_SUCCESS && f > 0.0f)
                out.ior = f;
            if (mt->Get(AI_MATKEY_TRANSMISSION_FACTOR, f) == AI_SUCCESS)
                out.transmission = f;

            // Classify type. glTF carries an explicit alphaMode that beats
            // the alpha-channel heuristics:
            //   "MASK"  -> AlphaMask (alpha-tested foliage / leaves; depth-
            //              writing in the opaque phase, no blending).
            //   "BLEND" -> Transparent (or sorted via the heuristics below
            //              when the asset is older / has no glTF metadata).
            // KHR_materials_transmission glass keeps alpha = 1, so the
            // heuristic also classifies on transmission > 0 so glass imports
            // as Transparent and the scene-behind refraction path lights up.
            aiString alphaMode;
            const bool hasAlphaMode = (mt->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS);
            float cutoff = 0.5f;  // glTF default
            mt->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff);
            if (hasAlphaMode && std::strcmp(alphaMode.C_Str(), "MASK") == 0) {
                out.type        = MaterialType::AlphaMask;
                out.alphaCutoff = cutoff;
            } else if (hasAlphaMode && std::strcmp(alphaMode.C_Str(), "BLEND") == 0) {
                out.type = MaterialType::Transparent;
            } else {
                out.type = (out.alpha < 0.999f || out.transmission > 0.001f)
                             ? MaterialType::Transparent
                             : MaterialType::Opaque;
            }
            if (mt->Get(AI_MATKEY_CLEARCOAT_FACTOR, f) == AI_SUCCESS)
                out.clearcoat = f;
            if (mt->Get(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, f) == AI_SUCCESS)
                out.clearcoatRoughness = f;
            if (mt->Get(AI_MATKEY_ANISOTROPY_FACTOR, f) == AI_SUCCESS)
                out.anisotropy = f;
            if (mt->Get(AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, f) == AI_SUCCESS)
                out.sheenRoughness = f;
            aiColor3D sheen;
            if (mt->Get(AI_MATKEY_SHEEN_COLOR_FACTOR, sheen) == AI_SUCCESS)
                out.sheenColor = toVec3(sheen);

            // KHR_materials_volume. thickness == 0 in glTF means thin-walled
            // (no absorption); leave defaults so the shader skips Beer-Lambert.
            // attenuationDistance defaults to +inf in glTF; we ship 1.0 so the
            // editor can tweak something visible without divide-by-zero risk.
            if (mt->Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, f) == AI_SUCCESS)
                out.thicknessFactor = f;
            if (mt->Get(AI_MATKEY_VOLUME_ATTENUATION_DISTANCE, f) == AI_SUCCESS && f > 0.0f)
                out.attenuationDistance = f;
            aiColor3D atten;
            if (mt->Get(AI_MATKEY_VOLUME_ATTENUATION_COLOR, atten) == AI_SUCCESS)
                out.attenuationColor = toVec3(atten);
            // KHR_materials_emissive_strength. Captured here, applied AFTER
            // the emissive-texture fallback below (so a dropped factor does
            // not zero out the strength too).
            float emInt = 1.0f;
            mt->Get(AI_MATKEY_EMISSIVE_INTENSITY, emInt);

            std::unordered_map<std::string, TextureHandle> cache;
            auto pick = [&](std::initializer_list<aiTextureType> types, bool srgb) {
                for (aiTextureType t : types) {
                    if (TextureHandle h = textureFor(scene, mt, t, srgb, path, res, cache))
                        return h;
                }
                return TextureHandle{};
            };
            out.albedoTexture = pick({aiTextureType_BASE_COLOR,
                                      aiTextureType_DIFFUSE}, true);
            out.normalTexture = pick({aiTextureType_NORMALS,
                                      aiTextureType_HEIGHT}, false);
            out.metallicRoughnessTexture = pick({aiTextureType_METALNESS,
                                      aiTextureType_DIFFUSE_ROUGHNESS}, false);
            out.aoTexture = pick({aiTextureType_AMBIENT_OCCLUSION,
                                  aiTextureType_LIGHTMAP}, false);
            out.emissionTexture = pick({aiTextureType_EMISSIVE,
                                        aiTextureType_EMISSION_COLOR}, true);

            // glTF multiplies the emissive texture by emissiveFactor. Assimp's
            // glTF importer frequently drops the factor; if a texture is bound
            // but the factor came back ~black, fall back to white so the glow
            // (DamagedHelmet vents/eyes, etc.) is not silently lost. Then
            // apply KHR_materials_emissive_strength last.
            if (out.emissionTexture &&
                out.emission.r < 1e-4f &&
                out.emission.g < 1e-4f &&
                out.emission.b < 1e-4f) {
                out.emission = glm::vec3(1.0f);
            }
            out.emission *= emInt;
            out.clearcoatTexture    = pick({aiTextureType_CLEARCOAT},    false);
            out.transmissionTexture = pick({aiTextureType_TRANSMISSION}, false);
            out.heightTexture       = pick({aiTextureType_DISPLACEMENT}, false);
        }
        return res.add(std::move(out));
    }

    Transform transformOf(const aiMatrix4x4& m) {
        aiVector3D   s, t;
        aiQuaternion r;
        m.Decompose(s, r, t);
        Transform tf;
        tf.position = glm::vec3(t.x, t.y, t.z);
        tf.rotation = glm::quat(r.w, r.x, r.y, r.z);
        tf.scale    = glm::vec3(s.x, s.y, s.z);
        return tf;
    }

} // namespace

MeshAsset loadModelMesh(const std::string& path, int meshIndex) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, kPostProcess);
    if (!scene) {
        LOG_ERROR("model load failed '%s': %s", path.c_str(),
            importer.GetErrorString());
        return {};
    }
    return buildMesh(scene, path, meshIndex);
}

MaterialHandle loadModelMaterial(const std::string& path, int materialIndex,
                                 ResourceManager& resources) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, kPostProcess);
    if (!scene) {
        LOG_ERROR("model load failed '%s': %s", path.c_str(),
            importer.GetErrorString());
        return {};
    }
    return buildMaterial(scene, path, materialIndex, resources);
}

EntityId importModelIntoScene(const std::string& path, ResourceManager& resources,
                              Scene& scene) {
    Assimp::Importer importer;
    const aiScene* aScene = importer.ReadFile(path, kPostProcess);
    if (!aScene || aScene->mNumMeshes == 0) {
        LOG_ERROR("model import failed '%s': %s", path.c_str(),
            importer.GetErrorString());
        return {};
    }

    // Cache assets once (idempotent by name), keyed by Assimp global index.
    std::unordered_map<int, MeshHandle>     meshes;
    std::unordered_map<int, MaterialHandle> materials;
    auto meshFor = [&](int idx) -> MeshHandle {
        auto it = meshes.find(idx);
        if (it != meshes.end()) return it->second;
        const std::string nm = meshName(path, idx);
        MeshHandle h = resources.findByName<MeshAsset>(nm);
        if (!h) {
            MeshAsset ma = buildMesh(aScene, path, idx);
            if (!ma.vertices.empty()) h = resources.add(std::move(ma));
        }
        meshes[idx] = h;
        return h;
    };
    auto materialFor = [&](int idx) -> MaterialHandle {
        auto it = materials.find(idx);
        if (it != materials.end()) return it->second;
        MaterialHandle h = buildMaterial(aScene, path, idx, resources);
        materials[idx] = h;
        return h;
    };

    Entity root = scene.createEntity();
    scene.add(root, Transform{});
    scene.add(root, Name(stemOf(path).c_str()));

    std::function<void(const aiNode*, EntityId)> spawn =
        [&](const aiNode* node, EntityId parent) {
        Entity e = scene.createEntity();
        scene.add(e, transformOf(node->mTransformation));
        scene.add(e, Name(node->mName.length ? node->mName.C_Str() : "node"));
        HierarchyOperations::setParent(scene, e.getID(), parent);

        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            const int mi = static_cast<int>(node->mMeshes[i]);
            MeshHandle mh = meshFor(mi);
            if (!mh) continue;
            MaterialHandle mat = materialFor(
                static_cast<int>(aScene->mMeshes[mi]->mMaterialIndex));
            if (node->mNumMeshes == 1) {
                scene.add(e, Mesh{mh, mat});
            } else {
                Entity sub = scene.createEntity();
                scene.add(sub, Transform{});
                scene.add(sub, Name(("mesh" + std::to_string(mi)).c_str()));
                scene.add(sub, Mesh{mh, mat});
                HierarchyOperations::setParent(scene, sub.getID(), e.getID());
            }
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c)
            spawn(node->mChildren[c], e.getID());
    };

    if (aScene->mRootNode)
        spawn(aScene->mRootNode, root.getID());

    LOG_INFO("Imported model '%s' (%u meshes, %u materials)",
        path.c_str(), aScene->mNumMeshes, aScene->mNumMaterials);
    return root.getID();
}

} // namespace Engine
