#include "system/ocean/ocean_swell.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>

#include <glm/gtc/constants.hpp>

#include "core/math/random.h"
#include "debug/profiler.h"
#include "ecs/component/render/ocean.h"
#include "platform/threading/thread_pool.h"

namespace Vkm::Engine {

namespace {

// The swell's stream of the ocean's seed.
constexpr uint64_t SWELL_STREAM = 0x7377656c6cull;

// The grid's texels, and the waves a cascade's repeat holds at the most.
constexpr int    FIELD          = SWELL_FIELD;
constexpr size_t TEXELS         = size_t(FIELD) * FIELD;
constexpr float  WAVES_A_REPEAT = float(FIELD / OceanSwell::SWELL_TEXELS_PER_WAVE);

// How near a whole number of the last cascade's repeats one of a larger cascade may fall: nearer,
// and the two line up. And the step the last's repeat shrinks by, looking for one that does not.
constexpr float WHOLE_MARGIN = 0.1f;
constexpr float LAST_SHRINK  = 0.99f;

std::atomic<uint64_t> g_versions{0};

// Grids let go by moments that are gone, kept for the next: a sea is drawn anew each frame and tick.
constexpr size_t MOST_SPARE = 4;

struct Spare {
    std::mutex                           mutex;
    std::vector<std::vector<SwellTexel>> grids;
};

// Never destroyed, so a moment let go as the process ends still finds it.
Spare& spare() {
    static Spare* s_spare = new Spare();
    return *s_spare;
}

// A grid of @p texels, cleared: a spare one when there is one.
std::vector<SwellTexel> takeGrid(size_t texels) {
    std::vector<SwellTexel> grid;
    {
        Spare&                            kept = spare();
        const std::lock_guard<std::mutex> lock(kept.mutex);
        if (!kept.grids.empty()) {
            grid = std::move(kept.grids.back());
            kept.grids.pop_back();
        }
    }
    grid.assign(texels, SwellTexel{});
    return grid;
}

// Keep @p grid for the next moment, unless enough are kept.
void giveGrid(std::vector<SwellTexel> && grid) {
    if (grid.capacity() == 0) return;
    Spare&                            kept = spare();
    const std::lock_guard<std::mutex> lock(kept.mutex);
    if (kept.grids.size() < MOST_SPARE) kept.grids.push_back(std::move(grid));
}

// The wavenumber, in grid steps, that index @p i holds: past the middle, the negative ones.
int binOf(int i) {
    return i < FIELD / 2 ? i : i - FIELD;
}

// The index holding the wavenumber opposite @p i's.
int mirrorOf(int i) {
    return (FIELD - i) % FIELD;
}

// Where index @p i is loaded for the butterflies: its bits reversed, as shaders/ocean_swell.glsl.
uint32_t shuffled(uint32_t i) {
    uint32_t out = 0;
    for (int bit = 1; bit < FIELD; bit <<= 1) {
        out = (out << 1) | (i & 1u);
        i >>= 1;
    }
    return out;
}

glm::vec2 times(const glm::vec2& a, const glm::vec2& b) {
    return {a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x};
}

// @p a times i.
glm::vec2 turned(const glm::vec2& a) {
    return {-a.y, a.x};
}

// Two real fields' spectra as one complex one: @p x + i @p y, which transforms to x's field plus
// i y's while both are conjugate-symmetric.
glm::vec2 packed(const glm::vec2& x, const glm::vec2& y) {
    return x + turned(y);
}

// The butterflies' twiddles, a span's j-th at span - 1 + j: worked out as the shader does, once.
const std::array<glm::vec2, FIELD - 1>& twiddles() {
    static const std::array<glm::vec2, FIELD - 1> s_table = [] {
        std::array<glm::vec2, FIELD - 1> made{};
        for (uint32_t span = 1; span < uint32_t(FIELD); span <<= 1) {
            for (uint32_t j = 0; j < span; ++j) {
                const float angle = glm::pi<float>() * float(j) / float(span);
                made[span - 1u + j] = glm::vec2(std::cos(angle), std::sin(angle));
            }
        }
        return made;
    }();
    return s_table;
}

// One butterfly of @p line, two complex numbers a texel, at @p a and @p b through @p w.
void butterfly(glm::vec4* line, uint32_t a, uint32_t b, const glm::vec2& w) {
    const glm::vec4 low  = line[a];
    const glm::vec4 high = line[b];
    const glm::vec4 turn(times(w, glm::vec2(high.x, high.y)), times(w, glm::vec2(high.z, high.w)));
    line[a] = low + turn;
    line[b] = low - turn;
}

// The inverse transform of @p first and @p second, in place, both through each twiddle: loaded in
// shuffled() order, they end in natural order. The butterflies of shaders/ocean_swell.glsl, in its
// order.
void transform(glm::vec4* first, glm::vec4* second) {
    const std::array<glm::vec2, FIELD - 1>& w = twiddles();
    for (uint32_t span = 1; span < uint32_t(FIELD); span <<= 1) {
        for (uint32_t t = 0; t < uint32_t(FIELD / 2); ++t) {
            const uint32_t j = t & (span - 1u);
            const uint32_t a = ((t - j) << 1u) + j;
            butterfly(first, a, a + span, w[span - 1u + j]);
            butterfly(second, a, a + span, w[span - 1u + j]);
        }
    }
}

// The cascade's band ends WAVES_A_REPEAT steps out, so only the rows within reach of the middle
// hold waves; the rest stay clear.
constexpr int WAVE_REACH = int(WAVES_A_REPEAT) + 1;
constexpr int WAVE_ROWS  = 2 * WAVE_REACH + 1;
static_assert(WAVE_ROWS <= FIELD, "The rows with waves are the grid's at most");

// Lines a pool task transforms: enough that a task outweighs handing it out.
constexpr size_t LINES_A_TASK = 8;

// Row @p row of cascade @p c along X: each texel its wave and the one against it, rolled to
// @p cycle, transformed into @p out, which the columns then transform in place.
void rollRow(SwellTexel* out, const OceanSwell& swell, uint32_t c, int row, float cycle) {
    const float     chop = swell.choppiness();
    const float     step = glm::two_pi<float>() / swell.cascade(c).size;
    const SwellBin* bins = swell.bins(c);

    std::array<glm::vec4, FIELD> line{};
    std::array<glm::vec4, FIELD> lineSlopes{};
    for (int col = 0; col < FIELD; ++col) {
        const SwellBin& ahead  = bins[size_t(row) * FIELD + col];
        const SwellBin& behind = bins[size_t(mirrorOf(row)) * FIELD + mirrorOf(col)];
        glm::vec4 m(0.0f);
        glm::vec4 s(0.0f);
        const glm::vec2 k      = step * glm::vec2(binOf(col), binOf(row));
        const float     length = glm::length(k);
        if (length > 0.0f && (ahead.turns > 0.0f || behind.turns > 0.0f)) {
            const float     turns = std::max(ahead.turns, behind.turns);
            const float     angle = glm::two_pi<float>() * glm::fract(turns * cycle);
            const glm::vec2 roll(std::cos(angle), std::sin(angle));
            const glm::vec2 h = times(ahead.height, glm::vec2(roll.x, -roll.y))
                + times(glm::vec2(behind.height.x, -behind.height.y), roll);
            // The carry is i chop k/|k| h - toward the crest, so crests sharpen and troughs round
            // (Tessendorf's D, its lambda negative) - the height's slope i k h, the carry's change
            // along X and Z -chop k k/|k| h. The slopes packed are i k_x h + i (i k_z h):
            // i k_x h - k_z h.
            const glm::vec2 across = turned(h) / length * chop;
            const glm::vec2 dx     = across * k.x;
            const glm::vec2 dz     = across * k.y;
            m = glm::vec4(packed(h, dx), packed(dz, h * (-chop * k.x * k.y / length)));
            s = glm::vec4(
                turned(h) * k.x - h * k.y,
                packed(h * (-chop * k.x * k.x / length), h * (-chop * k.y * k.y / length))
            );
        }
        line[shuffled(uint32_t(col))]       = m;
        lineSlopes[shuffled(uint32_t(col))] = s;
    }
    transform(line.data(), lineSlopes.data());
    for (int col = 0; col < FIELD; ++col) {
        out[size_t(row) * FIELD + col] = {line[col], lineSlopes[col]};
    }
}

// Column @p col of a cascade's grid @p out along Z, in place, once its rows are done.
void transformColumn(SwellTexel* out, int col) {
    std::array<glm::vec4, FIELD> line{};
    std::array<glm::vec4, FIELD> lineSlopes{};
    for (int row = 0; row < FIELD; ++row) {
        line[shuffled(uint32_t(row))]       = out[size_t(row) * FIELD + col].moved;
        lineSlopes[shuffled(uint32_t(row))] = out[size_t(row) * FIELD + col].slopes;
    }
    transform(line.data(), lineSlopes.data());
    for (int row = 0; row < FIELD; ++row) {
        // Each pair transforms to one field plus i the other: x the first, y the second.
        const glm::vec4& m = line[row];
        const glm::vec4& s = lineSlopes[row];
        out[size_t(row) * FIELD + col] = {glm::vec4(m.y, m.x, m.z, m.w), s};
    }
}

// The cascades a sea's band is laid over.
struct Layout {
    uint32_t                                     count = 0;
    std::array<SwellCascade, MAX_SWELL_CASCADES> cascades{};
};

// Whether a repeat of @p size metres lines up with one of @p layout's cascades: falls within
// WHOLE_MARGIN of a whole number of times into it.
bool linesUp(const Layout& layout, float size) {
    for (uint32_t c = 0; c < layout.count; ++c) {
        const float times = layout.cascades[c].size / size;
        if (std::abs(times - std::round(times)) < WHOLE_MARGIN) return true;
    }
    return false;
}

Layout layOut(const OceanShape& ocean) {
    Layout layout;
    const float shortest = std::max(ocean.minWavelength, 0.01f);
    const float longest  = ocean.maxWavelength;
    if (!(longest > shortest)) return layout;
    float size = longest;
    float top  = longest;
    for (int c = 0; c < MAX_SWELL_CASCADES; ++c) {
        // The last, which reaches the shortest wave, repeats as far as that wave still resolves: a
        // short band in a tile the ratio left small holds a few dozen waves, and its repeat shows.
        // Then shrunk off any larger repeat it lines up with, though never below where the ratio
        // put it, whose repeat lines up with none.
        const bool last = size / WAVES_A_REPEAT <= shortest;
        if (last) {
            const float least = size;
            size = shortest * WAVES_A_REPEAT;
            while (linesUp(layout, size) && size * LAST_SHRINK >= least) size *= LAST_SHRINK;
        }
        const float bottom = last ? shortest : size / WAVES_A_REPEAT;
        layout.cascades[layout.count++] = {size, top, bottom};
        if (last) break;
        top  = bottom;
        size = bottom * OceanSwell::SWELL_CASCADE_RATIO;
    }
    return layout;
}

// A standard normal pair, by Box-Muller.
glm::vec2 gaussian(Math::Rng& rng) {
    const float  u1     = (float(rng.nextU32() >> 8) + 0.5f) / 16777216.0f;
    const float  u2     = rng.nextFloat();
    const double radius = std::sqrt(-2.0 * std::log(double(u1)));
    const double angle  = glm::two_pi<double>() * u2;
    return glm::vec2(float(radius * std::cos(angle)), float(radius * std::sin(angle)));
}

} // namespace

std::shared_ptr<const OceanSwell> OceanSwell::of(const OceanShape& ocean) {
    const OceanSpectrum spectrum = OceanSpectrum::of(ocean);
    const Layout        layout   = layOut(ocean);
    if (!spectrum.any() || layout.count == 0) return nullptr;

    auto swell = std::make_shared<OceanSwell>();
    swell->m_count    = layout.count;
    swell->m_cascades = layout.cascades;
    swell->m_bins.assign(TEXELS * layout.count, SwellBin{});

    // Every wavenumber of a cascade's band, a Gaussian of the spectrum's height there: halved, as
    // the wave it rolls against holds the other half (Tessendorf's h0).
    Math::Rng    rng(ocean.seed, SWELL_STREAM);
    const double turn = glm::two_pi<double>() / OceanSpectrum::LOOP;
    double       sum  = 0.0;  // of |h0|
    double       m0   = 0.0;  // the height's variance the spectrum gives the grids, as expected
    double       m2   = 0.0;  // and the slope's
    for (uint32_t c = 0; c < layout.count; ++c) {
        const SwellCascade& cascade = layout.cascades[c];
        const float         step    = glm::two_pi<float>() / cascade.size;
        SwellBin*           bins    = swell->m_bins.data() + TEXELS * c;
        for (int row = 0; row < FIELD; ++row) {
            for (int col = 0; col < FIELD; ++col) {
                const glm::vec2 k      = step * glm::vec2(binOf(col), binOf(row));
                const float     length = glm::length(k);
                if (!(length > 0.0f)) continue;
                const float wave = glm::two_pi<float>() / length;
                // The first cascade holds its longest wave; each holds its shortest.
                const bool  held = wave >= cascade.shortest
                    && (c == 0 ? wave <= cascade.longest : wave < cascade.longest);
                if (!held) continue;
                SwellBin& bin = bins[size_t(row) * FIELD + col];
                const float density = spectrum.density(k);
                bin.height = gaussian(rng) * (0.5f * step * std::sqrt(density));
                // A wave too slow for half a turn a loop still rolls one.
                const double omega = OceanSpectrum::frequencyOf(length, spectrum.depth);
                bin.turns = float(std::max(std::round(omega / turn), 1.0));

                sum += glm::length(bin.height);
                const double share = double(step) * step * density;
                m0 += share;
                m2 += double(length) * length * share;
            }
        }
    }

    // Each crest draws the water `choppiness` amplitudes toward it, held where the squeeze it
    // rarely passes would fold the surface.
    const float slope          = float(std::sqrt(m2));
    const float chop           = std::clamp(ocean.choppiness, 0.0f, 1.0f);
    swell->m_choppiness        = chop / std::max(RARE_SPREADS * slope, 1.0f);
    swell->m_significantHeight = float(4.0 * std::sqrt(m0));
    swell->m_squeezeSpread     = swell->m_choppiness * slope;
    // Every wave and the one against it in step: the most each carries is the two heights.
    const float most = float(2.0 * sum);
    swell->m_farthestReach = glm::vec2(swell->m_choppiness * most, most);
    swell->m_version       = ++g_versions;
    return swell;
}

float OceanSwell::shortestOf(const OceanShape& ocean) {
    const Layout layout = layOut(ocean);
    if (layout.count == 0) return std::max(ocean.minWavelength, 0.01f);
    return layout.cascades[layout.count - 1].shortest;
}

SwellMoment::SwellMoment(std::shared_ptr<const OceanSwell> swell, float cycle)
    : m_swell(std::move(swell))
    , m_cycle(cycle)
{}

SwellMoment::~SwellMoment() {
    giveGrid(std::move(m_grids));
}

const SwellTexel* SwellMoment::grid(uint32_t c) const {
    std::call_once(m_drawn, [this] { draw(); });
    return m_grids.data() + TEXELS * c;
}

void SwellMoment::draw() const {
    PROFILE_SCOPE("Ocean/SwellDraw");
    const OceanSwell& swell = *m_swell;
    m_grids = takeGrid(TEXELS * swell.cascadeCount());

    // Each line transforms alone, so the rows of every cascade, then its columns, go to the pool.
    const size_t cascades = swell.cascadeCount();
    parallelFor(cascades * WAVE_ROWS, LINES_A_TASK, [&](size_t i) {
        const auto c   = uint32_t(i / WAVE_ROWS);
        const int  bin = int(i % WAVE_ROWS) - WAVE_REACH;
        rollRow(m_grids.data() + TEXELS * c, swell, c, bin < 0 ? bin + FIELD : bin, m_cycle);
    });
    parallelFor(cascades * FIELD, LINES_A_TASK, [&](size_t i) {
        transformColumn(m_grids.data() + TEXELS * (i / FIELD), int(i % FIELD));
    });
}

void SwellMoment::corners(uint32_t c, const glm::vec2& rest, const SwellTexel* at[4], float weight[4]) const {
    const SwellTexel* texels = grid(c);
    const glm::vec2   p      = rest / m_swell->cascade(c).size * float(FIELD);
    const glm::vec2   floor  = glm::floor(p);
    const glm::vec2   f      = p - floor;
    const int x0 = ((int(floor.x) % FIELD) + FIELD) % FIELD;
    const int z0 = ((int(floor.y) % FIELD) + FIELD) % FIELD;
    const int x1 = (x0 + 1) % FIELD;
    const int z1 = (z0 + 1) % FIELD;
    at[0] = &texels[size_t(z0) * FIELD + x0];
    at[1] = &texels[size_t(z0) * FIELD + x1];
    at[2] = &texels[size_t(z1) * FIELD + x0];
    at[3] = &texels[size_t(z1) * FIELD + x1];
    weight[0] = (1.0f - f.x) * (1.0f - f.y);
    weight[1] = f.x * (1.0f - f.y);
    weight[2] = (1.0f - f.x) * f.y;
    weight[3] = f.x * f.y;
}

glm::vec3 SwellMoment::offsetAt(const glm::vec2& rest) const {
    glm::vec3 carry(0.0f);
    for (uint32_t c = 0; c < m_swell->cascadeCount(); ++c) {
        const SwellTexel* at[4];
        float             weight[4];
        corners(c, rest, at, weight);
        for (int i = 0; i < 4; ++i) carry += glm::vec3(at[i]->moved) * weight[i];
    }
    return carry;
}

glm::vec3 SwellMoment::movedAt(const glm::vec2& rest, glm::vec3& alongX, glm::vec3& alongZ) const {
    glm::vec3 carry(0.0f);
    for (uint32_t c = 0; c < m_swell->cascadeCount(); ++c) {
        const SwellTexel* at[4];
        float             weight[4];
        corners(c, rest, at, weight);
        for (int i = 0; i < 4; ++i) {
            const glm::vec4& m = at[i]->moved;
            const glm::vec4& s = at[i]->slopes;
            carry  += glm::vec3(m) * weight[i];
            alongX += glm::vec3(s.z, s.x, m.w) * weight[i];
            alongZ += glm::vec3(m.w, s.y, s.w) * weight[i];
        }
    }
    return carry;
}

} // namespace Vkm::Engine
