#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace Vkm::Engine::PortableMath {

// The C library's sin, cos, acos, atan2 and pow are not correctly rounded, and two platforms'
// libraries differ by a unit in the last place: enough for a cook to write other bytes. These use
// +, -, *, /, floor and sqrt alone, which IEEE arithmetic rounds the same everywhere, in double, so
// every platform rounds the same float. For what a cook computes, not for a frame: they are slower.

namespace Detail {

constexpr double PI      = 3.14159265358979323846;
constexpr double HALF_PI = 1.57079632679489661923;
constexpr double TWO_PI_INVERSE = 0.63661977236758134308;  // 2 / pi
constexpr double PIO2_HI = 1.57079632673412561417e+00;     // pi / 2 to 33 bits: k * PIO2_HI is exact
constexpr double PIO2_LO = 6.07710050650619224932e-11;     // pi / 2 - PIO2_HI
constexpr double SQRT3   = 1.73205080756887729353;
constexpr double TAN_PI_12 = 0.26794919243112270647;      // 2 - sqrt(3)

/// sin(r) for |r| <= pi / 4: its Taylor series to r^19, under 1e-19 off.
inline double sinNear(double r) {
    const double r2 = r * r;
    double sum = 1.0 / 121645100408832000.0;  // 1 / 19!
    for (const double inverse : {-1.0 / 355687428096000.0, 1.0 / 1307674368000.0, -1.0 / 6227020800.0,
                                 1.0 / 39916800.0, -1.0 / 362880.0, 1.0 / 5040.0, -1.0 / 120.0, 1.0 / 6.0}) {
        sum = sum * r2 + inverse;
    }
    // sin r = r - r^3 (1/3! - r^2/5! + ... - r^16/19!).
    return r - r * r2 * sum;
}

/// cos(r) for |r| <= pi / 4: its Taylor series to r^20.
inline double cosNear(double r) {
    const double r2 = r * r;
    double sum = 1.0 / 2432902008176640000.0;  // 1 / 20!
    for (const double inverse : {-1.0 / 6402373705728000.0, 1.0 / 20922789888000.0, -1.0 / 87178291200.0,
                                 1.0 / 479001600.0, -1.0 / 3628800.0, 1.0 / 40320.0, -1.0 / 720.0,
                                 1.0 / 24.0, -1.0 / 2.0}) {
        sum = sum * r2 + inverse;
    }
    return 1.0 + r2 * sum;
}

/// atan(u) for |u| <= 2 - sqrt(3): its series to u^29.
inline double atanNear(double u) {
    const double u2 = u * u;
    double sum = 0.0;
    for (int k = 14; k >= 0; --k) {
        const double term = 1.0 / static_cast<double>(2 * k + 1);
        sum = sum * u2 + ((k & 1) ? -term : term);
    }
    return u * sum;
}

/// atan(t) for 0 <= t <= 1: past tan(pi / 12), pi / 6 plus the atan of what is left.
inline double atanUnit(double t) {
    if (t <= TAN_PI_12) return atanNear(t);
    return PI / 6.0 + atanNear((t * SQRT3 - 1.0) / (SQRT3 + t));
}

/// The quarter turn @p x is nearest, 0 to 3, and what is left of it, in [-pi / 4, pi / 4].
inline double reduce(double x, int& quarter) {
    const double k = std::floor(x * TWO_PI_INVERSE + 0.5);
    quarter = static_cast<int>(static_cast<int64_t>(k) & 3);
    return (x - k * PIO2_HI) - k * PIO2_LO;
}

} // namespace Detail

/// sin(@p x), the same on every platform, for |x| up to about 1e5.
inline double sin(double x) {
    int quarter    = 0;
    const double r = Detail::reduce(x, quarter);
    switch (quarter) {
        case 0:  return Detail::sinNear(r);
        case 1:  return Detail::cosNear(r);
        case 2:  return -Detail::sinNear(r);
        default: return -Detail::cosNear(r);
    }
}

/// cos(@p x), the same on every platform, for |x| up to about 1e5.
inline double cos(double x) {
    int quarter    = 0;
    const double r = Detail::reduce(x, quarter);
    switch (quarter) {
        case 0:  return Detail::cosNear(r);
        case 1:  return -Detail::sinNear(r);
        case 2:  return -Detail::cosNear(r);
        default: return Detail::sinNear(r);
    }
}

/// atan2(@p y, @p x) in [-pi, pi], the same on every platform; 0 for the origin.
inline double atan2(double y, double x) {
    const double ax = std::fabs(x);
    const double ay = std::fabs(y);
    if (ax == 0.0 && ay == 0.0) return 0.0;
    double angle = ay <= ax ? Detail::atanUnit(ay / ax) : Detail::HALF_PI - Detail::atanUnit(ax / ay);
    if (x < 0.0) angle = Detail::PI - angle;
    return y < 0.0 ? -angle : angle;
}

/// acos(@p x) in [0, pi], the same on every platform; @p x is held to [-1, 1].
inline double acos(double x) {
    const double c = x < -1.0 ? -1.0 : (x > 1.0 ? 1.0 : x);
    return atan2(std::sqrt((1.0 - c) * (1.0 + c)), c);
}

/**
 * @brief The @p n th root of @p a, the same on every platform.
 *
 * Newton's method from above, where each step only falls toward the root, stopped at the first
 * step that does not fall.
 *
 * @param a Above 0.
 * @param n At least 1.
 * @return a^(1/n).
 */
inline double root(double a, int n) {
    double y = a > 1.0 ? a : 1.0;
    while (true) {
        double below = 1.0;  // y^(n - 1)
        for (int k = 1; k < n; ++k) below *= y;
        const double next = y - (below * y - a) / (n * below);
        if (!(next < y)) return y;
        y = next;
    }
}

} // namespace Vkm::Engine::PortableMath
