#define VKM_LOG_CATEGORY "LOADER"

#include "loader/model_loaders.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>

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

#include "stb_image.h"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"
#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"
#include "io/asset/asset_cook.h"
#include "io/project_paths.h"
#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/name.h"
#include "ecs/component/animator.h"
#include "ecs/component/mesh.h"
#include "system/async/async_load_queue.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Vkm::Engine {

namespace {

// Identical between live import and the persistence factories so Assimp's
// global mesh/material indices stay stable for a given file.
constexpr unsigned POST_PROCESS_FLAGS =
    aiProcess_Triangulate |
    aiProcess_GenSmoothNormals |
    aiProcess_CalcTangentSpace |
    aiProcess_JoinIdenticalVertices |
    aiProcess_GenUVCoords |
    aiProcess_ImproveCacheLocality |
    // Caps a vertex at four influences and renormalises what is left, which is
    // exactly what SkinVertex holds - without it a fifth influence would be
    // dropped after the weights were already normalised against it.
    aiProcess_LimitBoneWeights |
    // Fills aiBone::mArmature and mNode, which is what makes "which rig does
    // this bone belong to" an answer Assimp gives rather than one guessed from
    // the node tree.
    aiProcess_PopulateArmatureData;

/**
 * @brief LRU cache of parsed Assimp scenes keyed by canonical path.
 *
 * The recipe factories re-import one aiMesh or one aiMaterial per
 * call, so a glTF with N meshes + M materials would otherwise be
 * parsed N+M times over. The Importer owns the aiScene; we hand
 * callers a shared_ptr so a concurrent eviction can't destroy the
 * scene out from under them mid-build.
 */
class ImporterCache {
    public:
        std::shared_ptr<Assimp::Importer> get(const std::string& path) {
            std::error_code ec;
            std::string canonical;
            {
                auto p = std::filesystem::weakly_canonical(path, ec);
                canonical = ec ? path : p.string();
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = std::find_if(m_entries.begin(), m_entries.end(),
                [&](const Entry& e) { return e.path == canonical; });
            if (it != m_entries.end()) {
                auto cached = it->importer;
                Entry moved = std::move(*it);
                m_entries.erase(it);
                m_entries.insert(m_entries.begin(), std::move(moved));
                return cached;
            }

            auto importer = std::make_shared<Assimp::Importer>();
            if (!importer->ReadFile(path, POST_PROCESS_FLAGS)) {
                // Log here while we still have the Importer that holds
                // the real error string. Callers would otherwise see a
                // null shared_ptr with no way to recover the cause.
                LOG_ERROR("Model load failed '%s': %s", path.c_str(),
                    importer->GetErrorString());
                return nullptr;
            }

            m_entries.insert(m_entries.begin(), Entry{canonical, importer});
            while (m_entries.size() > MAX_CACHED) m_entries.pop_back();
            return importer;
        }

    private:
        struct Entry {
            std::string path;
            std::shared_ptr<Assimp::Importer> importer;
        };
        // LRU cap. Sized for a typical asset-import batch: a file is
        // re-imported once per (mesh, material) an asset entry names,
        // and a single model can split across several sub-models. 8
        // entries comfortably covers the common case while keeping
        // retained aiScene memory bounded - each entry is a few MB.
        static constexpr size_t MAX_CACHED = 8;
        std::vector<Entry> m_entries;
        std::mutex m_mutex;
};

ImporterCache& importerCache() {
    static ImporterCache instance;
    return instance;
}

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
std::string skeletonName(const std::string& path) {
    return stemOf(path) + ":skeleton";
}
std::string clipName(const std::string& path, int idx) {
    return stemOf(path) + ":clip" + std::to_string(idx);
}

glm::vec4 toVec4(const aiColor4D& c) { return {c.r, c.g, c.b, c.a}; }
glm::vec3 toVec3(const aiColor3D& c) { return {c.r, c.g, c.b}; }
glm::vec3 toVec3(const aiVector3D& v) { return {v.x, v.y, v.z}; }

// Assimp stores matrices row-major, glm column-major.
glm::mat4 toMat4(const aiMatrix4x4& m) {
    return glm::mat4(m.a1, m.b1, m.c1, m.d1,
                     m.a2, m.b2, m.c2, m.d2,
                     m.a3, m.b3, m.c3, m.d3,
                     m.a4, m.b4, m.c4, m.d4);
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

/**
 * @brief Build the one rig a model file's skinned meshes are bound to.
 *
 * The bones are every aiBone any of the file's meshes names, plus the nodes
 * joining them down from their lowest common ancestor, emitted depth-first so
 * `parent < index` holds by construction - the invariant the cooked reader
 * re-checks and every pose walk relies on.
 *
 * @param scene Parsed Assimp scene.
 * @param path Project-relative model reference, used for the name and recipe.
 * @return The rig, or an empty SkeletonAsset when the file has no bones or its
 *         rig cannot be resolved to one connected tree.
 */
SkeletonAsset buildSkeleton(const aiScene* scene, const std::string& path) {
    SkeletonAsset out;
    if (!scene || !scene->mRootNode) return out;

    // Bone name -> inverse bind (Assimp's offset matrix: mesh space to bone
    // space at bind). A bone shared by two meshes carries the same offset in
    // both, so the first one seen stands.
    std::unordered_map<std::string, glm::mat4> offsets;
    const aiNode* armature = nullptr;
    bool disjoint = false;
    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];
        for (unsigned bi = 0; bi < m->mNumBones; ++bi) {
            const aiBone* bone = m->mBones[bi];
            offsets.emplace(bone->mName.C_Str(), toMat4(bone->mOffsetMatrix));
            if (!bone->mArmature) continue;
            if (!armature) armature = bone->mArmature;
            else if (armature != bone->mArmature) disjoint = true;
        }
    }
    if (offsets.empty()) return out;
    // aiProcess_PopulateArmatureData is what makes this answerable. Merging two
    // rigs would give them an invented shared root and one bone numbering, and
    // every clip in the file would then be bound to a rig neither of them is.
    if (disjoint) {
        LOG_ERROR("Model '%s': its bones belong to more than one armature; "
                  "a file has to hold one rig", path.c_str());
        return {};
    }

