#include "system/ocean/ocean_surface.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "core/clock.h"
#include "core/math/random.h"

namespace Vkm::Engine {

namespace {

constexpr double GRAVITY = OceanSurface::GRAVITY;


// The waves' stream of the ocean's seed.
constexpr uint64_t WAVE_STREAM = 0x6f6365616eull;

// Bins of the directional spread's cumulative table, over the whole circle.
constexpr int SPREAD_BINS = 256;

// Radians a second for a wave of @p k radians a metre over @p depth metres (0 deep).
double frequencyOf(double k, double depth) {
    return std::sqrt(GRAVITY * k * (depth > 0.0 ? std::tanh(k * depth) : 1.0));
}

// The wavenumber with frequency @p omega: deep water's, or where shallow, Eckart's estimate
// (within 5%) refined by Newton's steps on g k tanh(kd) - w^2, which double in digits each.
double wavenumberOf(double omega, double depth) {
    const double deep = omega * omega / GRAVITY;
    if (depth <= 0.0) return deep;
    double k = deep / std::sqrt(std::tanh(deep * depth));
    for (int i = 0; i < 4; ++i) {
        const double t = std::tanh(k * depth);
        k -= (GRAVITY * k * t - omega * omega) / (GRAVITY * (t + k * depth * (1.0 - t * t)));
    }
    return k;
}

// How fast frequencyOf grows with @p k: the speed a group of waves that long travels at.
double groupSpeed(double k, double depth) {
    const double omega = frequencyOf(k, depth);
    if (depth <= 0.0) return 0.5 * GRAVITY / omega;
    const double t = std::tanh(k * depth);
    return GRAVITY * (t + k * depth * (1.0 - t * t)) / (2.0 * omega);
}

// JONSWAP's energy density, in square metres per radian a second.
struct Jonswap {
    double peak  = 0.0;  // radians a second
    double scale = 0.0;  // Phillips' alpha times g squared

    // For a wind of @p wind m/s over @p fetch metres.
    static Jonswap of(double wind, double fetch) {
        return {
            22.0 * std::cbrt(GRAVITY * GRAVITY / (wind * fetch)),
            0.076 * std::pow(wind * wind / (fetch * GRAVITY), 0.22) * GRAVITY * GRAVITY,
        };
    }

    double at(double omega) const {
        const double sigma = omega <= peak ? JONSWAP_SIGMA_BELOW : JONSWAP_SIGMA_ABOVE;
        const double r     = std::exp(-(omega - peak) * (omega - peak) / (2.0 * sigma * sigma * peak * peak));
        return scale / std::pow(omega, 5.0) * std::exp(-1.25 * std::pow(peak / omega, 4.0))
             * std::pow(JONSWAP_GAMMA, r);
    }
};

// TMA's share of JONSWAP that water @p depth metres deep holds at @p omega (Kitaigorodskii).
double shallowShare(double omega, double depth) {
    if (depth <= 0.0) return 1.0;
    const double w = omega * std::sqrt(depth / GRAVITY);
    if (w <= 1.0) return 0.5 * w * w;
    if (w < 2.0) return 1.0 - 0.5 * (2.0 - w) * (2.0 - w);
    return 1.0;
}

// Draws angles off the wind from the cos^2s(theta / 2) spread (Longuet-Higgins), by its table.
struct Spread {
    std::array<double, SPREAD_BINS + 1> cumulative{};
    bool narrow = false;  ///< Every wave rolls with the wind.

    explicit Spread(float spread) {
        narrow = spread < 1e-3f;
        if (narrow) return;
        const double s = 2.0 / std::max(double(spread) * double(spread), 1e-4);
        for (int i = 0; i < SPREAD_BINS; ++i) {
            const double theta = glm::pi<double>() * (2.0 * (i + 0.5) / SPREAD_BINS - 1.0);
            cumulative[i + 1] = cumulative[i] + std::pow(std::cos(0.5 * theta), 2.0 * s);
        }
    }

