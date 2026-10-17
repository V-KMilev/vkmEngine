#define VKM_LOG_CATEGORY "IO"

#include "io/asset/terrain_file.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>

#include "logger.h"

#include "resource/resource_manager.h"

namespace Vkm::Engine::TerrainFile {

namespace {

/// Appends values to a body.
class Writer {
    public:
        explicit Writer(std::vector<uint8_t>& out) : m_out(out) {}
        ~Writer() = default;

        Writer(const Writer& other) = delete;
        Writer& operator=(const Writer& other) = delete;

        Writer(Writer && other) = delete;
        Writer& operator=(Writer && other) = delete;

    public:
        template<typename T>
        void raw(const T& value) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            m_out.insert(m_out.end(), bytes, bytes + sizeof(T));
        }

        template<typename T>
        void bulk(const std::vector<T>& values) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
            m_out.insert(m_out.end(), bytes, bytes + values.size() * sizeof(T));
        }

        void name(const std::string& value) {
            raw(static_cast<uint32_t>(value.size()));
            m_out.insert(m_out.end(), value.begin(), value.end());
        }

    private:
        std::vector<uint8_t>& m_out;
};

/// Takes values off a body, refusing to read past its end.
class Reader {
    public:
        Reader(const uint8_t* bytes, size_t size) : m_at(bytes), m_left(size) {}
        ~Reader() = default;

        Reader(const Reader& other) = delete;
        Reader& operator=(const Reader& other) = delete;

        Reader(Reader && other) = delete;
        Reader& operator=(Reader && other) = delete;

    public:
        template<typename T>
        bool raw(T& value) {
            if (m_left < sizeof(T)) return false;
            std::memcpy(&value, m_at, sizeof(T));
            m_at += sizeof(T);
            m_left -= sizeof(T);
            return true;
        }

        template<typename T>
        bool bulk(std::vector<T>& values, size_t count) {
            if (count > m_left / sizeof(T)) return false;
            values.resize(count);
            std::memcpy(values.data(), m_at, count * sizeof(T));
            m_at += count * sizeof(T);
            m_left -= count * sizeof(T);
            return true;
        }

        bool name(std::string& value) {
            uint32_t length = 0;
            if (!raw(length) || length > MAX_NAME || length > m_left) return false;
            value.assign(reinterpret_cast<const char*>(m_at), length);
            m_at += length;
            m_left -= length;
            return true;
        }

        size_t left() const { return m_left; }

    private:
        const uint8_t* m_at;
        size_t         m_left;
};

/// @p handle's asset's name, or none for a hidden or unnamed one, which is warned of.
template<typename Asset>
std::string nameOf(const Handle<Asset>& handle, const ResourceManager& resources, const char* what) {
    if (!handle) return {};
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
    Writer w(out);
    w.raw(terrain.resolution);
    w.raw(terrain.size);
    for (const MaterialHandle& layer : terrain.layers) w.name(nameOf(layer, resources, "layer material"));
    for (const float tiling : terrain.layerTiling) w.raw(tiling);
    w.bulk(terrain.heights);
    w.bulk(terrain.weights);
    w.raw(static_cast<uint32_t>(terrain.foliage.size()));
    for (const TerrainFoliage& layer : terrain.foliage) {
        w.raw(static_cast<uint32_t>(layer.parts.size()));
        for (const TerrainPlantPart& part : layer.parts) {
            w.name(nameOf(part.mesh, resources, "plant mesh"));
            w.name(nameOf(part.material, resources, "plant material"));
            w.raw(part.local);
        }
        w.raw(layer.perSquareMetre);
        w.raw(layer.minScale);
        w.raw(layer.maxScale);
        w.raw(layer.maxDistance);
        w.raw(layer.seed);
        w.raw(static_cast<uint8_t>(layer.castShadows ? 1 : 0));
        w.bulk(layer.density);
    }
    return out;
}

bool decode(const uint8_t* bytes, size_t size, TerrainAsset& out, Names& names, std::string& error) {
    Reader r(bytes, size);
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
        if (!r.name(layer)) {
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
    if (!r.bulk(terrain.heights, vertices) || !r.bulk(terrain.weights, vertices * 4)) {
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
        uint32_t        parts = 0;
        if (!r.raw(parts) || parts > TerrainAsset::MAX_PLANT_PARTS) {
            error = "a plant of more parts than one holds";
            return false;
        }
        layer.parts.resize(parts);
        read.plants[f].resize(parts);
        for (uint32_t p = 0; p < parts; ++p) {
            if (!r.name(read.plants[f][p].first) || !r.name(read.plants[f][p].second)
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
        if (!r.raw(layer.perSquareMetre) || !r.raw(layer.minScale) || !r.raw(layer.maxScale)
            || !r.raw(layer.maxDistance) || !r.raw(layer.seed) || !r.raw(casts)
            || !r.bulk(layer.density, vertices)) {
            error = "cut short in a foliage layer";
            return false;
        }
        layer.castShadows = casts != 0;
        const bool scales = std::isfinite(layer.minScale) && std::isfinite(layer.maxScale)
            && layer.minScale > 0.0f && layer.minScale <= layer.maxScale;
        if (!std::isfinite(layer.perSquareMetre) || layer.perSquareMetre < 0.0f || !scales
            || !std::isfinite(layer.maxDistance) || layer.maxDistance < 0.0f) {
            error = "a foliage layer whose density, sizes or draw distance are no lengths";
            return false;
        }
    }
    if (r.left() != 0) {
        error = "bytes past its end";
        return false;
    }
    out.resolution = terrain.resolution;
    out.size       = terrain.size;
    out.heights    = std::move(terrain.heights);
    out.weights    = std::move(terrain.weights);
    out.layerTiling = terrain.layerTiling;
    out.foliage    = std::move(terrain.foliage);
    out.layers     = {};
    names          = std::move(read);
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
    for (size_t i = 0; i < terrain.layers.size(); ++i) {
        terrain.layers[i] = find(material, names.layers[i], "layer material");
    }
    for (size_t f = 0; f < terrain.foliage.size() && f < names.plants.size(); ++f) {
        std::vector<TerrainPlantPart>& parts = terrain.foliage[f].parts;
        for (size_t p = 0; p < parts.size() && p < names.plants[f].size(); ++p) {
            parts[p].mesh     = find(mesh, names.plants[f][p].first, "plant mesh");
            parts[p].material = find(material, names.plants[f][p].second, "plant material");
        }
    }
}

bool write(const std::filesystem::path& path, const TerrainAsset& terrain, const ResourceManager& resources) {
    const std::vector<uint8_t> body = encode(terrain, resources);
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) {
            LOG_ERROR("Terrain: cannot open '%s' for writing", tmp.string().c_str());
            return false;
        }
        out.write(MAGIC, sizeof(MAGIC));
        out.write(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION));
        out.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
        out.close();
        if (!out) {
            LOG_ERROR("Terrain: write to '%s' failed", tmp.string().c_str());
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        LOG_ERROR(
            "Terrain: rename '%s' -> '%s' failed: %s",
            tmp.string().c_str(),
            path.string().c_str(),
            ec.message().c_str()
        );
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool readBody(const std::filesystem::path& path, std::vector<uint8_t>& body, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot be opened";
        return false;
    }
    char     magic[sizeof(MAGIC)] = {};
    uint32_t version              = 0;
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

} // namespace Vkm::Engine::TerrainFile
