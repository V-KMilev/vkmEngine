// What two builds compare before one joins the other: the fingerprint of what they replicate.

#include <cstdint>

#include "net/wire/schema.h"

#include "harness/check.h"

namespace WireSchema {

struct FloatPoints {
    float points = 0.0f;
};

struct IntPoints {
    int32_t points = 0;
};

struct CountPoints {
    uint32_t points = 0;
};

enum class Hue : uint8_t { Red, Green, Count };
enum class HueSwapped : uint8_t { Green, Red, Count };

struct Paint {
    Hue hue = Hue::Red;
};

struct PaintSwapped {
    HueSwapped hue = HueSwapped::Green;
};

} // namespace WireSchema

VKM_REFLECT_BEGIN(::WireSchema::FloatPoints)
    VKM_F(points)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::WireSchema::IntPoints)
    VKM_F(points)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::WireSchema::CountPoints)
    VKM_F(points)
VKM_REFLECT_END()

VKM_ENUM_NAMES(::WireSchema::Hue, "Red", "Green")
VKM_ENUM_NAMES(::WireSchema::HueSwapped, "Green", "Red")

VKM_REFLECT_BEGIN(::WireSchema::Paint)
    VKM_F(hue)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::WireSchema::PaintSwapped)
    VKM_F(hue)
VKM_REFLECT_END()

using namespace Vkm::Engine;
using namespace WireSchema;

namespace {

/// The fingerprint of a schema replicating @p T alone, under one name whatever T is.
template <typename T>
uint32_t fingerprintOf() {
    NetSchema::get().clear();
    NetSchema::get().replicate<T>("Thing");
    return NetSchema::get().fingerprint();
}

} // namespace

TEST("a field that changes its kind or an enum that changes its order is another build to the fingerprint") {
    CHECK(fingerprintOf<FloatPoints>() == fingerprintOf<FloatPoints>());
    CHECK(fingerprintOf<FloatPoints>() != fingerprintOf<IntPoints>());
    CHECK(fingerprintOf<IntPoints>() != fingerprintOf<CountPoints>());
    CHECK(fingerprintOf<Paint>() != fingerprintOf<PaintSwapped>());
}
