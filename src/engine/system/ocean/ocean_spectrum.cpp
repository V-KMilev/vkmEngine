#include "system/ocean/ocean_spectrum.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "ecs/component/render/ocean.h"

namespace Vkm::Engine {

namespace {

constexpr double GRAVITY = OceanSpectrum::GRAVITY;

// TMA's share of JONSWAP that water @p depth metres deep holds at @p omega (Kitaigorodskii).
double shallowShare(double omega, double depth) {
    if (depth <= 0.0) return 1.0;
    const double w = omega * std::sqrt(depth / GRAVITY);
    if (w <= 1.0) return 0.5 * w * w;
    if (w < 2.0) return 1.0 - 0.5 * (2.0 - w) * (2.0 - w);
    return 1.0;
}

} // namespace

OceanShape OceanShape::of(const Ocean& ocean) {
    OceanShape shape;
    shape.windSpeed     = ocean.windSpeed;
    shape.windDirection = ocean.windDirection;
    shape.fetch         = ocean.fetch;
    shape.depth         = ocean.depth;
    shape.spread        = ocean.spread;
    shape.choppiness    = ocean.choppiness;
    shape.heightScale   = ocean.heightScale;
    shape.minWavelength = ocean.minWavelength;
    shape.maxWavelength = ocean.maxWavelength;
    shape.seed          = ocean.seed;
    return shape;
}

bool OceanShape::operator==(const OceanShape& other) const {
    return windSpeed == other.windSpeed && windDirection == other.windDirection && fetch == other.fetch
        && depth == other.depth && spread == other.spread && choppiness == other.choppiness
        && heightScale == other.heightScale && minWavelength == other.minWavelength
        && maxWavelength == other.maxWavelength && seed == other.seed;
}

OceanSpectrum OceanSpectrum::of(const OceanShape& ocean) {
    OceanSpectrum spectrum;
    const double wind  = ocean.windSpeed;
    const double fetch = double(ocean.fetch) * 1000.0;
    if (!(wind > 0.0) || !(fetch > 0.0)) return spectrum;

    // JONSWAP's peak and scale for the wind over the fetch.
    spectrum.peak        = float(22.0 * std::cbrt(GRAVITY * GRAVITY / (wind * fetch)));
    spectrum.alpha       = float(0.076 * std::pow(wind * wind / (fetch * GRAVITY), 0.22) * GRAVITY * GRAVITY);
    spectrum.depth       = std::max(ocean.depth, 0.0f);
    spectrum.heightScale = ocean.heightScale;

    // The spread, scaled by its integral round the circle, 2 sqrt(pi) Gamma(s + 1/2) / Gamma(s + 1).
    const double heading = glm::radians(double(ocean.windDirection));
    const double power   = 2.0 / std::max(double(ocean.spread) * double(ocean.spread), 1e-4);
    spectrum.wind        = glm::vec2(float(std::sin(heading)), float(std::cos(heading)));
    spectrum.spreadPower = float(power);
    spectrum.spreadScale = float(
        std::exp(std::lgamma(power + 1.0) - std::lgamma(power + 0.5)) / (2.0 * std::sqrt(glm::pi<double>()))
    );
    return spectrum;
}

double OceanSpectrum::frequencyOf(double k, double depth) {
    return std::sqrt(GRAVITY * k * (depth > 0.0 ? std::tanh(k * depth) : 1.0));
}

double OceanSpectrum::wavenumberOf(double omega, double depth) {
    // Deep water's, or where shallow, Eckart's estimate (within 5%) refined by Newton's steps on
    // g k tanh(kd) - w^2, which double in digits each.
    const double deep = omega * omega / GRAVITY;
    if (depth <= 0.0) return deep;
    double k = deep / std::sqrt(std::tanh(deep * depth));
    for (int i = 0; i < 4; ++i) {
        const double t = std::tanh(k * depth);
        k -= (GRAVITY * k * t - omega * omega) / (GRAVITY * (t + k * depth * (1.0 - t * t)));
    }
    return k;
}

double OceanSpectrum::groupSpeed(double k, double depth) {
    const double omega = frequencyOf(k, depth);
    if (depth <= 0.0) return 0.5 * GRAVITY / omega;
    const double t = std::tanh(k * depth);
    return GRAVITY * (t + k * depth * (1.0 - t * t)) / (2.0 * omega);
}

double OceanSpectrum::energy(double omega) const {
    const double top     = peak;
    const double sigma   = omega <= top ? JONSWAP_SIGMA_BELOW : JONSWAP_SIGMA_ABOVE;
    const double r       = std::exp(-(omega - top) * (omega - top) / (2.0 * sigma * sigma * top * top));
    const double jonswap = alpha / std::pow(omega, 5.0) * std::exp(-1.25 * std::pow(top / omega, 4.0))
        * std::pow(JONSWAP_GAMMA, r);
    return jonswap * shallowShare(omega, depth);
}

float OceanSpectrum::density(const glm::vec2& k) const {
    const double length = glm::length(k);
    if (!(length > 0.0) || !any()) return 0.0f;
    // A band of frequency and angle holds S(w) D(theta) dw dtheta; over wavenumber, that is
    // S D (dw / dk) / k a square radian a metre.
    const double omega  = frequencyOf(length, depth);
    const double along  = std::max(0.5 + 0.5 * glm::dot(k, wind) / length, 0.0);
    const double spread = spreadScale * std::pow(along, double(spreadPower));
    return float(energy(omega) * spread * groupSpeed(length, depth) / length * heightScale * heightScale);
}

float OceanSpectrum::slopeVariance(float from, float to) const {
    if (!(from > 0.0f) || !(to > from) || !any()) return 0.0f;
    // The slope's spectrum is k^2 S, the spread integrating to one: summed over bands even in log.
    constexpr int STEPS  = 256;
    const double  lowest = frequencyOf(from, depth);
    const double  ratio  = frequencyOf(to, depth) / lowest;
    double        sum    = 0.0;
    for (int i = 0; i < STEPS; ++i) {
        const double a     = lowest * std::pow(ratio, double(i) / STEPS);
        const double b     = lowest * std::pow(ratio, double(i + 1) / STEPS);
        const double omega = 0.5 * (a + b);
        const double k     = wavenumberOf(omega, depth);
        sum += k * k * energy(omega) * (b - a);
    }
    return float(sum * heightScale * heightScale);
}

} // namespace Vkm::Engine
