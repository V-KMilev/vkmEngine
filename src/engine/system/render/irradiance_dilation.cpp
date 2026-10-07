#include "system/render/irradiance_dilation.h"

namespace Vkm::Engine {

uint32_t dilateProbeGrid(ProbeGridSH& sh, uint32_t x, uint32_t y, uint32_t z) {
    const size_t cells = static_cast<size_t>(x) * y * z;
    if (cells == 0) return 0;
    for (const std::vector<glm::vec4>& grid : sh) {
        if (grid.size() != cells) return 0;
    }

    std::vector<bool> trusted(cells, false);
    uint32_t          refused = 0;
    for (size_t i = 0; i < cells; ++i) {
        trusted[i] = probeTrusted(sh[0][i]);
        if (!trusted[i]) ++refused;
    }
    // Nothing to repair, or nothing to repair it from.
    if (refused == 0 || refused == cells) return refused;

    // X fastest, then Y, then Z - the layout the GPU volume holds.
    const auto index = [x, y](uint32_t cx, uint32_t cy, uint32_t cz) {
        return static_cast<size_t>(cx) + static_cast<size_t>(cy) * x + static_cast<size_t>(cz) * x * y;
    };

    // One cell of spread per round, so the L1 diameter bounds the count. Bounded, not
    // "until done", so a wrong connectedness assumption ends unrepaired, not hung.
    const uint32_t rounds = x + y + z;
    uint32_t       filled = 0;

    for (uint32_t round = 0; round < rounds && filled < refused; ++round) {
        std::vector<bool> reached = trusted;

        for (uint32_t cz = 0; cz < z; ++cz) {
            for (uint32_t cy = 0; cy < y; ++cy) {
                for (uint32_t cx = 0; cx < x; ++cx) {
                    const size_t cell = index(cx, cy, cz);
                    if (trusted[cell]) continue;

                    glm::vec4 sum[4] = {};
                    uint32_t  count  = 0;

                    // `trusted`, not `reached`: a cell filled this round is not a source until the next.
                    const auto gather = [&](uint32_t nx, uint32_t ny, uint32_t nz) {
                        const size_t n = index(nx, ny, nz);
                        if (!trusted[n]) return;
                        for (int c = 0; c < 4; ++c) sum[c] += sh[c][n];
                        ++count;
                    };
                    if (cx > 0)     gather(cx - 1, cy, cz);
                    if (cx + 1 < x) gather(cx + 1, cy, cz);
                    if (cy > 0)     gather(cx, cy - 1, cz);
                    if (cy + 1 < y) gather(cx, cy + 1, cz);
                    if (cz > 0)     gather(cx, cy, cz - 1);
                    if (cz + 1 < z) gather(cx, cy, cz + 1);
                    if (count == 0) continue;

                    const float inv = 1.0f / static_cast<float>(count);
                    for (int c = 0; c < 4; ++c) sh[c][cell] = sum[c] * inv;
                    sh[0][cell].w = 1.0f;  // repaired, and no longer a verdict

                    reached[cell] = true;
                    ++filled;
                }
            }
        }

        trusted = reached;
    }

    return refused;
}

} // namespace Vkm::Engine
