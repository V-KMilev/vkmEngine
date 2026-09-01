#include "net/replication/spawn.h"

#include <cmath>

#include "net/wire/protocol.h"
#include "net/wire/quantize.h"

namespace Vkm::Engine {

namespace {

/// Bits for a path's length. Enough for NET_PREFAB_PATH_MAX.
constexpr uint32_t PATH_BITS = 8;

static_assert(NET_PREFAB_PATH_MAX < (1u << PATH_BITS),
              "a path has to be able to say how long it is");

} // namespace

bool writeSpawn(BitWriter& out, const NetSpawn& spawn) {
    // Checked here because the length field is PATH_BITS wide: a longer path
    // would be written whole under a length that wrapped, and everything after
    // it in the stream would be read at the wrong offset. readSpawn already
    // refuses one, so without this the two ends disagree about what is legal.
    if (spawn.prefab.empty() || spawn.prefab.size() > NET_PREFAB_PATH_MAX) return false;

    out.u8(static_cast<uint8_t>(NetEvent::Spawn));
    out.u32(spawn.slot);

    out.bits(static_cast<uint32_t>(spawn.prefab.size()), PATH_BITS);
    for (const char c : spawn.prefab) out.bits(static_cast<uint8_t>(c), 8);

    // Full precision rather than quantised, unlike a snapshot: this is said
    // once and everything after is measured from it, so a millimetre of
    // rounding is one no later packet corrects. Twelve bytes, once.
    out.f32(spawn.at.position.x);
    out.f32(spawn.at.position.y);
    out.f32(spawn.at.position.z);
    Quantize::writeRotation(out, spawn.at.rotation);
    return true;
}

bool readSpawn(BitReader& in, NetSpawn& spawn) {
    spawn.slot = in.u32();
    if (spawn.slot == 0 || spawn.slot > NET_MAX_SLOT) return false;

    const uint32_t length = in.bits(PATH_BITS);
    if (in.failed() || length == 0 || length > NET_PREFAB_PATH_MAX) return false;

    spawn.prefab.resize(length);
    for (uint32_t i = 0; i < length; ++i) {
        spawn.prefab[i] = static_cast<char>(in.bits(8));
    }

    // Into named locals: the order an expression's operands are evaluated in is
    // unspecified, and three reads in one would be a wire format that differs
    // between builds of the same source.
    const float x = in.f32();
    const float y = in.f32();
    const float z = in.f32();
    if (in.failed()) return false;

    // The only raw float on the wire - every other one is quantised into a
    // range by construction and cannot be anything else. This one is whatever
    // the sender put there, and a NaN is written straight into a Transform,
    // where it spreads to every value computed from it and never comes back.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;

    spawn.at.position = glm::vec3(x, y, z);
    spawn.at.rotation = Quantize::readRotation(in);
    spawn.at.scale    = glm::vec3(1.0f);

    return !in.failed();
}

void writeDespawn(BitWriter& out, uint32_t slot) {
    out.u8(static_cast<uint8_t>(NetEvent::Despawn));
    out.u32(slot);
}

} // namespace Vkm::Engine
