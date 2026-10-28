#include "system/ocean/ocean_surface.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Vkm::Engine {

OceanSurface OceanSurface::at(const Ocean& ocean, const glm::vec3& centre, double time) {
    return at(OceanSwell::of(OceanShape::of(ocean)), ocean, centre, time);
}

OceanSurface OceanSurface::at(
    std::shared_ptr<const OceanSwell> swell,
    const Ocean& ocean,
    const glm::vec3& centre,
    double time
) {
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
    surface.time        = time;
    if (swell) surface.swell = std::make_shared<const SwellMoment>(std::move(swell), cycleOf(time));
    return surface;
}

float OceanSurface::cycleOf(double time) {
    double into = std::fmod(time, OceanSpectrum::LOOP);
    if (into < 0.0) into += OceanSpectrum::LOOP;
    return float(into / OceanSpectrum::LOOP);
}

OceanRipples OceanRipples::at(const Ocean& ocean, double time) {
    OceanRipples ripples;
    const OceanShape shape = OceanShape::of(ocean);
    ripples.spectrum = OceanSpectrum::of(shape);
    if (!ripples.spectrum.any()) return ripples;
    ripples.longest    = float(glm::two_pi<double>() / OceanSwell::shortestOf(shape));
    ripples.shortest   = float(glm::two_pi<double>() / std::max(double(ocean.shortestRipple), 0.001));
    ripples.choppiness = std::clamp(ocean.choppiness, 0.0f, 1.0f);
    ripples.seed       = ocean.seed;
    ripples.cycle      = OceanSurface::cycleOf(time);
    return ripples;
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
    const float     fade  = reach(rest);
    const glm::vec3 moved = swell && fade > 0.0f ? swell->offsetAt(rest) : glm::vec3(0.0f);
    // A surge only lifts the water: no carry across, so the inverse below steps over the swell alone.
    return moved * fade + glm::vec3(0.0f, surgesAt(*this, rest).x, 0.0f);
}

namespace {

// Where the water resting at a point goes, and the partial derivatives of that by rest X and Z:
// the surface's tangents.
struct Moved {
    glm::vec3 offset = glm::vec3(0.0f);
    glm::vec3 alongX = {1.0f, 0.0f, 0.0f};
    glm::vec3 alongZ = {0.0f, 0.0f, 1.0f};
};

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

// The swell, then faded: the fade's own slope tilts the tangents too, where it fades.
Moved movedAt(const OceanSurface& surface, const glm::vec2& rest) {
    const float fade = surface.reach(rest);
    glm::vec3   alongX(0.0f);
    glm::vec3   alongZ(0.0f);
    const glm::vec3 offset = surface.swell && fade > 0.0f
        ? surface.swell->movedAt(rest, alongX, alongZ)
        : glm::vec3(0.0f);
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
    return swell ? swell->swell().significantHeight() : 0.0f;
}

glm::vec2 OceanSurface::farthestReach() const {
    glm::vec2 reach = swell ? swell->swell().farthestReach() : glm::vec2(0.0f);
    // Where surges cross, they add.
    float risen = 0.0f;
    for (const OceanSurge& surge : surges) risen += std::max(surge.height, 0.0f);
    return reach + glm::vec2(0.0f, risen);
}

OceanSurface OceanSurface::withoutSurges() const {
    OceanSurface calm = *this;
    calm.surges       = {};
    return calm;
}

} // namespace Vkm::Engine
