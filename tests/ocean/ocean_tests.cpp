#include "support.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "ecs/component/core/transform.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "system/ocean/ocean_surface.h"
#include "system/ocean/ocean_swell.h"
#include "system/ocean/ocean_system.h"

namespace {

// An open sea: a fresh breeze over a hundred kilometres, the band wide enough to hold
// nearly all its energy.
Ocean openSea() {
    Ocean sea;
    sea.windSpeed     = 10.0f;
    sea.fetch         = 100.0f;
    sea.minWavelength = 0.5f;
    sea.maxWavelength = 2000.0f;
    return sea;
}

// Where the water resting at @p rest goes, as a world point.
glm::vec3 movedPoint(const OceanSurface& surface, const glm::vec2& rest) {
    const glm::vec3 moved = surface.displacement(rest);
    return glm::vec3(rest.x + moved.x, moved.y, rest.y + moved.z);
}

// The spectrum is JONSWAP's (Hasselmann et al. 1973): the field fit of the same measurements gives
// Hs = 0.0016 U^2/g sqrt(gF/U^2). The parametric spectrum, peak enhancement and all, stands about a
// third higher than that fit, so the height is held to within 40% of it.
void testTheSeaIsAsHighAsTheWindMakesIt() {
    std::printf("How high the wind raises the sea:\n");

    const Ocean   sea      = openSea();
    const float   wind     = sea.windSpeed;
    const float   fetch    = sea.fetch * 1000.0f;
    const float   expected = 0.0016f * wind * wind / 9.81f * std::sqrt(9.81f * fetch / (wind * wind));
    const OceanSurface surface = OceanSurface::at(sea, glm::vec3(0.0f), 0.0);
    const float   measured = surface.significantHeight();
    std::printf("      Hs %.2f m against the fetch law's %.2f m\n", measured, expected);
    const bool close = std::fabs(measured - expected) < 0.4f * expected;
    check("a fresh breeze over open water raises JONSWAP's sea", close);

    Ocean stronger = sea;
    stronger.windSpeed = 20.0f;
    const float windier = OceanSurface::at(stronger, glm::vec3(0.0f), 0.0).significantHeight();
    check("  twice the wind raises it more", windier > 1.5f * measured);

    Ocean shallow = sea;
    shallow.depth = 3.0f;
    const float shallower = OceanSurface::at(shallow, glm::vec3(0.0f), 0.0).significantHeight();
    check("  shallow water holds less of it", shallower < 0.8f * measured);

    Ocean calm = sea;
    calm.windSpeed = 0.0f;
    const OceanSurface still = OceanSurface::at(calm, glm::vec3(0.0f), 0.0);
    check("  no wind, no waves", !still.swell && still.displacement({3.0f, 4.0f}) == glm::vec3(0.0f));
}

// Each wave rolls as one wave of its length does, w^2 = g k tanh(k d) - rounded to whole turns a
// loop, so its phase stays exact however long the sea runs: within half a turn a loop, and one at
// the least. Shallow water is where the speed is hard to get right: long waves over a few metres,
// k d far below one.
void testEveryWaveRollsAtItsDepthsSpeed() {
    std::printf("How fast each wave rolls over its depth:\n");

    const double turn  = glm::two_pi<double>() / OceanSpectrum::LOOP;
    double       worst = 0.0;
    uint32_t     waves = 0;
    for (const float depth : {0.0f, 0.5f, 3.0f, 20.0f, 400.0f}) {
        Ocean sea = openSea();
        sea.depth = depth;
        const auto swell = OceanSwell::of(OceanShape::of(sea));
        for (uint32_t c = 0; swell && c < swell->cascadeCount(); ++c) {
            const float step = glm::two_pi<float>() / swell->cascade(c).size;
            for (int row = 0; row < SWELL_FIELD; ++row) {
                for (int col = 0; col < SWELL_FIELD; ++col) {
                    const SwellBin& bin = swell->bins(c)[size_t(row) * SWELL_FIELD + col];
                    if (bin.turns <= 0.0f) continue;
                    const auto      binOf = [](int i) { return i < SWELL_FIELD / 2 ? i : i - SWELL_FIELD; };
                    const glm::vec2 k     = step * glm::vec2(binOf(col), binOf(row));
                    const double    t     = depth > 0.0f ? std::tanh(glm::length(k) * depth) : 1.0;
                    const double    omega = std::sqrt(9.81 * glm::length(k) * t);
                    const double    off   = std::abs(bin.turns * turn - omega) / turn;
                    worst = std::max(worst, omega < 0.5 * turn ? (bin.turns == 1.0f ? 0.0 : off) : off);
                    ++waves;
                }
            }
        }
    }
    std::printf("      %u waves; the worst off its speed by %.3f of a turn a loop\n", waves, worst);
    check("every wave rolls at its depth's speed, to the nearest turn a loop", waves > 0 && worst <= 0.5);
}

// The swell's transform, the CPU's statement of the drawn one, against the waves summed directly:
// a sampled texel of every grid - the carry, its changes, the height's slopes - is the sum of every
// wave of its cascade there, rolled to the moment.
void testTheSwellsTransformIsItsWavesSummed() {
    std::printf("The swell's grids against its waves summed:\n");

    // Choppiness short of one, so each field is seen to take it.
    Ocean sea = openSea();
    sea.choppiness = 0.5f;
    const auto        swell  = OceanSwell::of(OceanShape::of(sea));
    const SwellMoment moment(swell, 0.3172f);
    const auto binOf = [](int i) { return i < SWELL_FIELD / 2 ? i : i - SWELL_FIELD; };
    const double chop = swell->choppiness();
    double worst = 0.0;
    double most  = 0.0;
    for (uint32_t c = 0; c < swell->cascadeCount(); ++c) {
        const double      step = glm::two_pi<double>() / swell->cascade(c).size;
        const SwellBin*   bins = swell->bins(c);
        const SwellTexel* grid = moment.grid(c);
        for (int texel = 0; texel < SWELL_FIELD * SWELL_FIELD; texel += 97) {
            const int    tx = texel % SWELL_FIELD;
            const int    tz = texel / SWELL_FIELD;
            double       sum[8] = {};
            for (int row = 0; row < SWELL_FIELD; ++row) {
                for (int col = 0; col < SWELL_FIELD; ++col) {
                    const SwellBin& ahead = bins[size_t(row) * SWELL_FIELD + col];
                    if (ahead.turns <= 0.0f) continue;
                    const int       mirrorRow = (SWELL_FIELD - row) % SWELL_FIELD;
                    const int       mirrorCol = (SWELL_FIELD - col) % SWELL_FIELD;
                    const SwellBin& behind    = bins[size_t(mirrorRow) * SWELL_FIELD + mirrorCol];
                    const double kx = step * binOf(col);
                    const double kz = step * binOf(row);
                    const double kk = std::sqrt(kx * kx + kz * kz);
                    const double a  = glm::two_pi<double>() * std::fmod(double(ahead.turns) * 0.3172, 1.0);
                    // h = h0 e^-ia + conj(h0') e^ia, then times e^i(k.x).
                    const double hr = ahead.height.x * std::cos(a) + ahead.height.y * std::sin(a)
                        + behind.height.x * std::cos(a) + behind.height.y * std::sin(a);
                    const double hi = ahead.height.y * std::cos(a) - ahead.height.x * std::sin(a)
                        - behind.height.y * std::cos(a) + behind.height.x * std::sin(a);
                    const int    cycles = binOf(col) * tx + binOf(row) * tz;
                    const double phase  = glm::two_pi<double>() * cycles / SWELL_FIELD;
                    const double re = hr * std::cos(phase) - hi * std::sin(phase);
                    const double im = hr * std::sin(phase) + hi * std::cos(phase);
                    // The real part of: h, i k/|k| h (the carry), i k h (slopes), -k k/|k| h (changes).
                    sum[0] += -chop * kx / kk * im;  // carry X
                    sum[1] += re;                    // height
                    sum[2] += -chop * kz / kk * im;  // carry Z
                    sum[3] += -chop * kx * kz / kk * re;
                    sum[4] += -kx * im;
                    sum[5] += -kz * im;
                    sum[6] += -chop * kx * kx / kk * re;
                    sum[7] += -chop * kz * kz / kk * re;
                }
            }
            const SwellTexel& got = grid[texel];
            const float drawn[8] = {
                got.moved.x, got.moved.y, got.moved.z, got.moved.w,
                got.slopes.x, got.slopes.y, got.slopes.z, got.slopes.w,
            };
            for (int i = 0; i < 8; ++i) {
                worst = std::max(worst, std::abs(drawn[i] - sum[i]));
                most  = std::max(most, std::abs(sum[i]));
            }
        }
    }
    std::printf("      every field within %.2e of its waves summed, the largest %.2f\n", worst, most);
    check(
        "the transform is the waves summed, every field of every cascade",
        most > 0.1 && worst < 1e-4 * most
    );
}

// The swell's cascades hold the band from the longest wave down, each from its size to an eighth
// of it, the next a little larger than that so no two repeats line up; past the last, the band is
// the ripples'.
void testTheCascadesHoldTheBand() {
    std::printf("The swell's cascades:\n");

    Ocean sea = openSea();
    sea.minWavelength = 2.0f;
    sea.maxWavelength = 200.0f;
    const auto swell = OceanSwell::of(OceanShape::of(sea));
    bool joined = swell && swell->cascade(0).longest == 200.0f;
    for (uint32_t c = 1; swell && c < swell->cascadeCount(); ++c) {
        const SwellCascade& before = swell->cascade(c - 1);
        const SwellCascade& now    = swell->cascade(c);
        joined = joined && now.longest == before.shortest && now.size > now.longest;
    }
    const SwellCascade& last = swell->cascade(swell->cascadeCount() - 1);
    std::printf(
        "      %u cascades, %.1f to %.2f m\n",
        swell->cascadeCount(),
        double(swell->cascade(0).longest),
        double(last.shortest)
    );
    check("they hold the band end to end", joined && nearly(last.shortest, 2.0f));
    // A short last band in a small tile holds a few dozen waves and shows its repeat; at 16 m this
    // one would fall twice into the cascade before it, so it is a little less.
    const bool resolves = last.size <= 16.0f && last.size > 14.0f;
    check("  the last repeating nearly as far as its shortest wave resolves", resolves);
    const float ratio = swell->cascade(0).size / swell->cascade(1).size;
    check("  no repeat a whole number of another's", std::fabs(ratio - std::round(ratio)) > 0.1f);

    sea.minWavelength = 0.05f;
    check("  a band they cannot reach is cut short", OceanSwell::shortestOf(OceanShape::of(sea)) > 0.05f);
}

// Whatever band the swell is laid over, no cascade's repeat falls a whole number of times into a
// larger one's, so no two line up - the last, sized by its shortest wave, included. The largest
// repeats at maxWavelength, or, for a band that fits one grid, as far as its shortest wave resolves.
void testNoCascadeLinesUpWithAnother() {
    std::printf("The cascades' repeats across bands:\n");

    const float resolves = float(SWELL_FIELD / OceanSwell::SWELL_TEXELS_PER_WAVE);
    std::vector<std::pair<float, float>> bands = {
        {2.0f, 160.0f},
        {1.0f, 128.0f},
        {2.0f, 200.0f},
        {2.0f, 10.0f}
    };
    for (const float shortest : {0.5f, 1.0f, 2.0f, 3.0f}) {
        for (float longest = 1.5f * shortest; longest < 3000.0f; longest *= 1.05f) {
            bands.emplace_back(shortest, longest);
        }
    }

    float nearest  = 1.0f;  // the nearest any two repeats come to a whole ratio
    bool  largest  = true;
    bool  resolved = true;
    for (const auto& [shortest, longest] : bands) {
        Ocean sea = openSea();
        sea.minWavelength = shortest;
        sea.maxWavelength = longest;
        const auto swell = OceanSwell::of(OceanShape::of(sea));
        if (!swell) {
            largest = false;
            continue;
        }
        const uint32_t count = swell->cascadeCount();
        for (uint32_t a = 0; a < count; ++a) {
            for (uint32_t b = a + 1; b < count; ++b) {
                const float times = swell->cascade(a).size / swell->cascade(b).size;
                nearest = std::min(nearest, std::fabs(times - std::round(times)));
            }
        }
        const float top = std::max(longest, resolves * shortest);
        largest = largest && std::fabs(swell->cascade(0).size - top) <= 1e-5f * top;
        const SwellCascade& last = swell->cascade(count - 1);
        if (last.shortest == shortest) resolved = resolved && last.size <= resolves * shortest * 1.00001f;
    }
    std::printf("      %zu bands; the nearest to a whole ratio: %.3f\n", bands.size(), nearest);
    check("no cascade's repeat falls a whole number of times into another's", nearest > 0.099f);
    check("  the largest repeating at maxWavelength, or the one grid's own", largest);
    check("  and the last resolving its shortest wave", resolved);
}

// Every end that builds the sea at the same moment must see the same water: the waves are
// seeded, never drawn from the clock-seeded generator.
void testTheSameSeaIsTheSameEverywhere() {
    std::printf("The same sea twice:\n");

    const Ocean sea = openSea();
    const OceanSurface a = OceanSurface::at(sea, glm::vec3(0.0f), 12.5);
    const OceanSurface b = OceanSurface::at(sea, glm::vec3(0.0f), 12.5);
    const glm::vec2 point = {7.0f, -3.0f};
    check("built twice at one moment, the same water", a.displacement(point) == b.displacement(point));

    Ocean other = sea;
    other.seed = 2;
    const OceanSurface c = OceanSurface::at(other, glm::vec3(0.0f), 12.5);
    check("  another seed, other waves", c.displacement(point) != a.displacement(point));
}

// Phases are wrapped in double: in float, a wave a few seconds long would step by most of a
// radian a frame by the end of a long session.
void testTheSeaStaysSmoothForDays() {
    std::printf("A sea a million seconds in:\n");

    const Ocean      sea   = openSea();
    const glm::vec2  point = {11.0f, 5.0f};
    float            worst = 0.0f;
    for (int frame = 0; frame < 60; ++frame) {
        const double    t    = 1.0e6 + frame / 60.0;
        const glm::vec3 now  = OceanSurface::at(sea, glm::vec3(0.0f), t).displacement(point);
        const glm::vec3 next = OceanSurface::at(sea, glm::vec3(0.0f), t + 1.0 / 60.0).displacement(point);
        worst = std::max(worst, glm::length(next - now));
    }
    std::printf("      the most the water moves in a frame: %.3f m\n", worst);
    check("a frame moves the water a centimetre or two, as it does at the start", worst < 0.03f);
}

// The crests may sharpen but rarely fold over: where they would, the drawn surface would turn
// inside out and every height asked of it would have two answers. Choppiness is held so the
// squeeze passes a fold only past OceanSwell::RARE_SPREADS spreads.
void testAStormRarelyFoldsAndSharpensItsCrests() {
    std::printf("Steep crests:\n");

    Ocean storm = openSea();
    storm.windSpeed  = 25.0f;
    storm.choppiness = 1.0f;
    const OceanSurface surface = OceanSurface::at(storm, glm::vec3(0.0f), 3.0);
    float    least  = 1.0f;
    uint32_t folded = 0;
    uint32_t seen   = 0;
    std::vector<std::pair<float, float>> squeezes;  // Each patch's height, and what of it is kept.
    for (int x = -60; x <= 60; ++x) {
        for (int z = -60; z <= 60; ++z) {
            const glm::vec2 rest = {x * 0.7f, z * 0.7f};
            const glm::vec3 p    = movedPoint(surface, rest);
            const glm::vec3 px   = movedPoint(surface, rest + glm::vec2(0.01f, 0.0f));
            const glm::vec3 pz   = movedPoint(surface, rest + glm::vec2(0.0f, 0.01f));
            // How much a patch of the plane is squeezed on its way to the surface.
            const glm::vec2 dx = glm::vec2(px.x - p.x, px.z - p.z) / 0.01f;
            const glm::vec2 dz = glm::vec2(pz.x - p.x, pz.z - p.z) / 0.01f;
            const float kept = dx.x * dz.y - dx.y * dz.x;
            least = std::min(least, kept);
            folded += kept <= 0.0f ? 1u : 0u;
            ++seen;
            squeezes.emplace_back(p.y, kept);
        }
    }
    // A choppy sea is squeezed at its crests and stretched in its troughs (Tessendorf's sharp
    // peaks and round troughs): the highest patches keep less of themselves than the lowest.
    std::sort(squeezes.begin(), squeezes.end());
    const size_t fiftieth = squeezes.size() / 50;
    float lowest  = 0.0f;
    float highest = 0.0f;
    for (size_t i = 0; i < fiftieth; ++i) {
        lowest  += squeezes[i].second / float(fiftieth);
        highest += squeezes[squeezes.size() - 1 - i].second / float(fiftieth);
    }
    const float share = float(folded) / float(seen);
    std::printf("      the tightest squeeze leaves %.2f of a patch; %.3f%% folded\n", least, share * 100.0f);
    std::printf("      the highest patches keep %.2f of themselves, the lowest %.2f\n", highest, lowest);
    check("a full storm at full choppiness folds a patch in a thousand or fewer", share < 0.001f);
    check("  but it does draw its crests in", least < 0.9f);
    check("  squeezing its crests and stretching its troughs", highest < 1.0f && lowest > 1.0f);
}

// heightAt and normalAt answer for a point fixed in the world, where the water over it came from
// somewhere else: they must agree with the surface the GPU draws, point for point.
void testAWorldPointFindsTheWaterOverIt() {
    std::printf("The water over a point:\n");

    Ocean sea = openSea();
    sea.choppiness = 1.0f;
    sea.surges[0]  = {{5.0f, -3.0f}, glm::normalize(glm::vec2(1.0f, 0.5f)), 1.2f, 2.0f, 10.0f};
    const OceanSurface surface = OceanSurface::at(sea, glm::vec3(0.0f), 7.0);
    float worstHeight = 0.0f;
    float worstNormal = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const glm::vec2 rest  = {std::fmod(i * 7.31f, 80.0f) - 40.0f, std::fmod(i * 3.17f, 80.0f) - 40.0f};
        const glm::vec3 point = movedPoint(surface, rest);
        worstHeight = std::max(worstHeight, std::fabs(surface.heightAt({point.x, point.z}) - point.y));

        // The normal against the surface's own slope, taken across the drawn points.
        const glm::vec2 at  = {point.x, point.z};
        const glm::vec2 dx  = {0.02f, 0.0f};
        const glm::vec2 dz  = {0.0f, 0.02f};
        const float     sx  = (surface.heightAt(at + dx) - surface.heightAt(at - dx)) / 0.04f;
        const float     sz  = (surface.heightAt(at + dz) - surface.heightAt(at - dz)) / 0.04f;
        const glm::vec3 slope = glm::normalize(glm::vec3(-sx, 1.0f, -sz));
        worstNormal = std::max(worstNormal, glm::length(surface.normalAt(at) - slope));
    }
    std::printf("      heights within %.4f m, normals within %.4f\n", worstHeight, worstNormal);
    check("the height over a drawn point is the drawn point's, within a centimetre", worstHeight < 0.01f);
    // The slopes are read between texels as the carry is, so the normal is the surface's slope to
    // within that reading, a degree or two.
    check("  and the normal there is the surface's slope", worstNormal < 0.05f);
    check("  a surge among them too", surface.surges[0].height > 0.0f);

