#include "cook/bc4_encoder.h"

#include <algorithm>
#include <cstdint>

namespace Vkm::Engine::AssetCooker {

namespace {

// Compared in 35ths: the palettes interpolate in sevenths and fifths, and 35 is whole in both.
constexpr int64_t SCALE = 35;

// Nearly every block's endpoints have stopped moving by then.
constexpr int REFINE_ROUNDS = 4;

/**
 * @brief One candidate encoding: the endpoints, the index each texel picked, and
 *        the squared error that leaves, in 35ths squared.
 */
struct Fit {
    int      e0 = 0;
    int      e1 = 0;
    uint8_t  indices[16] = {};
    uint64_t error = 0;
};

int64_t divideRounded(int64_t numerator, int64_t denominator) {
    return numerator >= 0
        ? (numerator + denominator / 2) / denominator
        : -((-numerator + denominator / 2) / denominator);
}

// The eight palette entries, in 35ths. e0 > e1 selects the eight-value palette; otherwise four
// entries between them, then exact 0 and 255.
void decodePalette(int e0, int e1, int64_t out[8]) {
    out[0] = e0 * SCALE;
    out[1] = e1 * SCALE;
    if (e0 > e1) {
        for (int k = 2; k < 8; ++k) out[k] = ((8 - k) * e0 + (k - 1) * e1) * (SCALE / 7);
    } else {
        for (int k = 2; k < 6; ++k) out[k] = ((6 - k) * e0 + (k - 1) * e1) * (SCALE / 5);
        out[6] = 0;
        out[7] = 255 * SCALE;
    }
}

// Give every texel the palette entry nearest its value, and say what that costs.
uint64_t assignIndices(const uint8_t values[16], int e0, int e1, uint8_t indices[16]) {
    int64_t palette[8];
    decodePalette(e0, e1, palette);

    uint64_t total = 0;
    for (int i = 0; i < 16; ++i) {
        const int64_t value = values[i] * SCALE;
        uint8_t  best      = 0;
        uint64_t bestError = UINT64_MAX;
        for (uint8_t k = 0; k < 8; ++k) {
            const int64_t  difference = palette[k] - value;
            const uint64_t error      = static_cast<uint64_t>(difference * difference);
            if (error < bestError) {
                bestError = error;
                best      = k;
            }
        }
        indices[i] = best;
        total += bestError;
    }
    return total;
}

// Least-squares endpoints for @p indices: each entry is ((steps - t) * e0 + t * e1) / steps, and
// the normal equations are solved exactly. Texels on the fixed 0 or 255 are left out. False when
// every texel is on one weight.
bool fitEndpoints(const uint8_t values[16], const uint8_t indices[16], int64_t steps, int& e0, int& e1) {
    int64_t aa = 0, ab = 0, bb = 0, av = 0, bv = 0;
    for (int i = 0; i < 16; ++i) {
        const int64_t index = indices[i];
        int64_t t = 0;
        if      (index == 0)     t = 0;
        else if (index == 1)     t = steps;
        else if (index <= steps) t = index - 1;
        else                     continue;

        const int64_t w0 = steps - t;
        aa += w0 * w0;
        ab += w0 * t;
        bb += t * t;
        av += w0 * values[i];
        bv += t * values[i];
    }

    const int64_t determinant = aa * bb - ab * ab;
    if (determinant == 0) return false;
    const int64_t fit0 = divideRounded(steps * (bb * av - ab * bv), determinant);
    const int64_t fit1 = divideRounded(steps * (aa * bv - ab * av), determinant);
    e0 = static_cast<int>(std::clamp<int64_t>(fit0, 0, 255));
    e1 = static_cast<int>(std::clamp<int64_t>(fit1, 0, 255));
    return true;
}

// Refine from @p e0 and @p e1, keeping the endpoint order that selects the @p eight palette.
Fit refine(const uint8_t values[16], int e0, int e1, bool eight) {
    Fit best;
    best.e0    = e0;
    best.e1    = e1;
    best.error = assignIndices(values, e0, e1, best.indices);

    Fit current = best;
    for (int round = 0; round < REFINE_ROUNDS && best.error > 0; ++round) {
        int next0 = 0;
        int next1 = 0;
        if (!fitEndpoints(values, current.indices, eight ? 7 : 5, next0, next1)) break;
        if (eight ? next0 < next1 : next0 > next1) std::swap(next0, next1);
        // Equal endpoints would select the other palette.
        if (eight && next0 == next1) break;
        if (next0 == current.e0 && next1 == current.e1) break;

        current.e0    = next0;
        current.e1    = next1;
        current.error = assignIndices(values, next0, next1, current.indices);
        if (current.error < best.error) best = current;
    }
    return best;
}

void pack(const Fit& fit, uint8_t block[8]) {
    block[0] = static_cast<uint8_t>(fit.e0);
    block[1] = static_cast<uint8_t>(fit.e1);
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= static_cast<uint64_t>(fit.indices[i]) << (3 * i);
    for (int b = 0; b < 6; ++b) block[2 + b] = static_cast<uint8_t>(bits >> (8 * b));
}

} // namespace

void encodeBC4Block(const uint8_t values[16], uint8_t block[8]) {
    const auto [low, high] = std::minmax_element(values, values + 16);

    // A flat block is exact under either palette with both endpoints on it.
    Fit eight = refine(values, *high, *low, *high != *low);

    // The six-value palette has exact 0 and 255, so its endpoints span only what lies between.
    int innerLow  = 255;
    int innerHigh = 0;
    for (int i = 0; i < 16; ++i) {
        if (values[i] == 0 || values[i] == 255) continue;
        innerLow  = std::min<int>(innerLow, values[i]);
        innerHigh = std::max<int>(innerHigh, values[i]);
    }
    if (innerLow > innerHigh) innerLow = innerHigh = 0;
    const Fit six = refine(values, innerLow, innerHigh, false);

    pack(six.error < eight.error ? six : eight, block);
}

void encodeBC5Block(const uint8_t texels[32], uint8_t block[16]) {
    uint8_t red[16];
    uint8_t green[16];
    for (int i = 0; i < 16; ++i) {
        red[i]   = texels[i * 2 + 0];
        green[i] = texels[i * 2 + 1];
    }
    encodeBC4Block(red, block);
    encodeBC4Block(green, block + 8);
}

} // namespace Vkm::Engine::AssetCooker