    // The chain from the scene root down to each bone. A bone Assimp named but
    // left out of the node tree has no local transform and cannot be posed, so
    // the rig is refused rather than emitted with a hole in it.
    std::vector<std::vector<const aiNode*>> chains;
    chains.reserve(offsets.size());
    for (const auto& entry : offsets) {
        const std::string& name = entry.first;
        const aiNode* node = scene->mRootNode->FindNode(name.c_str());
        if (!node) {
            LOG_ERROR("Model '%s': bone '%s' names no node in the file", path.c_str(), name.c_str());
            return {};
        }
        std::vector<const aiNode*> chain;
        for (const aiNode* walk = node; walk; walk = walk->mParent) chain.push_back(walk);
        std::reverse(chain.begin(), chain.end());
        chains.push_back(std::move(chain));
    }

    // The rig root is the deepest node every chain shares - the tightest node
    // containing all the bones. At least one is shared (the scene root), so the
    // prefix is never empty.
    size_t common = chains.front().size();
    for (const std::vector<const aiNode*>& chain : chains) {
        size_t i = 0;
        while (i < common && i < chain.size() && chain[i] == chains.front()[i]) ++i;
        common = i;
    }
    const aiNode* rigRoot = chains.front()[common - 1];

    std::unordered_set<const aiNode*> needed;
    for (const std::vector<const aiNode*>& chain : chains) {
        for (size_t i = common - 1; i < chain.size(); ++i) needed.insert(chain[i]);
    }

    // Depth-first from the rig root, skipping what no bone needs. The set is
    // connected by construction (it is built out of whole chains), so nothing
    // skipped here has a bone underneath it.
    std::function<void(const aiNode*, int32_t, const glm::mat4&)> emit =
        [&](const aiNode* node, int32_t parent, const glm::mat4& parentGlobal) {
        const glm::mat4 global = parentGlobal * toMat4(node->mTransformation);
        const auto index = static_cast<int32_t>(out.bones.size());
        out.bones.push_back({node->mName.C_Str(), parent});
        out.bindPose.push_back(transformOf(node->mTransformation));
        auto offset = offsets.find(node->mName.C_Str());
        // A joint that influences no vertex still has to sit in the array to
        // keep the chain connected. Nothing reads its inverse bind - only an
        // Assimp bone produces a weight - so the inverse of its own bind
        // transform stands in, which is what the offset matrix would be if the
        // mesh already sat in rig space.
        out.inverseBind.push_back(offset != offsets.end() ? offset->second : glm::inverse(global));
        for (unsigned c = 0; c < node->mNumChildren; ++c) {
            if (needed.count(node->mChildren[c])) emit(node->mChildren[c], index, global);
        }
    };
    emit(rigRoot, -1, glm::mat4(1.0f));

    if (out.bones.size() > AssetCook::MAX_SKELETON_BONES) {
        LOG_ERROR("Model '%s': its rig has %zu bones, past the %u the cooked format admits",
                  path.c_str(), out.bones.size(), AssetCook::MAX_SKELETON_BONES);
        return {};
    }

