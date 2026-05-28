#include "resource/asset_id.h"

#include <cstdio>
#include <random>

namespace Engine {

std::string AssetId::toString() const {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016lx", static_cast<unsigned long>(value));
    return std::string(buf);
}

AssetId AssetId::fromString(std::string_view s) {
    if (s.size() != 16) return {};
    AssetId out;
    for (char c : s) {
        out.value <<= 4;
        if      (c >= '0' && c <= '9') out.value |= static_cast<uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') out.value |= static_cast<uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') out.value |= static_cast<uint64_t>(c - 'A' + 10);
        else return {};
    }
    return out;
}

AssetId AssetId::generate() {
    // Process-thread RNG seeded from random_device once. Each call returns
    // the next 64-bit pull; collision odds with 64-bit width and the
    // handful of assets in a typical scene are astronomical.
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    uint64_t v = 0;
    while (v == 0) v = rng();   // Reject the 0 sentinel.
    return AssetId{v};
}

} // namespace Engine
