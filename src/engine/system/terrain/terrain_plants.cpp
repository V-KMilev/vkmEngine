#include "system/terrain/terrain_plants.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Vkm::Engine::TerrainPlants {

namespace {

/// A well-mixed 32 bits from three: the same on every machine.
uint32_t hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

/// The k-th of a square's numbers, in [0, 1).
float unit(uint32_t square, uint32_t seed, uint32_t k) {
    return static_cast<float>(hash(square, seed, k) >> 8) * (1.0f / 16777216.0f);
}

/**
 * @brief The plants of layer @p layer grid square @p i, @p j grows, appended to @p out.
 *
 * @param full Plants the square holds where its corners are painted full.
 */
void growSquare(
    const TerrainAsset& terrain,
    uint32_t layer,
    uint32_t i,
    uint32_t j,
    float full,
    std::vector<Plant>& out
) {
    const TerrainFoliage&       foliage = terrain.foliage[layer];
    const std::vector<uint8_t>& density = foliage.density;
    const uint32_t side = terrain.resolution;
    const size_t   at   = static_cast<size_t>(j) * side + i;
    const int      sum  = density[at] + density[at + 1] + density[at + side] + density[at + side + 1];
    if (sum == 0) return;

    // As many as the square's share asks, the fraction left to chance.
    const uint32_t square   = j * side + i;
    const uint32_t seed     = foliage.seed;
    // Capped before it becomes a count, which a huge or broken density would overflow.
    const float asked = static_cast<float>(sum) / (4.0f * 255.0f) * full;
    if (!(asked > 0.0f)) return;
    const float    expected = std::min(asked, static_cast<float>(MAX_PER_SQUARE));
    const float    whole    = std::floor(expected);
    const bool     extra    = unit(square, seed, 0) < expected - whole;
    const uint32_t count    = std::min(static_cast<uint32_t>(whole) + (extra ? 1u : 0u), MAX_PER_SQUARE);
    const float    spacing  = terrain.spacing();
    const float    half     = 0.5f * terrain.size;
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t first = 1 + k * 4;
        const float    x     = (static_cast<float>(i) + unit(square, seed, first)) * spacing - half;
        const float    z     = (static_cast<float>(j) + unit(square, seed, first + 1)) * spacing - half;
        const float    turn  = unit(square, seed, first + 2) * glm::two_pi<float>();
        const float    grown = unit(square, seed, first + 3);
        const float    scale = foliage.minScale + (foliage.maxScale - foliage.minScale) * grown;
        glm::mat4 local = glm::translate(glm::mat4(1.0f), glm::vec3(x, terrain.heightAt(x, z), z));
        local = glm::rotate(local, turn, glm::vec3(0.0f, 1.0f, 0.0f));
        local = glm::scale(local, glm::vec3(scale));
        out.push_back({layer, local});
    }
}

} // namespace

void scatter(const TerrainAsset& terrain, std::vector<Plant>& out) {
    out.clear();
    const uint32_t squares = terrain.resolution > 0 ? terrain.resolution - 1 : 0;
    grow(terrain, 0, squares, 0, squares, out);
}

void grow(
    const TerrainAsset& terrain,
    uint32_t i0,
    uint32_t i1,
    uint32_t j0,
    uint32_t j1,
    std::vector<Plant>& out
) {
    const uint32_t side = terrain.resolution;
    const size_t   grid = static_cast<size_t>(side) * side;
    if (!TerrainAsset::validResolution(side) || terrain.heights.size() != grid) return;

    const float area = terrain.spacing() * terrain.spacing();
    for (uint32_t layer = 0; layer < terrain.foliage.size(); ++layer) {
        const TerrainFoliage& foliage = terrain.foliage[layer];
        if (foliage.density.size() != grid || foliage.parts.empty()) continue;
        if (foliage.perSquareMetre <= 0.0f) continue;
        const float full = foliage.perSquareMetre * area;
        for (uint32_t j = j0; j < std::min(j1, side - 1); ++j) {
            for (uint32_t i = i0; i < std::min(i1, side - 1); ++i) {
                growSquare(terrain, layer, i, j, full, out);
            }
        }
    }
}

bool sameScatter(const TerrainFoliage& a, const TerrainFoliage& b) {
    return a.parts.empty() == b.parts.empty() && a.density.size() == b.density.size()
        && a.perSquareMetre == b.perSquareMetre && a.minScale == b.minScale && a.maxScale == b.maxScale
        && a.seed == b.seed;
}

} // namespace Vkm::Engine::TerrainPlants