    out.name         = skeletonName(path);
    out.sourceJson() = { {"kind", "model"}, {"path", path} };
    return out;
}

// One influence on one vertex, before the four that survive are quantised.
struct VertexInfluence {
    uint16_t bone   = 0;
    float    weight = 0.0f;
};

/**
 * @brief Transpose @p m's bone-to-vertices weights into @p out's per-vertex
 *        skin stream, addressed against @p skeleton's bone order.
 *
 * @param m Assimp mesh carrying the bones and their weights.
 * @param skeleton Rig the bone names resolve against.
 * @param out Mesh being built; its vertices must already be filled.
 */
void appendSkin(const aiMesh* m, const SkeletonAsset& skeleton, MeshAsset& out) {
    std::vector<std::vector<VertexInfluence>> perVertex(m->mNumVertices);
    unsigned outOfRange = 0;
    unsigned offRig     = 0;
    for (unsigned bi = 0; bi < m->mNumBones; ++bi) {
        const aiBone* bone = m->mBones[bi];
        const int32_t index = skeleton.indexOf(bone->mName.C_Str());
        if (index < 0) { ++offRig; continue; }
        for (unsigned wi = 0; wi < bone->mNumWeights; ++wi) {
            const aiVertexWeight& weight = bone->mWeights[wi];
            // aiProcess_JoinIdenticalVertices merges on a key that omits skin
            // weights (Assimp's Vertex.h:106-111) and filters the merged-away
            // ones out (JoinVerticesProcess.cpp:343). Past that,
            // JoinVerticesProcess.cpp:354 only rewrites a bone's weight list
            // when the rewrite is non-empty - so a bone whose weights ALL
            // landed on joined vertices keeps its pre-join vertex ids against
            // the shrunken array. Following one is an out-of-bounds read of
            // Assimp's own data, which is why this is a bounds check and not
            // an assertion.
            if (weight.mVertexId >= m->mNumVertices) { ++outOfRange; continue; }
            if (weight.mWeight <= 0.0f) continue;
            perVertex[weight.mVertexId].push_back({static_cast<uint16_t>(index), weight.mWeight});
        }
    }

    // Bind-pose model-space origin of each bone, for the skin radius below.
    std::vector<glm::vec3> origins(skeleton.bones.size());
    for (size_t b = 0; b < skeleton.bones.size(); ++b) {
        origins[b] = glm::vec3(glm::inverse(skeleton.inverseBind[b])[3]);
    }

    out.skin.assign(m->mNumVertices, SkinVertex{});
    unsigned unweighted = 0;
    float    radius     = 0.0f;
    for (unsigned v = 0; v < m->mNumVertices; ++v) {
        std::vector<VertexInfluence>& influences = perVertex[v];
        std::sort(influences.begin(), influences.end(),
            [](const VertexInfluence& a, const VertexInfluence& b) { return a.weight > b.weight; });
        if (influences.size() > 4) influences.resize(4);

        SkinVertex& skin = out.skin[v];
        float total = 0.0f;
        for (const VertexInfluence& influence : influences) total += influence.weight;
        if (total <= 0.0f) {
            // Sum(w * M) with every w zero collapses the vertex onto the origin.
            // Binding it rigidly to the rig root leaves it where the artist put
            // it - wrong in a way that can be seen and fixed, rather than one
            // that reads as a broken importer.
            skin.weights[0] = 255;
            ++unweighted;
        } else {
            int quantised[4] = {0, 0, 0, 0};
            int sum = 0;
            for (size_t k = 0; k < influences.size(); ++k) {
                skin.bones[k] = influences[k].bone;
                quantised[k]  = static_cast<int>(std::lround(influences[k].weight / total * 255.0f));
                sum += quantised[k];
            }
            // The rounding error lands on the largest influence, which the sort
            // put first. The four bytes have to sum to exactly 255 so that
            // w / 255.0 sums to exactly 1.0 and no vertex stage renormalises.
            quantised[0] = std::clamp(quantised[0] + (255 - sum), 0, 255);
            for (int k = 0; k < 4; ++k) skin.weights[k] = static_cast<uint8_t>(quantised[k]);
        }

        // Both branches, because the fallback binding is a real influence too.
        // An under-sized radius under-sizes the posed bounds, and the occlusion
        // cull keeps conservatively - it does not over-draw, it deletes
        // geometry that was visible.
        const glm::vec3 position = out.vertices[v].position;
        for (int k = 0; k < 4; ++k) {
            if (skin.weights[k] == 0) continue;
            radius = std::max(radius, glm::distance(position, origins[skin.bones[k]]));
        }
    }

    if (outOfRange) {
        LOG_WARNING("Mesh '%s': dropped %u bone weight(s) naming vertices the mesh does not have "
                    "(Assimp's post-join weight list)", m->mName.C_Str(), outOfRange);
    }
    if (offRig) {
        LOG_WARNING("Mesh '%s': dropped %u bone(s) the rig does not hold", m->mName.C_Str(), offRig);
    }
    if (unweighted) {
        LOG_WARNING("Mesh '%s': %u vertex/vertices carry no influence; bound to the rig root",
                    m->mName.C_Str(), unweighted);
    }

    out.skeleton   = skeleton.name;
    out.skinRadius = radius;
}

/**
 * @brief Build one of @p scene's animations as a clip bound to @p skeleton.
 *
 * @param scene Parsed Assimp scene.
 * @param path Project-relative model reference, used for the name and recipe.
 * @param clipIdx Assimp global animation index.
 * @param skeleton Rig the channels are resolved against.
 * @return The clip, or an empty AnimationClipAsset when the index names nothing.
 */
AnimationClipAsset buildClip(const aiScene* scene, const std::string& path, int clipIdx,
                             const SkeletonAsset& skeleton) {
    AnimationClipAsset out;
    if (!scene || clipIdx < 0 || clipIdx >= static_cast<int>(scene->mNumAnimations)) return out;
    const aiAnimation* anim = scene->mAnimations[clipIdx];
    if (!anim || skeleton.bones.empty()) return out;

    // Exporters emit a zero tick rate constantly, and dividing by it would make
    // every key land at infinity. 25 is Assimp's own documented stand-in.
    const double ticksPerSecond = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0;
    const auto seconds = [ticksPerSecond](double ticks) {
        return static_cast<float>(ticks / ticksPerSecond);
    };

    out.skeleton = skeleton.name;
    out.duration = std::max(0.0f, seconds(anim->mDuration));
    out.bones.resize(skeleton.bones.size());

    unsigned dropped = 0;
    for (unsigned ci = 0; ci < anim->mNumChannels; ++ci) {
        const aiNodeAnim* channel = anim->mChannels[ci];
        const int32_t bone = skeleton.indexOf(channel->mNodeName.C_Str());
        // A channel for a node outside the rig - a camera, a prop, a mesh node
        // the exporter animated - has nowhere to land.
        if (bone < 0) { ++dropped; continue; }
        ClipBone& target = out.bones[static_cast<size_t>(bone)];

        target.position = {static_cast<uint32_t>(out.positions.size()), channel->mNumPositionKeys};
        for (unsigned k = 0; k < channel->mNumPositionKeys; ++k) {
            out.positionTimes.push_back(seconds(channel->mPositionKeys[k].mTime));
            out.positions.push_back(toVec3(channel->mPositionKeys[k].mValue));
        }
        target.rotation = {static_cast<uint32_t>(out.rotations.size()), channel->mNumRotationKeys};
        for (unsigned k = 0; k < channel->mNumRotationKeys; ++k) {
            const aiQuaternion& q = channel->mRotationKeys[k].mValue;
            out.rotationTimes.push_back(seconds(channel->mRotationKeys[k].mTime));
            out.rotations.push_back(glm::quat(q.w, q.x, q.y, q.z));
        }
        target.scale = {static_cast<uint32_t>(out.scales.size()), channel->mNumScalingKeys};
        for (unsigned k = 0; k < channel->mNumScalingKeys; ++k) {
            out.scaleTimes.push_back(seconds(channel->mScalingKeys[k].mTime));
            out.scales.push_back(toVec3(channel->mScalingKeys[k].mValue));
        }
    }
    if (dropped) {
        LOG_WARNING("Clip '%s': dropped %u channel(s) naming nodes outside the rig",
                    clipName(path, clipIdx).c_str(), dropped);
    }

    out.name         = clipName(path, clipIdx);
    out.sourceJson() = { {"kind", "model"}, {"path", path}, {"clip", clipIdx} };
    return out;
}

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
        // Triangles only. aiProcess_Triangulate splits polygons but leaves
        // point and line primitives alone, and some exporters emit them; pushing
        // a 1- or 2-index face into a triangle list shifts every triangle after
        // it, which shows up as geometry that is subtly and inexplicably wrong.
        if (face.mNumIndices != 3) continue;
        for (unsigned k = 0; k < 3; ++k)
            out.indices.push_back(face.mIndices[k]);
    }

    // The rig is rebuilt here rather than threaded in, because every caller of
    // buildMesh would otherwise have to build one first to pass it. It is a
    // walk of the node tree and it runs at import, not per frame.
    if (m->HasBones()) {
        const SkeletonAsset skeleton = buildSkeleton(scene, path);
        if (!skeleton.bones.empty()) appendSkin(m, skeleton, out);
    }

    out.name           = meshName(path, meshIdx);
    out.sourceJson()   = { {"kind", "model"}, {"path", path}, {"mesh", meshIdx} };
    out.computeAndSetBounds();
    return out;
}

