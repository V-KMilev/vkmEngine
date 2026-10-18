#include "system/terrain/terrain_brush.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Vkm::Engine::TerrainBrush {

namespace {

/// Local metres of grid vertex @p i along either axis.
float coordinate(const TerrainAsset& terrain, int i) {
    return static_cast<float>(i) * terrain.spacing() - 0.5f * terrain.size;
}

/// Whether @p terrain's arrays hold its grid, which a dab may then write.
bool sound(const TerrainAsset& terrain) {
    const size_t side = terrain.resolution;
    return TerrainAsset::validResolution(terrain.resolution) && terrain.heights.size() == side * side
        && terrain.weights.size() == side * side * 4;
}

/**
 * @brief @p value moved @p t of the way to @p target, by at least one step while it is not there:
 *        a light dab a frame still gets somewhere.
 */
uint8_t toward(uint8_t value, float target, float t) {
    const float delta = (target - static_cast<float>(value)) * std::min(t, 1.0f);
    if (delta == 0.0f) return value;
    const float step = std::abs(delta) < 1.0f ? std::copysign(1.0f, delta) : delta;
    return static_cast<uint8_t>(std::lround(std::clamp(static_cast<float>(value) + step, 0.0f, 255.0f)));
}

/**
 * @brief Call @p fn(index, share) for each grid vertex under @p dab that its effect reaches.
 *
 * @return The region visited.
 */
template<typename Fn>
Region eachUnder(const TerrainAsset& terrain, const Dab& dab, Fn&& fn) {
    const Region region = reach(terrain, dab);
    for (int j = region.j0; j <= region.j1; ++j) {
        for (int i = region.i0; i <= region.i1; ++i) {
            const glm::vec2 at(coordinate(terrain, i), coordinate(terrain, j));
            const float     share = dab.strength * falloff(dab, glm::distance(at, dab.centre));
            if (share > 0.0f) fn(static_cast<size_t>(j) * terrain.resolution + i, share);
        }
    }
    return region;
}

} // namespace

void Region::add(const Region& other) {
    if (other.empty()) return;
    if (empty()) {
        *this = other;
        return;
    }
    i0 = std::min(i0, other.i0);
    j0 = std::min(j0, other.j0);
    i1 = std::max(i1, other.i1);
    j1 = std::max(j1, other.j1);
}

float falloff(const Dab& dab, float distance) {
    if (dab.radius <= 0.0f || distance >= dab.radius) return 0.0f;
    const float core = std::clamp(dab.hardness, 0.0f, 1.0f) * dab.radius;
    if (distance <= core) return 1.0f;
    const float t = (distance - core) / (dab.radius - core);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

Region reach(const TerrainAsset& terrain, const Dab& dab) {
    Region region;
    if (!sound(terrain) || dab.radius <= 0.0f) return region;
    const int   last   = static_cast<int>(terrain.resolution) - 1;
    const float toGrid = 1.0f / terrain.spacing();
    const float half   = 0.5f * terrain.size;
    region.i0 = std::max(static_cast<int>(std::ceil((dab.centre.x - dab.radius + half) * toGrid)), 0);
    region.j0 = std::max(static_cast<int>(std::ceil((dab.centre.y - dab.radius + half) * toGrid)), 0);
    region.i1 = std::min(static_cast<int>(std::floor((dab.centre.x + dab.radius + half) * toGrid)), last);
    region.j1 = std::min(static_cast<int>(std::floor((dab.centre.y + dab.radius + half) * toGrid)), last);
    return region;
}

bool hit(
    const TerrainAsset& terrain,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float range,
    float& t
) {
    if (!sound(terrain)) return false;
    // Only the span over the grid's square is marched.
    const float half = 0.5f * terrain.size;
    float enter = 0.0f;
    float leave = range;
    for (const int axis : {0, 2}) {
        if (std::abs(direction[axis]) < 1e-8f) {
            if (std::abs(origin[axis]) > half) return false;
            continue;
        }
        float a = (-half - origin[axis]) / direction[axis];
        float b = (half - origin[axis]) / direction[axis];
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a);
        leave = std::min(leave, b);
    }
    if (enter > leave) return false;

    const auto above = [&](float s) {
        const glm::vec3 p = origin + direction * s;
        return p.y - terrain.heightAt(p.x, p.z);
    };
    const float step = 0.5f * terrain.spacing();
    float       was  = enter;
    if (above(was) < 0.0f) return false;  // starts under the surface
    for (float s = enter + step;; s += step) {
        const float at = std::min(s, leave);
        if (above(at) <= 0.0f) {
            float low  = was;
            float high = at;
            for (int k = 0; k < 24; ++k) {
                const float mid = 0.5f * (low + high);
                (above(mid) > 0.0f ? low : high) = mid;
            }
            t = high;
            return true;
        }
        if (at >= leave) return false;
        was = at;
    }
}

Region raise(TerrainAsset& terrain, const Dab& dab, float metres) {
    return eachUnder(terrain, dab, [&](size_t at, float share) { terrain.heights[at] += metres * share; });
}

Region smooth(TerrainAsset& terrain, const Dab& dab) {
    // Each mean from the heights before the dab, so the result is the same in any visiting order.
    const std::vector<float> before = terrain.heights;
    const int side = static_cast<int>(terrain.resolution);
    return eachUnder(terrain, dab, [&](size_t at, float share) {
        const int i = static_cast<int>(at % terrain.resolution);
        const int j = static_cast<int>(at / terrain.resolution);
        float sum   = 0.0f;
        int   count = 0;
        for (int b = std::max(j - 1, 0); b <= std::min(j + 1, side - 1); ++b) {
            for (int a = std::max(i - 1, 0); a <= std::min(i + 1, side - 1); ++a) {
                sum += before[static_cast<size_t>(b) * side + a];
                ++count;
            }
        }
        terrain.heights[at] += (sum / static_cast<float>(count) - before[at]) * std::min(share, 1.0f);
    });
}

Region flatten(TerrainAsset& terrain, const Dab& dab, float height) {
    return eachUnder(terrain, dab, [&](size_t at, float share) {
        terrain.heights[at] += (height - terrain.heights[at]) * std::min(share, 1.0f);
    });
}

Region paint(TerrainAsset& terrain, const Dab& dab, uint32_t layer) {
    if (layer >= MaterialAsset::MAX_LAYERS) return {};
    return eachUnder(terrain, dab, [&](size_t at, float share) {
        // The others fade; the painted layer takes what they give up.
        uint8_t* shares = &terrain.weights[at * 4];
        int      others = 0;
        for (uint32_t c = 0; c < 4; ++c) {
            if (c == layer) continue;
            shares[c] = toward(shares[c], 0.0f, share);
            others += shares[c];
        }
        shares[layer] = static_cast<uint8_t>(std::max(255 - others, 0));
    });
}

Region grow(TerrainAsset& terrain, const Dab& dab, uint32_t foliage, float target) {
    if (foliage >= terrain.foliage.size() || !sound(terrain)) return {};
    std::vector<uint8_t>& density = terrain.foliage[foliage].density;
    density.resize(terrain.heights.size(), 0);
    const float full = std::clamp(target, 0.0f, 1.0f) * 255.0f;
    return eachUnder(terrain, dab, [&](size_t at, float share) {
        density[at] = toward(density[at], full, share);
    });
}

} // namespace Vkm::Engine::TerrainBrush
