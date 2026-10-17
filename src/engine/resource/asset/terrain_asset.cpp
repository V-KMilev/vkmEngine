#include "resource/asset/terrain_asset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace Vkm::Engine {

bool TerrainAsset::validResolution(uint32_t resolution) {
    if (resolution < MIN_RESOLUTION || resolution > MAX_RESOLUTION) return false;
    const uint32_t cells = resolution - 1;
    return (cells & (cells - 1)) == 0;
}

float TerrainAsset::heightAt(float x, float z) const {
    const int last = static_cast<int>(resolution) - 1;
    if (last < 1 || heights.size() < static_cast<size_t>(resolution) * resolution) return 0.0f;

    // Grid coordinates, clamped so the last square holds the far edge.
    const float gx = std::clamp((x / size + 0.5f) * static_cast<float>(last), 0.0f, static_cast<float>(last));
    const float gz = std::clamp((z / size + 0.5f) * static_cast<float>(last), 0.0f, static_cast<float>(last));
    const int   i  = std::min(static_cast<int>(gx), last - 1);
    const int   j  = std::min(static_cast<int>(gz), last - 1);
    const float fx = gx - static_cast<float>(i);
    const float fz = gz - static_cast<float>(j);

    const auto at  = [&](int a, int b) { return heights[static_cast<size_t>(b) * resolution + a]; };
    const float h00 = at(i, j);
    const float h10 = at(i + 1, j);
    const float h01 = at(i, j + 1);
    const float h11 = at(i + 1, j + 1);
    // The triangle on the diagonal's side the point is on.
    return fx >= fz ? h00 + fx * (h10 - h00) + fz * (h11 - h10)
                    : h00 + fz * (h01 - h00) + fx * (h11 - h01);
}

bool TerrainAsset::resample(uint32_t to) {
    const size_t from  = resolution;
    const bool   sound = from > 1 && heights.size() == from * from && weights.size() == from * from * 4;
    if (!validResolution(to) || !sound) return false;
    if (to == resolution) return true;

    const size_t side  = to;
    const float  scale = static_cast<float>(from - 1) / static_cast<float>(to - 1);
    // Vertex (i, j) of the new grid in the old's, its four neighbours there, and its share of each.
    struct Blend {
        std::array<size_t, 4> at;
        std::array<float, 4>  share;
    };
    const auto blendAt = [&](size_t i, size_t j) {
        const float  gx = static_cast<float>(i) * scale;
        const float  gz = static_cast<float>(j) * scale;
        const size_t i0 = std::min(static_cast<size_t>(gx), from - 2);
        const size_t j0 = std::min(static_cast<size_t>(gz), from - 2);
        const float  fx = gx - static_cast<float>(i0);
        const float  fz = gz - static_cast<float>(j0);
        const size_t v  = j0 * from + i0;
        Blend        blend;
        blend.at    = {v, v + 1, v + from, v + from + 1};
        blend.share = {(1 - fx) * (1 - fz), fx * (1 - fz), (1 - fx) * fz, fx * fz};
        return blend;
    };

    std::vector<float>   newHeights(side * side);
    std::vector<uint8_t> newWeights(side * side * 4);
    std::vector<std::vector<uint8_t>> newDensity(foliage.size());
    for (size_t f = 0; f < foliage.size(); ++f) {
        if (foliage[f].density.size() == from * from) newDensity[f].resize(side * side);
    }
    for (size_t j = 0; j < side; ++j) {
        for (size_t i = 0; i < side; ++i) {
            const size_t v     = j * side + i;
            const float  x     = (static_cast<float>(i) / static_cast<float>(to - 1) - 0.5f) * size;
            const float  z     = (static_cast<float>(j) / static_cast<float>(to - 1) - 0.5f) * size;
            const Blend  blend = blendAt(i, j);
            newHeights[v] = heightAt(x, z);

            // The shares, blended, then rounded so they sum to full; the rounding goes to the most.
            std::array<float, 4> mix{};
            for (size_t n = 0; n < 4; ++n) {
                for (size_t c = 0; c < 4; ++c) mix[c] += blend.share[n] * weights[blend.at[n] * 4 + c];
            }
            int total = 0;
            for (size_t c = 0; c < 4; ++c) {
                newWeights[v * 4 + c] = static_cast<uint8_t>(std::lround(mix[c]));
                total += newWeights[v * 4 + c];
            }
            const auto most  = std::max_element(mix.begin(), mix.end());
            uint8_t&   share = newWeights[v * 4 + static_cast<size_t>(std::distance(mix.begin(), most))];
            share = static_cast<uint8_t>(std::clamp(share + 255 - total, 0, 255));

            for (size_t f = 0; f < foliage.size(); ++f) {
                if (newDensity[f].empty()) continue;
                float density = 0.0f;
                for (size_t n = 0; n < 4; ++n) density += blend.share[n] * foliage[f].density[blend.at[n]];
                newDensity[f][v] = static_cast<uint8_t>(std::lround(density));
            }
        }
    }
    resolution = to;
    heights    = std::move(newHeights);
    weights    = std::move(newWeights);
    for (size_t f = 0; f < foliage.size(); ++f) foliage[f].density = std::move(newDensity[f]);
    return true;
}

glm::vec3 TerrainAsset::normalAt(int i, int j) const {
    const int last = static_cast<int>(resolution) - 1;
    if (last < 1 || heights.size() < static_cast<size_t>(resolution) * resolution) return {0.0f, 1.0f, 0.0f};
    i = std::clamp(i, 0, last);
    j = std::clamp(j, 0, last);
    const auto at = [&](int a, int b) {
        return heights[static_cast<size_t>(std::clamp(b, 0, last)) * resolution + std::clamp(a, 0, last)];
    };
    // Central differences, one-sided at the edges.
    const int   i0 = std::max(i - 1, 0);
    const int   i1 = std::min(i + 1, last);
    const int   j0 = std::max(j - 1, 0);
    const int   j1 = std::min(j + 1, last);
    const float dx = (at(i1, j) - at(i0, j)) / (static_cast<float>(i1 - i0) * spacing());
    const float dz = (at(i, j1) - at(i, j0)) / (static_cast<float>(j1 - j0) * spacing());
    return glm::normalize(glm::vec3(-dx, 1.0f, -dz));
}

} // namespace Vkm::Engine