    // A surge lifts the water resting at its crest by its height, over the wind's sea there.
    const glm::vec2 crest = sea.surges[0].centre;
    const float     lift  = surface.displacement(crest).y - surface.withoutSurges().displacement(crest).y;
    std::printf("      the surge's crest stands %.3f m over the sea\n", lift);
    check("a surge raises the water at its crest by its height", std::fabs(lift - 1.2f) < 1e-4f);
    const glm::vec2 past = {40.0f, 40.0f};
    check("  and none past its end", surface.heightAt(past) == surface.withoutSurges().heightAt(past));
}

// A surge is set by gameplay as it runs, its direction however it came: only the way it points
// counts, and one pointing nowhere is none. Where two cross they add, and the cull must reach that.
void testASurgeRollsTheWayItPoints() {
    std::printf("Which way a surge rolls:\n");

    Ocean calm;
    calm.windSpeed = 0.0f;
    calm.surges[0] = {{0.0f, 0.0f}, {0.0f, 1.0f}, 1.0f, 2.0f, 8.0f};
    const glm::vec2 off  = {1.5f, 0.7f};
    const float     unit = OceanSurface::at(calm, glm::vec3(0.0f), 0.0).heightAt(off);
    calm.surges[0].direction = {0.0f, 3.0f};
    const float longer = OceanSurface::at(calm, glm::vec3(0.0f), 0.0).heightAt(off);
    check("a surge's direction is only the way it points", unit > 0.0f && nearly(longer, unit));
    calm.surges[0].direction = glm::vec2(0.0f);
    const OceanSurface nowhere = OceanSurface::at(calm, glm::vec3(0.0f), 0.0);
    check("  and one pointing nowhere raises nothing", nowhere.heightAt(glm::vec2(0.0f)) == 0.0f);

    calm.surges[0] = {{0.0f, 0.0f}, {0.0f, 1.0f}, 1.0f, 2.0f, 8.0f};
    calm.surges[1] = {{0.0f, 0.0f}, {1.0f, 0.0f}, 0.5f, 2.0f, 8.0f};
    const OceanSurface crossed = OceanSurface::at(calm, glm::vec3(0.0f), 0.0);
    const float        top     = crossed.heightAt(glm::vec2(0.0f));
    check("two crossing add", nearly(top, 1.5f));
    check("  and the farthest the water reaches counts both", crossed.farthestReach().y >= top);
}