// Decode raw RGBA8 bytes into a (cached, idempotent) TextureAsset. The source
// is stamped onto the asset so cold-start load can recreate the same texture
// via the recipe texture dispatch.
TextureHandle addTexture(
    ResourceManager& res,
    const std::string& name,
    int w,
    int h,
    const unsigned char* rgba,
    bool srgb,
    nlohmann::json source
) {
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
    tex.sourceJson() = std::move(source);
    return res.add(std::move(tex));
}

// Decode one embedded aiTexture into RGBA8 bytes and register it under
// @p name. Shared between the live importer (textureFor) and the
// model-image factory (loadModelEmbeddedTexture).
TextureHandle decodeEmbedded(
    const aiTexture* emb,
    ResourceManager& res,
    const std::string& name,
    const std::string& modelPath,
    const std::string& ref,
    bool srgb
) {
    if (emb->mHeight == 0) {  // compressed blob (PNG/JPG/...)
        int w = 0, hh = 0, n = 0;
        unsigned char* px = stbi_load_from_memory(
            reinterpret_cast<const unsigned char*>(emb->pcData),
            static_cast<int>(emb->mWidth), &w, &hh, &n, 4);
        if (!px) {
            LOG_WARNING("Failed to decode embedded texture '%s' from '%s': %s",
                ref.c_str(), modelPath.c_str(), stbi_failure_reason());
            return {};
        }
        nlohmann::json source = {
            {"kind", "model-image"}, {"path", modelPath},
            {"ref",  ref},           {"sRGB", srgb},
        };
        TextureHandle h = addTexture(res, name, w, hh, px, srgb, std::move(source));
        stbi_image_free(px);
        return h;
    }
    // Raw aiTexel BGRA grid.
    const int w  = static_cast<int>(emb->mWidth);
    const int hh = static_cast<int>(emb->mHeight);
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * hh * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * hh; ++i) {
        rgba[i * 4 + 0] = emb->pcData[i].r;
        rgba[i * 4 + 1] = emb->pcData[i].g;
        rgba[i * 4 + 2] = emb->pcData[i].b;
        rgba[i * 4 + 3] = emb->pcData[i].a;
    }
    nlohmann::json source = {
        {"kind", "model-image"}, {"path", modelPath},
        {"ref",  ref},           {"sRGB", srgb},
    };
    return addTexture(res, name, w, hh, rgba.data(), srgb, std::move(source));
}

