#include "system/render/probe_fill.h"

#include <cstddef>
#include <map>

namespace Vkm::Engine {

std::vector<ProbeFill> probeFillWeights(
    const std::vector<bool>& trusted,
    uint32_t x,
    uint32_t y,
    uint32_t z
) {
    const size_t cells = static_cast<size_t>(x) * y * z;
    if (cells == 0 || trusted.size() != cells) return {};

    // Each cell as a blend of trusted ones: a trusted cell is itself, a refused one nothing yet.
    std::vector<std::map<uint32_t, float>> blend(cells);
    std::vector<bool> reached = trusted;
    uint32_t          refused = 0;
    for (size_t i = 0; i < cells; ++i) {
        if (trusted[i]) {
            blend[i][static_cast<uint32_t>(i)] = 1.0f;
        } else {
            ++refused;
        }
    }
    // Nothing to repair, or nothing to repair it from.
    if (refused == 0 || refused == cells) return {};

    // X fastest, then Y, then Z - the layout the GPU volume holds.
    const auto index = [x, y](uint32_t cx, uint32_t cy, uint32_t cz) {
        return static_cast<size_t>(cx) + static_cast<size_t>(cy) * x + static_cast<size_t>(cz) * x * y;
    };

    // One cell of spread per round, so the L1 diameter bounds the count. Bounded, not
    // "until done", so a wrong connectedness assumption ends unrepaired, not hung.
    const uint32_t rounds = x + y + z;
    uint32_t       filled = 0;

    for (uint32_t round = 0; round < rounds && filled < refused; ++round) {
        const std::vector<bool> sources = reached;

        for (uint32_t cz = 0; cz < z; ++cz) {
            for (uint32_t cy = 0; cy < y; ++cy) {
                for (uint32_t cx = 0; cx < x; ++cx) {
                    const size_t cell = index(cx, cy, cz);
                    if (sources[cell]) continue;

                    // `sources`, not `reached`: a cell filled this round is not a source until the next.
                    std::vector<size_t> from;
                    const auto gather = [&](uint32_t nx, uint32_t ny, uint32_t nz) {
                        const size_t n = index(nx, ny, nz);
                        if (sources[n]) from.push_back(n);
                    };
                    if (cx > 0)     gather(cx - 1, cy, cz);
                    if (cx + 1 < x) gather(cx + 1, cy, cz);
                    if (cy > 0)     gather(cx, cy - 1, cz);
                    if (cy + 1 < y) gather(cx, cy + 1, cz);
                    if (cz > 0)     gather(cx, cy, cz - 1);
                    if (cz + 1 < z) gather(cx, cy, cz + 1);
                    if (from.empty()) continue;

                    const float share = 1.0f / static_cast<float>(from.size());
                    for (const size_t n : from) {
                        for (const auto& [source, weight] : blend[n]) blend[cell][source] += weight * share;
                    }
                    reached[cell] = true;
                    ++filled;
                }
            }
        }
    }

    std::vector<ProbeFill> fills;
    fills.reserve(filled);
    for (size_t i = 0; i < cells; ++i) {
        if (trusted[i] || blend[i].empty()) continue;
        fills.push_back({static_cast<uint32_t>(i), {blend[i].begin(), blend[i].end()}});
    }
    return fills;
}

} // namespace Vkm::Engine