// Past stillRadius from the ocean's Transform the water lies flat; inside calmRadius it is whole.
void testTheWavesDieAwayFromTheCentre() {
    std::printf("Where the waves reach:\n");

    Ocean sea = openSea();
    sea.calmRadius  = 50.0f;
    sea.stillRadius = 100.0f;
    const OceanSurface surface = OceanSurface::at(sea, glm::vec3(10.0f, 0.0f, 10.0f), 4.0);
    check("past the still radius the sea is flat", surface.displacement({10.0f, 120.0f}) == glm::vec3(0.0f));
    check("  inside the calm radius it is whole", nearly(surface.reach({30.0f, 20.0f}), 1.0f));
    const float between = surface.reach({10.0f, 85.0f});
    check("  and between, it fades", between > 0.0f && between < 1.0f);

    // Where it fades the surface still has one normal: its own slope, the fade's included.
    Ocean edge = openSea();
    edge.choppiness  = 1.0f;
    edge.calmRadius  = 40.0f;
    edge.stillRadius = 60.0f;
    const OceanSurface fading = OceanSurface::at(edge, glm::vec3(0.0f), 4.0);
    float worst = 0.0f;
    for (int i = 0; i < 200; ++i) {
        const float     angle = i * 0.37f;
        const float     out   = 42.0f + std::fmod(i * 1.7f, 16.0f);
        const glm::vec2 rest  = out * glm::vec2(std::cos(angle), std::sin(angle));
        const glm::vec3 point = movedPoint(fading, rest);
        const glm::vec2 at    = {point.x, point.z};
        const glm::vec2 dx    = {0.02f, 0.0f};
        const glm::vec2 dz    = {0.0f, 0.02f};
        const float     sx    = (fading.heightAt(at + dx) - fading.heightAt(at - dx)) / 0.04f;
        const float     sz    = (fading.heightAt(at + dz) - fading.heightAt(at - dz)) / 0.04f;
        const glm::vec3 slope = glm::normalize(glm::vec3(-sx, 1.0f, -sz));
        worst = std::max(worst, glm::length(fading.normalAt(at) - slope));
    }
    std::printf("      across the fade, normals within %.4f of the slope\n", worst);
    check("  where it fades, the normal is still the surface's slope", worst < 0.05f);
}