// Resolve one material texture slot, embedded or external file, to a handle.
TextureHandle textureFor(
    const aiScene* scene,
    const aiMaterial* mat,
    aiTextureType type,
    bool srgb,
    const std::string& modelPath,
    ResourceManager& res,
    std::unordered_map<std::string, TextureHandle>& cache
) {
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
        TextureHandle h = decodeEmbedded(emb, res, nm, modelPath, ref.C_Str(), srgb);
        cache[key] = h;
        return h;
    }

    // External file, relative to the model directory - which is itself resolved
    // from the model's reference, so a project-relative model finds its maps.
    std::filesystem::path p(ref.C_Str());
    if (p.is_relative())
        p = ProjectPaths::resolveProjectPath(modelPath).parent_path() / p;
    const std::string abs = p.lexically_normal().string();
    int w = 0, hh = 0, n = 0;
    unsigned char* px = stbi_load(abs.c_str(), &w, &hh, &n, 4);
    if (!px) {
        LOG_WARNING("Model texture not found: %s", abs.c_str());
        cache[key] = {};
        return {};
    }
    nlohmann::json source = {
        {"kind", "file"}, {"path", ProjectPaths::toProjectRelative(abs)}, {"sRGB", srgb},
        {"generateMipmaps", true},
    };
    TextureHandle h = addTexture(res, stem + ":file:" + key, w, hh, px, srgb,
        std::move(source));
    stbi_image_free(px);
    cache[key] = h;
    return h;
}

