#define VKM_LOG_CATEGORY "LOADER"

#include "import/model_loaders.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "stb_image.h"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include "logger.h"
#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"
#include "io/asset/asset_cook.h"
#include "io/project_paths.h"
#include "loader/image_loaders.h"
#include "import/model_part_names.h"
#include "import/model_source.h"
#include "import/texture_loaders.h"
#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/mesh.h"
#include "ecs/hierarchy_operations.h"
#include "system/async/async_load_queue.h"
#include "resource/asset_source_kind.h"

namespace Vkm::Engine {

namespace {

using ModelPartNames::clipName;
using ModelPartNames::embeddedName;
using ModelPartNames::materialName;
using ModelPartNames::meshName;
using ModelPartNames::skeletonName;

// The root entity's label: the stem reads better than the path.
std::string stemOf(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

// One writer each: the finished asset and its async stub must carry the same descriptor, or the
// cook hashes two for one asset.
nlohmann::json modelMeshRecipe(const std::string& path, int meshIdx) {
    return {{"kind", AssetSourceKind::MODEL}, {AssetSourceKey::PATH, path}, {AssetSourceKey::MESH, meshIdx}};
}
nlohmann::json modelImageRecipe(const std::string& modelPath, const std::string& ref, TextureUsage usage) {
    return {
        {"kind", AssetSourceKind::MODEL_IMAGE},
        {AssetSourceKey::PATH, modelPath},
        {AssetSourceKey::REF, ref},
        {AssetSourceKey::USAGE, Reflect::enumName(usage)}
    };
}

// One embedded image imported for two usages is two assets: the pixels differ.
const char* usageSuffix(TextureUsage usage) {
    switch (usage) {
        case TextureUsage::Color:  return "#s";
        case TextureUsage::Normal: return "#n";
        case TextureUsage::Data:
        case TextureUsage::Count:  break;
    }
    return "#l";
}

// The parsed file a project reference names, from the main thread.
std::shared_ptr<const SourceModel> modelFor(const std::string& ref) {
    return loadSourceModel(ProjectPaths::resolveProjectPath(ref).string(), ref);
}

/**
 * @brief A rig, and the node its first bone is: the Animator goes on that node's parent.
 */
struct Rig {
    SkeletonAsset skeleton;
    int32_t       rootNode = -1;
};

/**
 * @brief Build the one rig a model file's skinned meshes are bound to.
 *
 * Emitted depth-first, so `parent < index` holds (the invariant findSkeletonFault states).
 *
 * @param model Parsed file.
 * @param path Project-relative model reference, recorded in the recipe. Add the
 *        result under skeletonName(path), which a clip binds to.
 * @return The rig, or an empty one when the file has no bones or its rig is not
 *         one connected tree.
 */
Rig buildRig(const SourceModel& model, const std::string& path) {
    Rig out;

    // Joint node -> inverse bind; a shared joint has the same in both meshes, so the first stands.
    std::unordered_map<uint32_t, glm::mat4> offsets;
    for (const SourceMesh& mesh : model.meshes) {
        for (const SourceBone& bone : mesh.bones) offsets.emplace(bone.node, bone.inverseBind);
    }
    if (offsets.empty()) return out;

    std::vector<std::vector<uint32_t>> chains;
    chains.reserve(offsets.size());
    for (const auto& entry : offsets) {
        std::vector<uint32_t> chain;
        for (int32_t walk = static_cast<int32_t>(entry.first); walk >= 0;
             walk = model.nodes[static_cast<size_t>(walk)].parent) {
            chain.push_back(static_cast<uint32_t>(walk));
        }
        std::reverse(chain.begin(), chain.end());
        chains.push_back(std::move(chain));
    }

    // The rig root is the deepest node every chain shares; every chain starts at the file's root,
    // so the prefix is never empty.
    size_t common = chains.front().size();
    for (const std::vector<uint32_t>& chain : chains) {
        size_t i = 0;
        while (i < common && i < chain.size() && chain[i] == chains.front()[i]) ++i;
        common = i;
    }
    const uint32_t rigRoot = chains.front()[common - 1];

    std::unordered_set<uint32_t> needed;
    for (const std::vector<uint32_t>& chain : chains) {
        for (size_t i = common - 1; i < chain.size(); ++i) needed.insert(chain[i]);
    }

    // Two rigs would merge under a root neither has: refused when bones lie down two branches.
    if (offsets.find(rigRoot) == offsets.end()) {
        size_t branches = 0;
        for (const uint32_t child : model.nodes[rigRoot].children) {
            if (needed.count(child)) ++branches;
        }
        if (branches > 1) {
            LOG_ERROR(
                "Model '%s': its bones form %zu separate rigs under '%s'; a file has to hold one rig",
                path.c_str(),
                branches,
                model.nodes[rigRoot].name.c_str()
            );
            return {};
        }
    }

    // Depth-first, skipping what no bone needs; built from whole chains, so nothing skipped has a
    // bone under it.
    SkeletonAsset& skeleton = out.skeleton;
    std::function<void(uint32_t, int32_t, const glm::mat4&)> emit =
        [&](uint32_t node, int32_t parent, const glm::mat4& parentGlobal) {
        const SourceNode& source = model.nodes[node];
        const glm::mat4 global = parentGlobal * Transform::computeModelMatrix(source.local);
        const auto index = static_cast<int32_t>(skeleton.bones.size());
        skeleton.bones.push_back({source.name, parent});
        skeleton.bindPose.push_back(source.local);
        const auto offset = offsets.find(node);
        // A connecting node no vertex uses: nothing reads its inverse bind, so the inverse of its
        // own bind transform stands in.
        skeleton.inverseBind.push_back(offset != offsets.end() ? offset->second : glm::inverse(global));
        for (const uint32_t child : source.children) {
            if (needed.count(child)) emit(child, index, global);
        }
    };
    emit(rigRoot, -1, glm::mat4(1.0f));

    if (skeleton.bones.size() > AssetCook::MAX_SKELETON_BONES) {
        LOG_ERROR(
            "Model '%s': its rig has %zu bones, past the %u the cooked format admits",
            path.c_str(),
            skeleton.bones.size(),
            AssetCook::MAX_SKELETON_BONES
        );
        return {};
    }
    skeleton.sourceJson() = {{"kind", AssetSourceKind::MODEL}, {AssetSourceKey::PATH, path}};
    out.rootNode = static_cast<int32_t>(rigRoot);
    return out;
}

// Markers the clip can announce, in order; nameless or out-of-timeline ones are dropped.
std::vector<ClipMarker> usableMarkers(
    std::vector<ClipMarker> markers,
    float duration,
    const std::string& clip
) {
    const auto unusable = [&](const ClipMarker& marker) {
        return marker.name.empty() || !std::isfinite(marker.time)
            || marker.time < 0.0f || marker.time > duration;
    };
    const auto keep = std::remove_if(markers.begin(), markers.end(), unusable);
    const auto dropped = static_cast<size_t>(markers.end() - keep);
    if (dropped > 0) {
        LOG_WARNING(
            "Clip '%s': dropped %zu marker(s) with no name or a time outside its %.3f seconds",
            clip.c_str(),
            dropped,
            static_cast<double>(duration)
        );
        markers.erase(keep, markers.end());
    }
    std::stable_sort(
        markers.begin(),
        markers.end(),
        [](const ClipMarker& a, const ClipMarker& b) { return a.time < b.time; }
    );
    return markers;
}

/**
 * @brief Build one of @p model's clips bound to @p skeleton.
 *
 * Bound by name: on a stamp mismatch SkeletalAnimationSystem holds the bind pose.
 *
 * @param model Parsed file.
 * @param path Project-relative model reference, recorded in the recipe. Add the
 *        result under clipName(path, clipIdx).
 * @param clipIdx Index of the clip in the file.
 * @param skeleton Rig the channels are resolved against.
 * @param rigName The rig's registered name, stamped onto the clip; recorded in the recipe only
 *        when not this file's own.
 * @param markers Authored markers to carry on the clip; may be empty.
 * @return The clip, or an empty one when the index names nothing or no channel
 *         names a bone of @p skeleton.
 */
AnimationClipAsset buildClip(
    const SourceModel& model,
    const std::string& path,
    int clipIdx,
    const SkeletonAsset& skeleton,
    const std::string& rigName,
    std::vector<ClipMarker> markers
) {
    AnimationClipAsset out;
    if (clipIdx < 0 || clipIdx >= static_cast<int>(model.clips.size())) return out;
    const SourceClip& source = model.clips[static_cast<size_t>(clipIdx)];
    if (skeleton.bones.empty()) return out;

    out.skeleton = rigName;
    out.duration = std::max(0.0f, source.duration);
    out.bones.resize(skeleton.bones.size());

    unsigned dropped = 0;
    for (const SourceChannel& channel : source.channels) {
        const int32_t bone = skeleton.indexOf(model.nodes[channel.node].name);
        // A node outside the rig (camera, prop) has nowhere to land.
        if (bone < 0) {
            ++dropped;
            continue;
        }
        ClipBone& target = out.bones[static_cast<size_t>(bone)];

        target.position = {
            static_cast<uint32_t>(out.positions.size()),
            static_cast<uint32_t>(channel.positions.size())
        };
        out.positionTimes.insert(
            out.positionTimes.end(),
            channel.positionTimes.begin(),
            channel.positionTimes.end()
        );
        out.positions.insert(out.positions.end(), channel.positions.begin(), channel.positions.end());
        target.rotation = {
            static_cast<uint32_t>(out.rotations.size()),
            static_cast<uint32_t>(channel.rotations.size())
        };
        out.rotationTimes.insert(
            out.rotationTimes.end(),
            channel.rotationTimes.begin(),
            channel.rotationTimes.end()
        );
        out.rotations.insert(out.rotations.end(), channel.rotations.begin(), channel.rotations.end());
        target.scale = {
            static_cast<uint32_t>(out.scales.size()),
            static_cast<uint32_t>(channel.scales.size())
        };
        out.scaleTimes.insert(out.scaleTimes.end(), channel.scaleTimes.begin(), channel.scaleTimes.end());
        out.scales.insert(out.scales.end(), channel.scales.begin(), channel.scales.end());
    }
    // No channel found a bone: the clip animates some other rig.
    if (out.positions.empty() && out.rotations.empty() && out.scales.empty()) {
        LOG_ERROR(
            "Clip '%s': none of its %zu channels name a bone of rig '%s'; it animates a different rig",
            clipName(path, clipIdx).c_str(),
            source.channels.size(),
            rigName.c_str()
        );
        return {};
    }

    if (dropped) {
        LOG_WARNING(
            "Clip '%s': dropped %u channel(s) naming nodes outside the rig",
            clipName(path, clipIdx).c_str(),
            dropped
        );
    }

    out.markers = usableMarkers(std::move(markers), out.duration, clipName(path, clipIdx));

    // The recipe is regenerated from this on every cook, so accepted markers go back into it.
    out.sourceJson() = {
        {"kind", AssetSourceKind::MODEL},
        {AssetSourceKey::PATH, path},
        {AssetSourceKey::CLIP, clipIdx}
    };
    // Only when it is not this file's own rig: absent means the file's own.
    if (rigName != skeletonName(path)) out.sourceJson()[AssetSourceKey::RIG] = rigName;
    if (!out.markers.empty()) {
        nlohmann::json markerJson = nlohmann::json::array();
        for (const ClipMarker& marker : out.markers) {
            markerJson.push_back({{"name", marker.name}, {"time", marker.time}});
        }
        out.sourceJson()[AssetSourceKey::MARKERS] = std::move(markerJson);
    }
    return out;
}

/**
 * @brief Build one of @p model's meshes, its skin addressed against the file's rig.
 *
 * The rig's name is stamped on, as the indices mean nothing against another rig;
 * SkeletalAnimationSystem warns on a mismatch.
 *
 * @param model Parsed file.
 * @param path Project-relative model reference, recorded in the recipe.
 * @param meshIdx Index of the mesh in the file.
 * @return The mesh, or an empty one when the index names nothing.
 */
MeshAsset buildMesh(const SourceModel& model, const std::string& path, int meshIdx) {
    MeshAsset out;
    if (meshIdx < 0 || meshIdx >= static_cast<int>(model.meshes.size())) return out;
    const SourceMesh& source = model.meshes[static_cast<size_t>(meshIdx)];
    if (source.geometry.vertices.empty()) return out;

    out.vertices = source.geometry.vertices;
    out.indices  = source.geometry.indices;

    // Rebuilt rather than threaded in: a node-tree walk, at import only.
    if (!source.bones.empty() && !source.geometry.skin.empty()) {
        const Rig rig = buildRig(model, path);
        if (!rig.skeleton.bones.empty()) {
            std::vector<uint16_t> toRig;
            toRig.reserve(source.bones.size());
            for (const SourceBone& bone : source.bones) {
                // Every joint a mesh names is in the rig: the rig is their union.
                toRig.push_back(static_cast<uint16_t>(rig.skeleton.indexOf(model.nodes[bone.node].name)));
            }
            out.skin = source.geometry.skin;
            for (SkinVertex& skin : out.skin) {
                if (skin.weights[0] == 0) {
                    // All-zero weights collapse the vertex to the origin; rigid to the root keeps it.
                    skin = SkinVertex{};
                    skin.weights[0] = 255;
                    continue;
                }
                for (int k = 0; k < 4; ++k) {
                    skin.bones[k] = skin.weights[k] ? toRig[skin.bones[k]] : uint16_t{0};
                }
            }
            out.skeleton = skeletonName(path);
            out.computeAndSetSkinRadius(rig.skeleton);
        }
    }

    out.sourceJson() = modelMeshRecipe(path, meshIdx);
    out.computeAndSetBounds();
    return out;
}

// Models routinely tile UVs outside [0,1], which ClampToEdge would smear.
constexpr TextureWrapMode MODEL_TEXTURE_WRAP = TextureWrapMode::Repeat;

/**
 * @brief Decode one image a model file carries and register it under @p name.
 *
 * Idempotent by name; tiling and mipmapped. The source is stamped on, so a cold load recreates it.
 *
 * @param image     The embedded image; its bytes are decoded.
 * @param res       Graph the texture is added to.
 * @param name      Name the texture is registered and looked up under.
 * @param modelPath Project-relative model reference, recorded in the recipe.
 * @param usage     What the texels mean.
 * @return The texture, or an invalid handle when the bytes do not decode.
 */
TextureHandle decodeEmbedded(
    const SourceImage& image,
    ResourceManager& res,
    const std::string& name,
    const std::string& modelPath,
    TextureUsage usage
) {
    if (TextureHandle e = res.findByName<TextureAsset>(name)) return e;
    TextureAsset tex;
    if (!decodeTextureFromMemory(image.embedded.data(), image.embedded.size(), usage, tex)) {
        LOG_WARNING(
            "Failed to decode embedded texture '%s' from '%s': %s",
            image.ref.c_str(),
            modelPath.c_str(),
            stbi_failure_reason()
        );
        return {};
    }
    tex.params.wrapS           = MODEL_TEXTURE_WRAP;
    tex.params.wrapT           = MODEL_TEXTURE_WRAP;
    tex.params.generateMipmaps = true;
    tex.sourceJson() = modelImageRecipe(modelPath, image.ref, usage);
    return res.add(std::move(tex), name);
}

// Resolve one of the file's images, embedded or beside it, to a handle.
TextureHandle textureFor(
    const SourceImage& image,
    TextureUsage usage,
    const std::string& modelPath,
    ResourceManager& res,
    std::unordered_map<std::string, TextureHandle>& cache
) {
    // An image the file meant to carry and did not decode has nothing to load.
    if (image.ref.empty() || (image.embedded.empty() && image.ref.front() == '*')) return {};
    const std::string key = image.ref + usageSuffix(usage);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    decodeImagesBottomUp();

    if (!image.embedded.empty()) {
        const TextureHandle h = decodeEmbedded(image, res, embeddedName(modelPath, key), modelPath, usage);
        cache[key] = h;
        return h;
    }

    // External file, relative to the resolved model directory.
    std::filesystem::path p(image.ref);
    if (p.is_relative())
        p = ProjectPaths::resolveProjectPath(modelPath).parent_path() / p;
    const std::string abs = p.lexically_normal().string();

    // Named by the reference, so one image two models reference is one asset.
    const std::string fileRef = ProjectPaths::toProjectRelative(abs);
    if (TextureHandle existing = res.findByName<TextureAsset>(fileRef)) {
        cache[key] = existing;
        return existing;
    }

    // Through the file-texture loader its recipe reloads by, so import and later loads decode alike.
    const bool generateMipmaps = true;
    const TextureHandle h = loadTexture(
        fileRef,
        res,
        usage,
        generateMipmaps,
        TextureFilterOverride::None,
        MODEL_TEXTURE_WRAP
    );
    cache[key] = h;
    return h;
}

MaterialHandle buildMaterial(
    const SourceModel& model,
    const std::string& path,
    int matIdx,
    ResourceManager& res
) {
    const std::string nm = materialName(path, matIdx);
    if (MaterialHandle e = res.findByName<MaterialAsset>(nm)) return e;
    if (matIdx < 0 || matIdx >= static_cast<int>(model.materials.size())) return {};

    const SourceMaterial& source = model.materials[static_cast<size_t>(matIdx)];
    MaterialAsset out = source.values;
    std::unordered_map<std::string, TextureHandle> cache;
    for (const SourceMap& map : source.maps) {
        out.*map.slot = textureFor(model.images[map.image], map.usage, path, res, cache);
    }
    // A metallic-roughness map names its normal map, so the cook folds the bumps its mips lose
    // into roughness. Only a normal map in its own file pairs, as the cook decodes it by name.
    // The first material to pair a map decides: shared under different normal maps, it keeps
    // that first pairing.
    if (out.metallicRoughnessTexture && out.normalTexture) {
        const TextureAsset& normal = res.get(out.normalTexture);
        nlohmann::json& recipe     = res.edit(out.metallicRoughnessTexture).sourceJson();
        if (normal.sourceJson().value("kind", std::string{}) == AssetSourceKind::FILE
            && !recipe.contains(AssetSourceKey::ROUGHNESS_NORMAL)) {
            recipe[AssetSourceKey::ROUGHNESS_NORMAL] = normal.name();
        }
    }
    return res.add(std::move(out), nm);
}

} // namespace

MeshHandle requestModelMeshAsync(const std::string& path, int meshIndex, ResourceManager& resources) {
    // Resolved here, not in the worker: ProjectPaths::setProjectRoot can re-point the root.
    const std::string ref      = ProjectPaths::toProjectRelative(path);
    const std::string absolute = ProjectPaths::resolveProjectPath(ref).string();
    const std::string name     = meshName(ref, meshIndex);

    if (auto existing = resources.findByName<MeshAsset>(name)) return existing;

    MeshAsset stub;
    stub.loading = true;
    stub.sourceJson() = modelMeshRecipe(ref, meshIndex);
    const MeshHandle handle = resources.add(std::move(stub), name);
    const uint64_t   uid    = resources.get(handle).uid();

    ThreadPool::get().addTask([handle, uid, absolute, ref, meshIndex]() {
        // Workers asking for one file's meshes share one parse.
        const std::shared_ptr<const SourceModel> model = loadSourceModel(absolute, ref);
        MeshAsset decoded = model ? buildMesh(*model, ref, meshIndex) : MeshAsset{};
        AsyncLoadQueue::get().pushMesh({handle, uid, std::move(decoded)});
    });

    return handle;
}

SkeletonHandle loadModelSkeleton(const std::string& path, ResourceManager& resources) {
    const std::string ref = ProjectPaths::toProjectRelative(path);
    if (auto existing = resources.findByName<SkeletonAsset>(skeletonName(ref))) return existing;

    const std::shared_ptr<const SourceModel> model = modelFor(ref);
    if (!model) return {};
    Rig rig = buildRig(*model, ref);
    if (rig.skeleton.bones.empty()) return {};
    return resources.add(std::move(rig.skeleton), skeletonName(ref));
}

AnimationClipHandle loadModelAnimationClip(
    const std::string& path,
    int clipIndex,
    std::vector<ClipMarker> markers,
    ResourceManager& resources,
    const std::string& rig
) {
    const std::string ref = ProjectPaths::toProjectRelative(path);
    if (auto existing = resources.findByName<AnimationClipAsset>(clipName(ref, clipIndex))) return existing;

    const std::shared_ptr<const SourceModel> model = modelFor(ref);
    if (!model) return {};

    // Bind to the manager's copy, not a rebuild: bone indices mean something only against the
    // rig they were resolved with.
    if (!rig.empty()) {
        const SkeletonHandle handle = resources.findByName<SkeletonAsset>(rig);
        if (!handle) {
            LOG_ERROR(
                "Clip '%s': names rig '%s', which is not loaded",
                clipName(ref, clipIndex).c_str(),
                rig.c_str()
            );
            return {};
        }
        const SkeletonAsset& target = resources.get(handle);
        AnimationClipAsset bound =
            buildClip(*model, ref, clipIndex, target, rig, std::move(markers));
        if (bound.bones.empty()) return {};
        return resources.add(std::move(bound), clipName(ref, clipIndex));
    }

    const Rig own = buildRig(*model, ref);
    if (own.skeleton.bones.empty()) {
        LOG_ERROR(
            "Clip '%s': the file has no rig of its own, and none was named. An animation exported "
            "without skin has to say which rig it animates.",
            clipName(ref, clipIndex).c_str()
        );
        return {};
    }

    AnimationClipAsset clip =
        buildClip(*model, ref, clipIndex, own.skeleton, skeletonName(ref), std::move(markers));
    if (clip.bones.empty()) return {};
    return resources.add(std::move(clip), clipName(ref, clipIndex));
}

TextureHandle loadModelEmbeddedTexture(
    const std::string& path,
    const std::string& ref,
    TextureUsage usage,
    ResourceManager& resources
) {
    const std::string modelRef = ProjectPaths::toProjectRelative(path);
    const std::shared_ptr<const SourceModel> model = modelFor(modelRef);
    if (!model) return {};
    const auto image = std::find_if(
        model->images.begin(),
        model->images.end(),
        [&](const SourceImage& candidate) { return candidate.ref == ref && !candidate.embedded.empty(); }
    );
    if (image == model->images.end()) {
        LOG_WARNING("Model-image: embedded texture '%s' not found in '%s'", ref.c_str(), modelRef.c_str());
        return {};
    }
    const std::string name = embeddedName(modelRef, ref + usageSuffix(usage));
    decodeImagesBottomUp();
    return decodeEmbedded(*image, resources, name, modelRef, usage);
}

ModelImport importModelIntoScene(const std::string& path, ResourceManager& resources, Scene& scene) {
    PROFILE_SCOPE("ModelImport");
    const std::string ref = ProjectPaths::toProjectRelative(path);

    const std::shared_ptr<const SourceModel> model = modelFor(ref);
    if (!model) return {};

    // Animations only: clips bound to a rig already loaded; with several, it must be named per clip.
    if (model->meshes.empty()) {
        if (model->clips.empty()) {
            LOG_ERROR("Model import failed '%s': it holds no mesh and no animation", ref.c_str());
            return {};
        }

        std::vector<std::string> rigs;
        resources.forEachOfType<SkeletonAsset>([&](SkeletonHandle, const SkeletonAsset& asset) {
            rigs.push_back(asset.name());
        });

        if (rigs.size() != 1) {
            LOG_ERROR(
                "'%s' holds %zu animation(s) and no rig of its own. The project has %zu rig(s) "
                "loaded, so which one they animate cannot be guessed - import the character "
                "first, or name the rig on the clip.",
                ref.c_str(),
                model->clips.size(),
                rigs.size()
            );
            return {};
        }

        unsigned imported = 0;
        for (size_t i = 0; i < model->clips.size(); ++i) {
            if (loadModelAnimationClip(ref, static_cast<int>(i), {}, resources, rigs.front())) ++imported;
        }
        LOG_INFO(
            "Imported %u clip(s) from '%s', bound to rig '%s'. Nothing was added to the scene: "
            "the file has no mesh.",
            imported,
            ref.c_str(),
            rigs.front().c_str()
        );
        return {EntityId{}, imported, true};
    }

    // Keyed by index in the file.
    std::unordered_map<uint32_t, MeshHandle>     meshes;
    std::unordered_map<uint32_t, MaterialHandle> materials;
    auto meshFor = [&](uint32_t idx) -> MeshHandle {
        auto it = meshes.find(idx);
        if (it != meshes.end()) return it->second;
        const std::string nm = meshName(ref, static_cast<int>(idx));
        MeshHandle h = resources.findByName<MeshAsset>(nm);
        if (!h) {
            MeshAsset ma = buildMesh(*model, ref, static_cast<int>(idx));
            if (!ma.vertices.empty()) h = resources.add(std::move(ma), nm);
        }
        meshes[idx] = h;
        return h;
    };
    auto materialFor = [&](uint32_t idx) -> MaterialHandle {
        auto it = materials.find(idx);
        if (it != materials.end()) return it->second;
        MaterialHandle h = buildMaterial(*model, ref, static_cast<int>(idx), resources);
        materials[idx] = h;
        return h;
    };

    // The rig and clips belong to the file, not a node.
    const Rig           rig = buildRig(*model, ref);
    SkeletonHandle      rigHandle;
    AnimationClipHandle firstClip;
    // What was built: a clip for some other rig does not count.
    uint32_t            clips = 0;
    if (!rig.skeleton.bones.empty()) {
        rigHandle = resources.findByName<SkeletonAsset>(skeletonName(ref));
        if (!rigHandle) {
            SkeletonAsset copy = rig.skeleton;
            rigHandle = resources.add(std::move(copy), skeletonName(ref));
        }
        for (size_t i = 0; i < model->clips.size(); ++i) {
            const int clipIdx = static_cast<int>(i);
            AnimationClipHandle handle = resources.findByName<AnimationClipAsset>(clipName(ref, clipIdx));
            if (!handle) {
                // No interchange format has markers; they are authored into the recipe later.
                AnimationClipAsset clip =
                    buildClip(*model, ref, clipIdx, rig.skeleton, skeletonName(ref), {});
                if (!clip.bones.empty()) handle = resources.add(std::move(clip), clipName(ref, clipIdx));
            }
            if (!handle) continue;
            ++clips;
            if (!firstClip) firstClip = handle;
        }
    }

    // The import root is the file's own root node: its children are the top level.
    EntityId root = scene.createEntity();
    scene.add(root, model->nodes.front().local);
    scene.add(root, makeName(stemOf(ref).c_str()));

    // A bone-only node is no entity. Pruned as whole subtrees, so a prop on a hand keeps the bone
    // chain that places it.
    std::unordered_set<std::string> boneNames;
    for (const Bone& bone : rig.skeleton.bones) boneNames.insert(bone.name);

    std::vector<int8_t> boneOnlyCache(model->nodes.size(), -1);
    std::function<bool(uint32_t)> boneOnly = [&](uint32_t node) -> bool {
        if (boneOnlyCache[node] >= 0) return boneOnlyCache[node] != 0;
        const SourceNode& source = model->nodes[node];
        bool answer = source.meshes.empty() && boneNames.count(source.name) != 0;
        for (size_t c = 0; answer && c < source.children.size(); ++c) answer = boneOnly(source.children[c]);
        boneOnlyCache[node] = answer ? 1 : 0;
        return answer;
    };

    std::unordered_map<uint32_t, EntityId> nodeEntity;
    nodeEntity[0] = root;
    std::vector<EntityId> skinnedMeshes;

    std::function<void(uint32_t, EntityId)> spawn = [&](uint32_t node, EntityId parent) {
        if (boneOnly(node)) return;
        const SourceNode& source = model->nodes[node];

        EntityId e = scene.createEntity();
        scene.add(e, source.local);
        scene.add(e, makeName(source.name.empty() ? "node" : source.name.c_str()));
        HierarchyOperations::setParent(scene, e, parent);
        nodeEntity[node] = e;

        for (const uint32_t mi : source.meshes) {
            MeshHandle mh = meshFor(mi);
            if (!mh) continue;
            MaterialHandle mat = materialFor(model->meshes[mi].material);
            // Its own entity: it moves onto the rig, and its node's children must not move with it.
            const bool skinned = !model->meshes[mi].bones.empty();
            if (source.meshes.size() == 1 && !skinned) {
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
        for (const uint32_t child : source.children) spawn(child, e);
    };
    for (const uint32_t child : model->nodes.front().children) spawn(child, root);

    // The root bone's parent: buildRig keeps bone 0's local transform, so poses are in the space
    // the root sits in. Anywhere else offsets the pose.
    if (rigHandle) {
        const int32_t rigFrame = model->nodes[static_cast<size_t>(rig.rootNode)].parent;
        const auto it = rigFrame >= 0 ? nodeEntity.find(static_cast<uint32_t>(rigFrame)) : nodeEntity.end();
        const EntityId rigEntity = (it != nodeEntity.end()) ? it->second : root;
        Animator animator;
        animator.skeleton = rigHandle;
        animator.clip     = firstClip;
        scene.add(rigEntity, animator);

        // Skinned vertices resolve into rig space via the inverse binds; parenting at identity keeps
        // them from being transformed twice.
        for (EntityId skinned : skinnedMeshes) {
            HierarchyOperations::setParent(scene, skinned, rigEntity);
            scene.get<Transform>(skinned) = Transform{};
        }
    }

    LOG_INFO(
        "Imported model '%s' (%zu meshes, %zu materials, %zu bones, %u clips)",
        ref.c_str(),
        model->meshes.size(),
        model->materials.size(),
        rig.skeleton.bones.size(),
        clips
    );
    return {root, clips, true};
}

} // namespace Vkm::Engine
