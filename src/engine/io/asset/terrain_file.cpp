#define VKM_LOG_CATEGORY "IO"

#include "io/asset/terrain_file.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include <nlohmann/json.hpp>

#include "vkm_log/assert.h"
#include "vkm_log/logger.h"

#include "io/asset/byte_stream.h"
#include "io/asset/recipe/asset_source.h"
#include "io/project/roots.h"
#include "io/whole_file_write.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine::TerrainFile {

namespace {

// A name as the body keeps it: its length, then its bytes.
void writeName(ByteWriter& w, const std::string& value) {
    w.raw(static_cast<uint32_t>(value.size()));
    w.chars(value);
}

bool readName(ByteReader& r, std::string& value) {
    uint32_t length = 0;
    return r.raw(length) && length <= MAX_NAME && r.chars(value, length);
}

/**
 * @brief The name the file keeps for @p handle's asset.
 *
 * @param handle     The asset; none for one whose name resolved to nothing.
 * @param unresolved What the file named that resolved to nothing; returned for no handle.
 * @param resources  Holds the asset.
 * @param what       What the asset is to the terrain, for the warning.
 * @return Its name, or none for a hidden or unnamed one, which is warned of.
 */
template<typename Asset>
std::string nameOf(
    const Handle<Asset>& handle,
    const std::string& unresolved,
    const ResourceManager& resources,
    const char* what
) {
    if (!handle) return unresolved;
    const Asset* asset = resources.tryGet(handle);
    if (!asset || asset->isHidden() || asset->name().empty()) {
        LOG_WARNING("Terrain: a %s it names has no name to keep, so is dropped", what);
        return {};
    }
    return asset->name();
}

} // namespace

std::vector<uint8_t> encode(const TerrainAsset& terrain, const ResourceManager& resources) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.raw(terrain.resolution);
    w.raw(terrain.size);
    for (size_t i = 0; i < terrain.layers.size(); ++i) {
        writeName(w, nameOf(terrain.layers[i], terrain.unresolvedLayers[i], resources, "layer material"));
    }
    for (const float tiling : terrain.layerTiling) w.raw(tiling);
    w.bulk(terrain.heights);
    w.bulk(terrain.weights);
    w.raw(static_cast<uint32_t>(terrain.foliage.size()));
    for (const TerrainFoliage& layer : terrain.foliage) {
        w.raw(static_cast<uint32_t>(layer.parts.size()));
        for (const TerrainPlantPart& part : layer.parts) {
            writeName(w, nameOf(part.mesh, part.unresolvedMesh, resources, "plant mesh"));
            writeName(w, nameOf(part.material, part.unresolvedMaterial, resources, "plant material"));
            w.raw(part.local);
        }
        w.raw(layer.perSquareMeter);
        w.raw(layer.minScale);
        w.raw(layer.maxScale);
        w.raw(layer.maxDistance);
        w.raw(layer.seed);
        w.raw(static_cast<uint8_t>(layer.castShadows ? 1 : 0));
        // A layer not yet painted holds no map; the file holds one regardless, of none.
        if (layer.density.empty()) {
            w.bulk(std::vector<uint8_t>(static_cast<size_t>(terrain.resolution) * terrain.resolution, 0));
        } else {
            w.bulk(layer.density);
        }
    }
    return out;
}

bool decode(const uint8_t* bytes, size_t size, TerrainAsset& out, Names& names, std::string& error) {
    VKM_ASSERT(out.uid() == 0, "TerrainFile::decode fills a fresh asset; it takes the identity too");
    ByteReader r(bytes, size);
    TerrainAsset terrain;
    Names        read;
    if (!r.raw(terrain.resolution) || !r.raw(terrain.size)) {
        error = "cut short in its grid";
        return false;
    }
    if (!TerrainAsset::validResolution(terrain.resolution)) {
        error = "a resolution of " + std::to_string(terrain.resolution) + ", not 2^n + 1 within bounds";
        return false;
    }
    if (!std::isfinite(terrain.size) || terrain.size <= 0.0f) {
        error = "a size that is not a positive length";
        return false;
    }
    for (std::string& layer : read.layers) {
        if (!readName(r, layer)) {
            error = "cut short in its layers";
            return false;
        }
    }
    for (float& tiling : terrain.layerTiling) {
        if (!r.raw(tiling) || !std::isfinite(tiling) || tiling <= 0.0f) {
            error = "a layer's tiling that is not a positive length";
            return false;
        }
    }
    const size_t vertices = static_cast<size_t>(terrain.resolution) * terrain.resolution;
    const size_t shares   = vertices * TerrainAsset::WEIGHTS_PER_VERTEX;
    if (!r.bulk(terrain.heights, vertices) || !r.bulk(terrain.weights, shares)) {
        error = "cut short in its heights or weights";
        return false;
    }
    for (const float height : terrain.heights) {
        if (!std::isfinite(height)) {
            error = "a height that is not a number";
            return false;
        }
    }
    uint32_t foliage = 0;
    if (!r.raw(foliage) || foliage > TerrainAsset::MAX_FOLIAGE) {
        error = "more foliage layers than a terrain holds, or none said";
        return false;
    }
    terrain.foliage.resize(foliage);
    read.plants.resize(foliage);
    for (uint32_t f = 0; f < foliage; ++f) {
        TerrainFoliage& layer = terrain.foliage[f];
        uint32_t parts = 0;
        if (!r.raw(parts) || parts > TerrainAsset::MAX_PLANT_PARTS) {
            error = "a plant of more parts than one holds";
            return false;
        }
        layer.parts.resize(parts);
        read.plants[f].resize(parts);
        for (uint32_t p = 0; p < parts; ++p) {
            if (!readName(r, read.plants[f][p].first) || !readName(r, read.plants[f][p].second)
                || !r.raw(layer.parts[p].local)) {
                error = "cut short in a plant";
                return false;
            }
            const float* local = &layer.parts[p].local[0][0];
            if (!std::all_of(local, local + 16, [](float v) { return std::isfinite(v); })) {
                error = "a plant part placed by a matrix that is not numbers";
                return false;
            }
        }
        uint8_t casts = 0;
        if (!r.raw(layer.perSquareMeter) || !r.raw(layer.minScale) || !r.raw(layer.maxScale)
            || !r.raw(layer.maxDistance) || !r.raw(layer.seed) || !r.raw(casts)
            || !r.bulk(layer.density, vertices)) {
            error = "cut short in a foliage layer";
            return false;
        }
        layer.castShadows = casts != 0;
        const bool scales = std::isfinite(layer.minScale) && std::isfinite(layer.maxScale)
            && layer.minScale > 0.0f && layer.minScale <= layer.maxScale;
        if (!std::isfinite(layer.perSquareMeter) || layer.perSquareMeter < 0.0f || !scales
            || !std::isfinite(layer.maxDistance) || layer.maxDistance < 0.0f) {
            error = "a foliage layer whose density, sizes or draw distance are no lengths";
            return false;
        }
    }
    if (r.left() != 0) {
        error = "bytes past its end";
        return false;
    }
    out   = std::move(terrain);
    names = std::move(read);
    return true;
}