MaterialHandle buildMaterial(
    const aiScene* scene,
    const std::string& path,
    int matIdx,
    ResourceManager& res
) {
    const std::string nm = materialName(path, matIdx);
    if (MaterialHandle e = res.findByName<MaterialAsset>(nm)) return e;

    MaterialAsset out;
    out.name           = nm;
    out.sourceJson()   = { {"kind", "model"}, {"path", path}, {"material", matIdx} };

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

        // Opacity folds into albedo.a (the asset's single opacity channel).
        float opacity = 1.0f;
        if (mt->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
            out.albedo.a = glm::min(out.albedo.a, opacity);

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
            out.type = (out.albedo.a < 0.999f || out.transmission > 0.001f)
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
        // KHR_materials_emissive_strength - the HDR multiplier rides its
        // own asset field instead of being folded into the emission color.
        float emInt = 1.0f;
        mt->Get(AI_MATKEY_EMISSIVE_INTENSITY, emInt);
        out.emissiveStrength = emInt;

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
        out.clearcoatTexture    = pick({aiTextureType_CLEARCOAT},    false);
        out.transmissionTexture = pick({aiTextureType_TRANSMISSION}, false);
        out.heightTexture       = pick({aiTextureType_DISPLACEMENT}, false);
    }
    return res.add(std::move(out));
}

/**
 * @brief Import @p absolute and build one of its meshes, named by @p ref.
 *
 * The two are separate arguments because they answer different questions: only
 * Assimp needs the path on this machine, while the name and the recipe record
 * the reference, which has to mean the same thing on another one. Callable from
 * a worker, unlike the resolution itself, which reads main-thread state.
 */
MeshAsset buildMeshFrom(const std::string& absolute, const std::string& ref, int meshIndex) {
    auto importer = importerCache().get(absolute);
    if (!importer) return {};  // get() already logged the Assimp error
    const aiScene* scene = importer->GetScene();
    if (!scene) return {};
    return buildMesh(scene, ref, meshIndex);
}

} // namespace

MeshAsset loadModelMesh(const std::string& path, int meshIndex) {
    return buildMeshFrom(ProjectPaths::resolveProjectPath(path).string(),
                         ProjectPaths::toProjectRelative(path), meshIndex);
}

MeshHandle requestModelMeshAsync(
    const std::string& path,
    int meshIndex,
    ResourceManager& resources
) {
    // Resolved here rather than in the worker: a reference resolves against the
    // project root, which the editor can re-point between projects.
    const std::string ref      = ProjectPaths::toProjectRelative(path);
    const std::string absolute = ProjectPaths::resolveProjectPath(ref).string();
    const std::string name     = meshName(ref, meshIndex);

    // (path, meshIndex) is the stable identity. Idempotent: a second
    // request with the same identity returns the already-registered
    // handle, even if its decode is still in flight.
    if (auto existing = resources.findByName<MeshAsset>(name)) return existing;

    // Stub: bounds left zero so VisibilitySystem keeps it culled until
    // the worker fills in real vertex data.
    MeshAsset stub;
    stub.name    = name;
    stub.loading = true;
    stub.sourceJson() = { {"kind", "model"}, {"path", ref}, {"mesh", meshIndex} };
    const MeshHandle handle = resources.add(std::move(stub));
    const uint64_t   uid    = resources.get(handle).uid;

    ThreadPool::get().addTask([handle, uid, absolute, ref, meshIndex]() {
        // ImporterCache is mutex-guarded, so concurrent callers from
        // different workers are safe.
        MeshAsset decoded = buildMeshFrom(absolute, ref, meshIndex);

        MeshLoadCompletion completion;
        completion.handle     = handle;
        completion.assetUid   = uid;
        completion.vertices   = std::move(decoded.vertices);
        completion.indices    = std::move(decoded.indices);
        completion.skin       = std::move(decoded.skin);
        completion.skeleton   = std::move(decoded.skeleton);
        completion.boundsMin  = decoded.boundsMin;
        completion.boundsMax  = decoded.boundsMax;
        completion.skinRadius = decoded.skinRadius;
        completion.success    = !completion.vertices.empty();
        AsyncLoadQueue::get().pushMesh(std::move(completion));
    });

    return handle;
}

SkeletonHandle loadModelSkeleton(const std::string& path, ResourceManager& resources) {
    const std::string ref = ProjectPaths::toProjectRelative(path);
    if (auto existing = resources.findByName<SkeletonAsset>(skeletonName(ref))) return existing;

    auto importer = importerCache().get(ProjectPaths::resolveProjectPath(ref).string());
    if (!importer) return {};
    const aiScene* scene = importer->GetScene();
    if (!scene) return {};

    SkeletonAsset skeleton = buildSkeleton(scene, ref);
    if (skeleton.bones.empty()) return {};
    return resources.add(std::move(skeleton));
}