    // The angle at cumulative share @p u, 0..1.
    double angle(double u) const {
        if (narrow) return 0.0;
        const double target = u * cumulative[SPREAD_BINS];
        const auto   upper  = std::upper_bound(cumulative.begin(), cumulative.end(), target);
        const int    bin    = std::clamp(int(upper - cumulative.begin()) - 1, 0, SPREAD_BINS - 1);
        const double width  = cumulative[bin + 1] - cumulative[bin];
        const double within = width > 0.0 ? (target - cumulative[bin]) / width : 0.5;
        return glm::pi<double>() * (2.0 * (bin + within) / SPREAD_BINS - 1.0);
    }
};

// Where the water resting at a point goes, and the partial derivatives of that by rest X and Z:
// the surface's tangents.
struct Moved {
    glm::vec3 offset = glm::vec3(0.0f);
    glm::vec3 alongX = {1.0f, 0.0f, 0.0f};
    glm::vec3 alongZ = {0.0f, 0.0f, 1.0f};
};

// Draws @p count waves of @p ocean's spectrum between @p longest and @p shortest metres into
// @p waves at @p time; returns the sum of their slopes, k a.
// None for a windless ocean or an empty band.
double drawBand(
    const Ocean& ocean,
    double longest,
    double shortest,
    uint32_t count,
    double time,
    GerstnerWave* waves
) {
    const double wind  = ocean.windSpeed;
    const double fetch = double(ocean.fetch) * 1000.0;
    const double depth = std::max(double(ocean.depth), 0.0);
    if (!(wind > 0.0) || !(fetch > 0.0) || !(longest > shortest) || count == 0) return 0.0;

    // Frequency bands spaced evenly in log, one wave a band, each carrying the band's energy:
    // a = sqrt(2 S(w) dw). Seeded jitter inside the band, and a shuffled stratum of the spread,
    // so neighbouring frequencies do not share a direction.
    const double lowest = frequencyOf(glm::two_pi<double>() / longest, depth);
    const double ratio  = frequencyOf(glm::two_pi<double>() / shortest, depth) / lowest;
    Math::Rng    rng(ocean.seed, WAVE_STREAM);
    const Jonswap spectrum = Jonswap::of(wind, fetch);

    std::array<uint32_t, Ocean::MAX_WAVES> stratum{};
    for (uint32_t i = 0; i < count; ++i) stratum[i] = i;
    for (uint32_t i = count; i > 1; --i) std::swap(stratum[i - 1], stratum[rng.nextU32() % i]);

    const Spread spread(ocean.spread);
    const double heading = glm::radians(double(ocean.windDirection));
    double slope = 0.0;
    for (uint32_t i = 0; i < count; ++i) {
        const double from  = lowest * std::pow(ratio, double(i) / count);
        const double to    = lowest * std::pow(ratio, double(i + 1) / count);
        const double omega = from + (to - from) * rng.nextFloat(0.25f, 0.75f);
        const double k     = wavenumberOf(omega, depth);
        const double energy = spectrum.at(omega) * shallowShare(omega, depth) * (to - from);
        const double angle  = heading + spread.angle((stratum[i] + rng.nextFloat()) / count);

        GerstnerWave& wave = waves[i];
        wave.direction  = glm::vec2(float(std::sin(angle)), float(std::cos(angle)));
        wave.wavenumber = float(k);
        wave.frequency  = float(omega);
        wave.amplitude  = float(std::sqrt(2.0 * energy) * ocean.heightScale);
        // The phase as it rolls: k (d . x) - w t, the time part wrapped here in double.
        const double start = rng.nextFloat() * glm::two_pi<double>();
        double       phase = std::fmod(start - omega * time, glm::two_pi<double>());
        if (phase < 0.0) phase += glm::two_pi<double>();
        wave.phase = float(phase);
        slope += k * wave.amplitude;
    }
    return slope;
}

// Each crest draws the water `choppiness` amplitudes toward it (Tessendorf's lambda), held where
// the slopes add past one so the surface never folds over itself.
float steepnessOf(const Ocean& ocean, double slope) {
    return std::clamp(ocean.choppiness, 0.0f, 1.0f) / float(std::max(slope, 1.0));
}

} // namespace

OceanSurface OceanSurface::at(const Ocean& ocean, const glm::vec3& centre, double time) {
    OceanSurface surface;
    // Each surge rolls along its direction made unit; one with none to roll along is none.
    for (uint32_t i = 0; i < Ocean::MAX_SURGES; ++i) {
        const OceanSurge& surge = ocean.surges[i];
        const float       along = glm::length(surge.direction);
        if (!(along > 1e-6f)) continue;
        surface.surges[i]           = surge;
        surface.surges[i].direction = surge.direction / along;
    }
    surface.centre      = {centre.x, centre.z};
    surface.calmRadius  = ocean.calmRadius;
    surface.stillRadius = ocean.stillRadius;

    const double   shortest = std::max(double(ocean.minWavelength), 0.01);
    const uint32_t count    = std::clamp<uint32_t>(ocean.waveCount, 1u, Ocean::MAX_WAVES);
    GerstnerWave*  waves    = surface.waves.data();
    const double   slope    = drawBand(ocean, ocean.maxWavelength, shortest, count, time, waves);
    if (slope <= 0.0) return surface;
    for (uint32_t i = 0; i < count; ++i) surface.waves[i].steepness = steepnessOf(ocean, slope);
    surface.count = count;
    return surface;
}

OceanRipples OceanRipples::at(const Ocean& ocean, double time) {
    OceanRipples ripples;
    const double wind  = ocean.windSpeed;
    const double fetch = double(ocean.fetch) * 1000.0;
    if (!(wind > 0.0) || !(fetch > 0.0)) return ripples;

    const Jonswap spectrum = Jonswap::of(wind, fetch);
    const double  heading  = glm::radians(double(ocean.windDirection));
    // The spread as Spread draws it, scaled by its integral round the circle,
    // 2 sqrt(pi) Gamma(s + 1/2) / Gamma(s + 1).
    const double  power    = 2.0 / std::max(double(ocean.spread) * double(ocean.spread), 1e-4);
    double        into     = std::fmod(time, LOOP);
    if (into < 0.0) into += LOOP;

    ripples.longest     = float(glm::two_pi<double>() / std::max(double(ocean.minWavelength), 0.01));
    ripples.shortest    = float(glm::two_pi<double>() / std::max(double(ocean.shortestRipple), 0.001));
    ripples.peak        = float(spectrum.peak);
    ripples.alpha       = float(spectrum.scale);
    ripples.depth       = std::max(ocean.depth, 0.0f);
    ripples.heightScale = ocean.heightScale;
    ripples.choppiness  = std::clamp(ocean.choppiness, 0.0f, 1.0f);
    ripples.wind        = glm::vec2(float(std::sin(heading)), float(std::cos(heading)));
    ripples.spreadPower = float(power);
    ripples.spreadScale = float(
        std::exp(std::lgamma(power + 1.0) - std::lgamma(power + 0.5)) / (2.0 * std::sqrt(glm::pi<double>()))
    );
    ripples.seed  = ocean.seed;
    ripples.cycle = float(into / LOOP);
    return ripples;
}

float OceanRipples::density(const glm::vec2& k) const {
    const double length = glm::length(k);
    if (!(length > 0.0) || !any()) return 0.0f;
    // A band of frequency and angle holds S(w) D(theta) dw dtheta; over wavenumber, that is
    // S D (dw / dk) / k a square radian a metre.
    const double omega  = frequencyOf(length, depth);
    const double along  = std::max(0.5 + 0.5 * glm::dot(k, wind) / length, 0.0);
    const double spread = spreadScale * std::pow(along, double(spreadPower));
    const double energy = Jonswap{peak, alpha}.at(omega) * shallowShare(omega, depth);
    return float(energy * spread * groupSpeed(length, depth) / length * heightScale * heightScale);
}

float OceanRipples::slopeVariance(float from, float to) const {
    if (!(from > 0.0f) || !(to > from) || !any()) return 0.0f;
    // The slope's spectrum is k^2 S, the spread integrating to one: summed over bands even in log.
    constexpr int STEPS    = 256;
    const Jonswap spectrum = {peak, alpha};
    const double  lowest   = frequencyOf(from, depth);
    const double  ratio    = frequencyOf(to, depth) / lowest;
    double        sum      = 0.0;
    for (int i = 0; i < STEPS; ++i) {
        const double a     = lowest * std::pow(ratio, double(i) / STEPS);
        const double b     = lowest * std::pow(ratio, double(i + 1) / STEPS);
        const double omega = 0.5 * (a + b);
        const double k     = wavenumberOf(omega, depth);
        sum += k * k * spectrum.at(omega) * shallowShare(omega, depth) * (b - a);
    }
    return float(sum * heightScale * heightScale);
}

float OceanSurface::reach(const glm::vec2& rest) const {
    if (stillRadius <= calmRadius) return 1.0f;
    const float r = glm::length(rest - centre);
    const float u = glm::clamp((r - calmRadius) / (stillRadius - calmRadius), 0.0f, 1.0f);
    return 1.0f - u * u * (3.0f - 2.0f * u);
}

namespace {

// A surge's rise at @p rest, and its slopes by rest X and Z: a cosine across its roll, times a
// smooth taper over each end. Stated again in shaders/ocean.glsl (oceanSurgeAt), term for term.
glm::vec3 surgeAt(const OceanSurge& surge, const glm::vec2& rest) {
    if (!(surge.height > 0.0f) || !(surge.halfWidth > 0.0f) || !(surge.halfLength > 0.0f)) {
        return glm::vec3(0.0f);
    }
    const glm::vec2 off  = rest - surge.centre;
    const glm::vec2 side = {-surge.direction.y, surge.direction.x};
    const float     u    = glm::dot(off, surge.direction) / surge.halfWidth;
    const float     v    = glm::dot(off, side) / surge.halfLength;
    if (std::abs(u) >= 1.0f || std::abs(v) >= 1.0f) return glm::vec3(0.0f);
    const float     bump   = 0.5f + 0.5f * std::cos(glm::pi<float>() * u);
    const float     dBump  = -0.5f * glm::pi<float>() * std::sin(glm::pi<float>() * u) / surge.halfWidth;
    const float     tapers = 1.0f - OceanSurface::SURGE_WHOLE;  // the share each end tapers over
    const float     e      = glm::clamp((std::abs(v) - OceanSurface::SURGE_WHOLE) / tapers, 0.0f, 1.0f);
    const float     taper  = 1.0f - e * e * (3.0f - 2.0f * e);
    const float     dE     = (v < 0.0f ? -1.0f : 1.0f) / (tapers * surge.halfLength);
    const float     dTaper = -6.0f * e * (1.0f - e) * dE;
    const glm::vec2 slope  = surge.height * (dBump * taper * surge.direction + bump * dTaper * side);
    return {surge.height * bump * taper, slope.x, slope.y};
}

// Every surge's rise at @p rest, and its slopes.
glm::vec3 surgesAt(const OceanSurface& surface, const glm::vec2& rest) {
    glm::vec3 sum(0.0f);
    for (const OceanSurge& surge : surface.surges) sum += surgeAt(surge, rest);
    return sum;
}

} // namespace

glm::vec3 OceanSurface::displacement(const glm::vec2& rest) const {
    const float fade = reach(rest);
    glm::vec3   moved(0.0f);
    for (uint32_t i = 0; fade > 0.0f && i < count; ++i) {
        const GerstnerWave& w = waves[i];
        const float theta = w.wavenumber * glm::dot(w.direction, rest) + w.phase;
        const float along = w.steepness * w.amplitude * std::cos(theta);
        moved.x += along * w.direction.x;
        moved.z += along * w.direction.y;
        moved.y += w.amplitude * std::sin(theta);
    }
    // A surge only lifts the water: no carry across, so the inverse below steps over the waves alone.
    return moved * fade + glm::vec3(0.0f, surgesAt(*this, rest).x, 0.0f);
}

namespace {

// The rest point the water over world @p point came from.
glm::vec2 restUnder(const OceanSurface& surface, const glm::vec2& point) {
    glm::vec2 rest = point;
    for (uint32_t i = 0; i < OceanSurface::INVERSE_STEPS; ++i) {
        const glm::vec3 moved = surface.displacement(rest);
        rest = point - glm::vec2(moved.x, moved.z);
    }
    return rest;
}

// How fast reach() changes by rest X and Z: nothing but across the band the waves fade over.
glm::vec2 reachSlope(const OceanSurface& surface, const glm::vec2& rest) {
    if (surface.stillRadius <= surface.calmRadius) return glm::vec2(0.0f);
    const glm::vec2 off  = rest - surface.centre;
    const float     r    = glm::length(off);
    const float     band = surface.stillRadius - surface.calmRadius;
    const float     u    = (r - surface.calmRadius) / band;
    if (!(r > 0.0f) || u <= 0.0f || u >= 1.0f) return glm::vec2(0.0f);
    return -6.0f * u * (1.0f - u) / band * off / r;
}

// The waves summed whole, then faded: the fade's own slope tilts the tangents too, where it fades.
Moved movedAt(const OceanSurface& surface, const glm::vec2& rest) {
    const float fade = surface.reach(rest);
    glm::vec3   offset(0.0f);
    glm::vec3   alongX(0.0f);
    glm::vec3   alongZ(0.0f);
    for (uint32_t i = 0; fade > 0.0f && i < surface.count; ++i) {
        const GerstnerWave& w = surface.waves[i];
        const float theta = w.wavenumber * glm::dot(w.direction, rest) + w.phase;
        const float wa    = w.wavenumber * w.amplitude;
        const float s     = std::sin(theta);
        const float c     = std::cos(theta);
        const float d     = w.direction.x * w.direction.y;
        const float across = w.steepness * w.amplitude * c;
        const float pull   = w.steepness * wa * s;
        offset += glm::vec3(across * w.direction.x, w.amplitude * s, across * w.direction.y);
        alongX += glm::vec3(-pull * w.direction.x * w.direction.x, wa * w.direction.x * c, -pull * d);
        alongZ += glm::vec3(-pull * d, wa * w.direction.y * c, -pull * w.direction.y * w.direction.y);
    }
    const glm::vec2 slope = reachSlope(surface, rest);
    const glm::vec3 surge = surgesAt(surface, rest);
    Moved moved;
    moved.offset  = offset * fade + glm::vec3(0.0f, surge.x, 0.0f);
    moved.alongX += alongX * fade + offset * slope.x + glm::vec3(0.0f, surge.y, 0.0f);
    moved.alongZ += alongZ * fade + offset * slope.y + glm::vec3(0.0f, surge.z, 0.0f);
    return moved;
}

} // namespace

float OceanSurface::heightAt(const glm::vec2& point) const {
    return displacement(restUnder(*this, point)).y;
}

glm::vec3 OceanSurface::normalAt(const glm::vec2& point) const {
    return sampleAt(point).normal;
}

OceanSample OceanSurface::sampleAt(const glm::vec2& point) const {
    const Moved moved = movedAt(*this, restUnder(*this, point));
    return {moved.offset.y, glm::normalize(glm::cross(moved.alongZ, moved.alongX))};
}

float OceanSurface::significantHeight() const {
    float m0 = 0.0f;
    for (uint32_t i = 0; i < count; ++i) m0 += 0.5f * waves[i].amplitude * waves[i].amplitude;
    return 4.0f * std::sqrt(m0);
}

glm::vec2 OceanSurface::farthestReach() const {
    glm::vec2 reach(0.0f);
    for (uint32_t i = 0; i < count; ++i) reach += waves[i].amplitude * glm::vec2(waves[i].steepness, 1.0f);
    // Where surges cross, they add.
    float risen = 0.0f;
    for (const OceanSurge& surge : surges) risen += std::max(surge.height, 0.0f);
    return reach + glm::vec2(0.0f, risen);
}

OceanSurface OceanSurface::withoutSurges() const {
    OceanSurface swell = *this;
    swell.surges       = {};
    return swell;
}

double oceanTime(const Clock& clock) {
    return (double(clock.getTick()) + double(clock.getFixedAlpha())) * double(clock.getFixedStep());
}

} // namespace Vkm::Engine