void resolve(TerrainAsset& terrain, const Names& names, const ResourceManager& resources) {
    const auto find = [&](const auto& handleOfName, const std::string& name, const char* what) {
        using Handle = std::decay_t<decltype(handleOfName(name))>;
        if (name.empty()) return Handle{};
        const Handle handle = handleOfName(name);
        if (!handle) LOG_WARNING("Terrain: no %s named '%s'; left empty", what, name.c_str());
        return handle;
    };
    const auto material = [&](const std::string& n) { return resources.findByName<MaterialAsset>(n); };
    const auto mesh     = [&](const std::string& n) { return resources.findByName<MeshAsset>(n); };
    // A name that finds nothing is kept beside the empty handle, for a save to write back.
    const auto keep = [](const auto& handle, const std::string& name) {
        return handle ? std::string() : name;
    };
    for (size_t i = 0; i < terrain.layers.size(); ++i) {
        terrain.layers[i]           = find(material, names.layers[i], "layer material");
        terrain.unresolvedLayers[i] = keep(terrain.layers[i], names.layers[i]);
    }
    for (size_t f = 0; f < terrain.foliage.size() && f < names.plants.size(); ++f) {
        std::vector<TerrainPlantPart>& parts = terrain.foliage[f].parts;
        for (size_t p = 0; p < parts.size() && p < names.plants[f].size(); ++p) {
            parts[p].mesh               = find(mesh, names.plants[f][p].first, "plant mesh");
            parts[p].material           = find(material, names.plants[f][p].second, "plant material");
            parts[p].unresolvedMesh     = keep(parts[p].mesh, names.plants[f][p].first);
            parts[p].unresolvedMaterial = keep(parts[p].material, names.plants[f][p].second);
        }
    }
}

bool write(const std::filesystem::path& path, const TerrainAsset& terrain, const ResourceManager& resources) {
    const std::vector<uint8_t> body = encode(terrain, resources);
    WholeFileWrite file(path, std::ios::binary, "Terrain");
    if (!file) return false;
    std::ofstream& out = file.stream();
    out.write(MAGIC, sizeof(MAGIC));
    out.write(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION));
    out.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    return file.commit();
}

bool readBody(const std::filesystem::path& path, std::vector<uint8_t>& body, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot be opened";
        return false;
    }
    char     magic[sizeof(MAGIC)] = {};
    uint32_t version = 0;
    in.read(magic, sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!in || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0) {
        error = "is not a terrain file";
        return false;
    }
    if (version != VERSION) {
        error = "is version " + std::to_string(version) + ", where this build reads "
            + std::to_string(VERSION);
        return false;
    }
    body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

TerrainHandle load(const std::string& name, ResourceManager& resources) {
    const std::string ref = Roots::toProjectRelative(name);
    if (const TerrainHandle resident = resources.findByName<TerrainAsset>(ref)) return resident;

    std::vector<uint8_t> body;
    std::string          error;
    TerrainAsset         terrain;
    Names                names;
    if (!readBody(Roots::resolveProjectPath(ref), body, error)
        || !decode(body.data(), body.size(), terrain, names, error)) {
        LOG_ERROR("Terrain '%s' %s", ref.c_str(), error.c_str());
        return {};
    }
    resolve(terrain, names, resources);
    terrain.sourceJson() = {{AssetSourceKey::KIND, AssetSourceKind::FILE}, {AssetSourceKey::PATH, ref}};
    return resources.add(std::move(terrain), ref);
}

} // namespace Vkm::Engine::TerrainFile