AnimationClipHandle loadModelAnimationClip(
    const std::string& path,
    int clipIndex,
    ResourceManager& resources
) {
    const std::string ref = ProjectPaths::toProjectRelative(path);
    if (auto existing = resources.findByName<AnimationClipAsset>(clipName(ref, clipIndex))) return existing;

    auto importer = importerCache().get(ProjectPaths::resolveProjectPath(ref).string());
    if (!importer) return {};
    const aiScene* scene = importer->GetScene();
    if (!scene) return {};

    // The clip's bone indices are only meaningful against the rig they were
    // resolved with, so it is built here rather than looked up: a rig the
    // manager happens to hold under the same name could have come from
    // anywhere.
    const SkeletonAsset skeleton = buildSkeleton(scene, ref);
    if (skeleton.bones.empty()) {
        LOG_ERROR("Clip '%s': the file has no rig to bind it to", clipName(ref, clipIndex).c_str());
        return {};
    }

    AnimationClipAsset clip = buildClip(scene, ref, clipIndex, skeleton);
    if (clip.bones.empty()) return {};
    return resources.add(std::move(clip));
}

MaterialHandle loadModelMaterial(
    const std::string& path,
    int materialIndex,
    ResourceManager& resources
) {
    auto importer = importerCache().get(ProjectPaths::resolveProjectPath(path).string());
    if (!importer) return {};
    const aiScene* scene = importer->GetScene();
    if (!scene) return {};
    return buildMaterial(scene, ProjectPaths::toProjectRelative(path), materialIndex, resources);
}

TextureHandle loadModelEmbeddedTexture(
    const std::string& path,
    const std::string& ref,
    bool srgb,
    ResourceManager& resources
) {
    const std::string modelRef = ProjectPaths::toProjectRelative(path);
    auto importer = importerCache().get(ProjectPaths::resolveProjectPath(path).string());
    if (!importer) return {};
    const aiScene* scene = importer->GetScene();
    if (!scene) return {};
    const aiTexture* emb = scene->GetEmbeddedTexture(ref.c_str());
    if (!emb) {
        LOG_WARNING("Model-image: embedded texture '%s' not found in '%s'",
            ref.c_str(), modelRef.c_str());
        return {};
    }
    const std::string key = ref + (srgb ? "#s" : "#l");
    const std::string name = stemOf(modelRef) + ":emb:" + key;
    stbi_set_flip_vertically_on_load(true);
    return decodeEmbedded(emb, resources, name, modelRef, ref, srgb);
}