// The ripples are the spectrum on below the waves, for the light alone, drawn over wavenumber:
// over a band, that density must hold the slope the spectrum gives it, and adding them moves no wave.
void testTheRipplesAreTheSpectrumBelowTheWaves() {
    std::printf("The ripples:\n");

    Ocean sea = openSea();
    sea.minWavelength  = 3.0f;
    sea.shortestRipple = 0.03f;
    const OceanRipples ripples = OceanRipples::at(sea, 5.0);
    const float        swell   = OceanSwell::shortestOf(OceanShape::of(sea));
    check(
        "the band runs from the swell's shortest wave to the shortest ripple",
        ripples.any() && nearly(ripples.longest * swell, glm::two_pi<float>())
            && nearly(ripples.shortest * 0.03f, glm::two_pi<float>())
    );

    // The density's slope, k^2 times it over the band's annulus, against the spectrum's own.
    constexpr int RINGS  = 400;
    constexpr int ANGLES = 720;
    const double  ratio  = double(ripples.shortest) / ripples.longest;
    double        slope  = 0.0;
    for (int i = 0; i < RINGS; ++i) {
        const double from = ripples.longest * std::pow(ratio, double(i) / RINGS);
        const double to   = ripples.longest * std::pow(ratio, double(i + 1) / RINGS);
        const double k    = 0.5 * (from + to);
        for (int j = 0; j < ANGLES; ++j) {
            const double    angle = glm::two_pi<double>() * (j + 0.5) / ANGLES;
            const glm::vec2 at    = glm::vec2(float(k * std::cos(angle)), float(k * std::sin(angle)));
            slope += k * k * ripples.spectrum.density(at) * k * (to - from) * glm::two_pi<double>() / ANGLES;
        }
    }
    const double expected = ripples.spectrum.slopeVariance(ripples.longest, ripples.shortest);
    std::printf("      slope variance %.5f over wavenumber, %.5f by the spectrum\n", slope, expected);
    const bool holds = std::fabs(slope - expected) < 0.02 * expected;
    check("  over wavenumber they hold the slope the spectrum gives", holds);
    const glm::vec2 k = ripples.spectrum.wind * 10.0f;
    const OceanSpectrum& spectrum = ripples.spectrum;
    check("  most of them roll with the wind", spectrum.density(k) > 10.0f * spectrum.density(-k));

    Ocean shorter = sea;
    shorter.shortestRipple = 0.01f;
    const glm::vec2 point = {4.0f, 9.0f};
    const glm::vec3 with    = OceanSurface::at(sea, glm::vec3(0.0f), 5.0).displacement(point);
    const glm::vec3 further = OceanSurface::at(shorter, glm::vec3(0.0f), 5.0).displacement(point);
    check("  more of them moves no wave", with == further);

    Ocean calm = sea;
    calm.windSpeed = 0.0f;
    check("  no wind, no ripples", !OceanRipples::at(calm, 5.0).any());
}