EntityId importModelIntoScene(
    const std::string& path,
    ResourceManager& resources,
    Scene& scene
) {
    PROFILE_SCOPE("ModelImport");
    const std::string ref = ProjectPaths::toProjectRelative(path);

    Assimp::Importer importer;
    const aiScene* aScene = importer.ReadFile(
        ProjectPaths::resolveProjectPath(ref).string(), POST_PROCESS_FLAGS);
    if (!aScene || aScene->mNumMeshes == 0) {
        LOG_ERROR("Model import failed '%s': %s", ref.c_str(),
            importer.GetErrorString());
        return {};
    }

    // Cache assets once (idempotent by name), keyed by Assimp global index.
    std::unordered_map<int, MeshHandle>     meshes;
    std::unordered_map<int, MaterialHandle> materials;
    auto meshFor = [&](int idx) -> MeshHandle {
        auto it = meshes.find(idx);
        if (it != meshes.end()) return it->second;
        const std::string nm = meshName(ref, idx);
        MeshHandle h = resources.findByName<MeshAsset>(nm);
        if (!h) {
            MeshAsset ma = buildMesh(aScene, ref, idx);
            if (!ma.vertices.empty()) h = resources.add(std::move(ma));
        }
        meshes[idx] = h;
        return h;
    };
    auto materialFor = [&](int idx) -> MaterialHandle {
        auto it = materials.find(idx);
        if (it != materials.end()) return it->second;
        MaterialHandle h = buildMaterial(aScene, ref, idx, resources);
        materials[idx] = h;
        return h;
    };

    // The rig and its clips belong to the file rather than to any one node, so
    // they are built once, here.
    const SkeletonAsset rig = buildSkeleton(aScene, ref);
    SkeletonHandle      rigHandle;
    AnimationClipHandle firstClip;
    if (!rig.bones.empty()) {
        rigHandle = resources.findByName<SkeletonAsset>(rig.name);
        if (!rigHandle) {
            SkeletonAsset copy = rig;
            rigHandle = resources.add(std::move(copy));
        }
        for (unsigned i = 0; i < aScene->mNumAnimations; ++i) {
            const int clipIdx = static_cast<int>(i);
            AnimationClipHandle handle = resources.findByName<AnimationClipAsset>(clipName(ref, clipIdx));
            if (!handle) {
                AnimationClipAsset clip = buildClip(aScene, ref, clipIdx, rig);
                if (!clip.bones.empty()) handle = resources.add(std::move(clip));
            }
            if (handle && !firstClip) firstClip = handle;
        }
    }

    EntityId root = scene.createEntity();
    scene.add(root, Transform{});
    scene.add(root, makeName(stemOf(ref).c_str()));

    // A bone is an index in the skeleton asset, not an entity, so a node that is
    // only a bone has nothing to be. Pruned as whole subtrees rather than node by
    // node: a prop parented to a hand keeps the chain of bones that places it,
    // and nothing is ever re-parented to an ancestor it did not sit under.
    std::unordered_set<std::string> boneNames;
    for (const Bone& bone : rig.bones) boneNames.insert(bone.name);

    std::unordered_map<const aiNode*, bool> boneOnlyCache;
    std::function<bool(const aiNode*)> boneOnly = [&](const aiNode* node) -> bool {
        const auto cached = boneOnlyCache.find(node);
        if (cached != boneOnlyCache.end()) return cached->second;

        bool answer = node->mNumMeshes == 0
                   && boneNames.count(node->mName.C_Str()) != 0;
        for (unsigned c = 0; answer && c < node->mNumChildren; ++c)
            answer = boneOnly(node->mChildren[c]);

        boneOnlyCache[node] = answer;
        return answer;
    };

    std::unordered_map<const aiNode*, EntityId> nodeEntity;
    std::vector<EntityId> skinnedMeshes;

    std::function<void(const aiNode*, EntityId)> spawn =
        [&](const aiNode* node, EntityId parent) {
        if (boneOnly(node)) return;

        EntityId e = scene.createEntity();
        scene.add(e, transformOf(node->mTransformation));
        scene.add(e, makeName(node->mName.length ? node->mName.C_Str() : "node"));
        HierarchyOperations::setParent(scene, e, parent);
        nodeEntity[node] = e;

        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            const int mi = static_cast<int>(node->mMeshes[i]);
            MeshHandle mh = meshFor(mi);
            if (!mh) continue;
            MaterialHandle mat = materialFor(
                static_cast<int>(aScene->mMeshes[mi]->mMaterialIndex));
            // A skinned mesh always gets an entity of its own, because it is
            // about to be moved onto the rig and its node may carry children
            // that must not move with it.
            const bool skinned = aScene->mMeshes[mi]->HasBones();
            if (node->mNumMeshes == 1 && !skinned) {
                scene.add(e, Mesh{mh, mat});
            } else {
                EntityId sub = scene.createEntity();
                scene.add(sub, Transform{});
                scene.add(sub, makeName(("mesh" + std::to_string(mi)).c_str()));
                scene.add(sub, Mesh{mh, mat});
                HierarchyOperations::setParent(scene, sub, e);
                if (skinned) skinnedMeshes.push_back(sub);
            }
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c)
            spawn(node->mChildren[c], e);
    };

    if (aScene->mRootNode)
        spawn(aScene->mRootNode, root);

    // The rig's frame is the PARENT of its root bone, because buildSkeleton
    // composes bone 0 from its own local transform down - so a bone's model
    // matrix is expressed in the space its root sits in. Putting the Animator
    // anywhere else would offset the whole pose by one node transform, which
    // looks plausible until the character is compared with its own mesh.
    if (rigHandle) {
        const aiNode* rootBone = aScene->mRootNode->FindNode(rig.bones[0].name.c_str());
        const aiNode* rigFrame = rootBone ? rootBone->mParent : nullptr;
        const auto it = rigFrame ? nodeEntity.find(rigFrame) : nodeEntity.end();
        // No parent means the rig is rooted at the scene node itself, whose own
        // transform bone 0 already carries: the import root is that frame.
        const EntityId rigEntity = (it != nodeEntity.end()) ? it->second : root;
        Animator animator;
        animator.skeleton = rigHandle;
        animator.clip     = firstClip;
        scene.add(rigEntity, animator);

        // Skinned vertices resolve into the rig's own space - the inverse-bind
        // matrices already carry whatever placed the mesh there - so the matrix
        // multiplying them must be the rig's world matrix and nothing else.
        // Parenting each skinned mesh to the rig at identity makes that true by
        // construction instead of by convention; a mesh left under its own node
        // would be transformed twice, which looks plausible for exactly one pose.
        for (EntityId skinned : skinnedMeshes) {
            HierarchyOperations::setParent(scene, skinned, rigEntity);
            scene.get<Transform>(skinned) = Transform{};
        }
    }

    LOG_INFO("Imported model '%s' (%u meshes, %u materials, %zu bones, %u clips)",
        ref.c_str(), aScene->mNumMeshes, aScene->mNumMaterials,
        rig.bones.size(), rig.bones.empty() ? 0u : aScene->mNumAnimations);
    return root;
}

} // namespace Vkm::Engine