// OceanSystem builds the one sea everything reads: published every frame, an ocean or none, and
// inside a tick at the tick's own time, so what a tick floats on it does not depend on the frame.
void testTheOceanSystemBuildsTheOneSea() {
    std::printf("The scene's one sea:\n");

    Scene scene;
    TestFrame frame(scene);
    frame.clock.setTickRate(60);
    OceanSystem system;
    system.update(frame.ctx);
    check("with no ocean, the frame still says so", frame.ctx.ocean && !frame.ctx.ocean->present);

    const Ocean     sea    = openSea();
    const glm::vec3 centre = {3.0f, -1.0f, 2.0f};
    const EntityId  entity = scene.createEntity();
    scene.add(entity, Transform{centre, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(1.0f)});
    scene.add(entity, sea);
    frame.clock.requestStep(7);
    while (frame.clock.consumeFixedStep()) {}

    frame.ctx.net.beginTick(frame.clock.getTick(), InputCommand{}, 0, frame.ctx.input.actionFingerprint());
    system.fixedUpdate(frame.ctx);
    frame.ctx.net.endTick(scene, frame.clock.getTick());
    const double       tick  = 7.0 * static_cast<double>(frame.clock.getFixedStep());
    const OceanSurface again = OceanSurface::at(sea, centre, tick);
    const glm::vec2    point = {4.0f, -6.0f};
    const OceanData&   built = *frame.ctx.ocean;
    check("an ocean is built where it stands", built.present && built.level == centre.y);
    check(
        "  inside a tick, at that tick's own time",
        built.surface.swell && built.surface.heightAt(point) == again.heightAt(point)
    );

    // Its swell is made once and kept, and made again when what shapes it changes.
    const SwellMoment* first = built.surface.swell.get();
    const OceanSwell*  made  = &first->swell();
    system.update(frame.ctx);
    check("its swell is kept from frame to frame", &frame.ctx.ocean->surface.swell->swell() == made);
    scene.get<Ocean>(entity).seed = 7;
    system.update(frame.ctx);
    check("  and made again when its seed changes", &frame.ctx.ocean->surface.swell->swell() != made);

    // Every field that shapes it remakes it, and one that does not keeps it.
    const auto version = [&] { return frame.ctx.ocean->surface.swell->swell().version(); };
    const std::vector<void (*)(Ocean&)> shapers = {
        [](Ocean& o) { o.windSpeed += 1.0f; },
        [](Ocean& o) { o.windDirection += 10.0f; },
        [](Ocean& o) { o.fetch += 10.0f; },
        [](Ocean& o) { o.depth += 5.0f; },
        [](Ocean& o) { o.spread += 0.1f; },
        [](Ocean& o) { o.choppiness -= 0.1f; },
        [](Ocean& o) { o.heightScale += 0.1f; },
        [](Ocean& o) { o.minWavelength += 0.5f; },
        [](Ocean& o) { o.maxWavelength += 10.0f; },
        [](Ocean& o) { o.seed += 1; },
    };
    bool remade = true;
    for (const auto shape : shapers) {
        const uint64_t before = version();
        shape(scene.get<Ocean>(entity));
        system.update(frame.ctx);
        remade = remade && version() != before;
    }
    check("  as it is when anything else that shapes it does", remade);
    const uint64_t before = version();
    scene.get<Ocean>(entity).foam += 0.1f;
    system.update(frame.ctx);
    check("  but not when what it does not read changes", version() == before);
}

} // namespace

void runOceanTests() {
    testTheSeaIsAsHighAsTheWindMakesIt();
    testEveryWaveRollsAtItsDepthsSpeed();
    testTheSwellsTransformIsItsWavesSummed();
    testTheCascadesHoldTheBand();
    testNoCascadeLinesUpWithAnother();
    testTheSameSeaIsTheSameEverywhere();
    testTheSeaStaysSmoothForDays();
    testAStormRarelyFoldsAndSharpensItsCrests();
    testAWorldPointFindsTheWaterOverIt();
    testASurgeRollsTheWayItPoints();
    testTheWavesDieAwayFromTheCentre();
    testTheRipplesAreTheSpectrumBelowTheWaves();
    testTheOceanSystemBuildsTheOneSea();
}
