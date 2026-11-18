#include "net/wire/codecs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>

#include "base/container/slot_allocator.h"
#include "net/wire/protocol.h"
#include "net/wire/schema.h"
#include "net/wire/quantize.h"

namespace Vkm::Engine {

namespace {

constexpr float LINEAR_THRESHOLD = 0.01f;  ///< Below this a linear velocity is carried as zero. Slower covers
                                           ///< less than a position quantization step per tick: nothing
                                           ///< drawn.

constexpr float ANGULAR_THRESHOLD = 0.01f;  ///< Below this an angular velocity is carried as zero.

bool isUnitScale(const glm::vec3& scale) {
    return glm::all(glm::epsilonEqual(scale, glm::vec3(1.0f), glm::epsilon<float>()));
}

static_assert(
    NET_SPAWN_MAX_SLOTS < (1u << NET_SPAWN_SLOT_COUNT_BITS),
    "a spawn has to be able to say how many slots"
);
static_assert(
    SlotAllocator::MAX_CLAIMED_INDEX < (1u << NET_SPAWN_SLOT_BITS),
    "a slot has to fit what an allocator claims"
);

} // namespace

glm::vec3 netScale(const glm::vec3& scale) {
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z)) {
        return glm::vec3(1.0f);
    }
    return glm::clamp(scale, glm::vec3(-NET_MAX_SCALE), glm::vec3(NET_MAX_SCALE));
}

void netKeep(Transform& held, const Transform& incoming) {
    held.scale = incoming.scale;
}

void netEncode(const Transform& value, Network::BitWriter& out) {
    Quantize::writePosition(out, value.position.x);
    Quantize::writePosition(out, value.position.y);
    Quantize::writePosition(out, value.position.z);
    Quantize::writeRotation(out, value.rotation);

    const glm::vec3 scale = netScale(value.scale);
    const bool scaled     = !isUnitScale(scale);
    out.boolean(scaled);
    if (scaled) {
        out.f32(scale.x);
        out.f32(scale.y);
        out.f32(scale.z);
    }
}

void netDecode(Transform& value, Network::BitReader& in) {
    value.position.x = Quantize::readPosition(in);
    value.position.y = Quantize::readPosition(in);
    value.position.z = Quantize::readPosition(in);
    value.rotation   = Quantize::readRotation(in);

    // Absent means one. One read per statement: argument evaluation order is
    // unspecified.
    value.scale = glm::vec3(1.0f);
    if (in.boolean()) {
        const float x = in.f32();
        const float y = in.f32();
        const float z = in.f32();

        // Unquantized, so nothing else bounds it; an honest sender is unchanged.
        value.scale = netScale(glm::vec3(x, y, z));
    }
}

void netEncode(const Rigidbody& value, Network::BitWriter& out) {
    const bool linear  = glm::length(value.linearVelocity)  > LINEAR_THRESHOLD;
    const bool angular = glm::length(value.angularVelocity) > ANGULAR_THRESHOLD;

    out.boolean(linear);
    if (linear) {
        Quantize::writeVelocity(out, value.linearVelocity.x);
        Quantize::writeVelocity(out, value.linearVelocity.y);
        Quantize::writeVelocity(out, value.linearVelocity.z);
    }
    out.boolean(angular);
    if (angular) {
        Quantize::writeVelocity(out, value.angularVelocity.x);
        Quantize::writeVelocity(out, value.angularVelocity.y);
        Quantize::writeVelocity(out, value.angularVelocity.z);
    }
    out.boolean(value.sleeping);
}

void netDecode(Rigidbody& value, Network::BitReader& in) {
    value.linearVelocity = glm::vec3(0.0f);
    if (in.boolean()) {
        value.linearVelocity.x = Quantize::readVelocity(in);
        value.linearVelocity.y = Quantize::readVelocity(in);
        value.linearVelocity.z = Quantize::readVelocity(in);
    }
    value.angularVelocity = glm::vec3(0.0f);
    if (in.boolean()) {
        value.angularVelocity.x = Quantize::readVelocity(in);
        value.angularVelocity.y = Quantize::readVelocity(in);
        value.angularVelocity.z = Quantize::readVelocity(in);
    }
    value.sleeping = in.boolean();
}

void netEncode(const CharacterController& value, Network::BitWriter& out) {
    out.boolean(value.grounded);
    out.boolean(value.jumping);
    out.boolean(value.crouched);
}

void netDecode(CharacterController& value, Network::BitReader& in) {
    value.grounded = in.boolean();
    value.jumping  = in.boolean();
    value.crouched = in.boolean();
}

void netEncode(const Ragdoll& value, Network::BitWriter& out) {
    out.boolean(value.active);
}

void netDecode(Ragdoll& value, Network::BitReader& in) {
    value.active = in.boolean();
}

bool isSayable(const NetSpawn& spawn) {
    if (!isProjectJsonPath(spawn.prefab)) return false;

    const Transform& at = spawn.at;
    const float values[10] = {
        at.position.x,
        at.position.y,
        at.position.z,
        at.rotation.w,
        at.rotation.x,
        at.rotation.y,
        at.rotation.z,
        at.scale.x,
        at.scale.y,
        at.scale.z
    };
    // Unquantized, and a NaN in a Transform spreads to everything.
    for (const float value : values) {
        if (!std::isfinite(value)) return false;
    }
    // Refused, not clamped: the server would build at a scale the client did not.
    if (glm::any(glm::greaterThan(glm::abs(at.scale), glm::vec3(NET_MAX_SCALE)))) return false;

    // Not normalized, so both ends hold the same bits; only zero length is refused.
    if (glm::length(at.rotation) < glm::epsilon<float>()) return false;

    // Each must be a slot an allocator will claim, not the null slot.
    if (spawn.slots.size() > NET_SPAWN_MAX_SLOTS) return false;
    return std::all_of(spawn.slots.begin(), spawn.slots.end(), [](const auto& entry) {
        return entry.second != 0 && entry.second <= SlotAllocator::MAX_CLAIMED_INDEX;
    });
}

void netEncode(const NetSpawn& value, Network::BitWriter& out) {
    // Unsayable goes with no path, and no slots.
    const bool sayable = isSayable(value);
    writeProjectPath(sayable ? std::string_view(value.prefab) : std::string_view{}, out);

    out.f32(value.at.position.x);
    out.f32(value.at.position.y);
    out.f32(value.at.position.z);
    out.f32(value.at.rotation.w);
    out.f32(value.at.rotation.x);
    out.f32(value.at.rotation.y);
    out.f32(value.at.rotation.z);
    out.f32(value.at.scale.x);
    out.f32(value.at.scale.y);
    out.f32(value.at.scale.z);

    out.bits(sayable ? static_cast<uint32_t>(value.slots.size()) : 0u, NET_SPAWN_SLOT_COUNT_BITS);
    if (!sayable) return;
    for (const auto& [uid, slot] : value.slots) {
        out.bits(uid, 32);
        out.bits(slot, NET_SPAWN_SLOT_BITS);
    }
}

void netDecode(NetSpawn& value, Network::BitReader& in) {
    // Every field is read, refused or not, to stay in step.
    std::string prefab = readProjectPath(in);

    // One read per statement: operand evaluation order is unspecified.
    float values[10];
    for (float& read : values) read = in.f32();

    std::map<uint32_t, uint32_t> slots;
    const uint32_t count = in.bits(NET_SPAWN_SLOT_COUNT_BITS);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t uid = in.bits(32);
        slots[uid] = in.bits(NET_SPAWN_SLOT_BITS);
    }

    NetSpawn said;
    said.prefab      = std::move(prefab);
    said.at.position = glm::vec3(values[0], values[1], values[2]);
    said.at.rotation = glm::quat(values[3], values[4], values[5], values[6]);
    said.at.scale    = glm::vec3(values[7], values[8], values[9]);
    said.slots       = std::move(slots);

    // Refused is empty, which builds nothing.
    value = NetSpawn{};
    if (in.failed() || !isSayable(said)) return;
    value = std::move(said);
}

void registerEngineNetTypes(NetSchema& schema) {
    schema.replicate<Transform>("Transform");
    schema.replicate<Rigidbody>("Rigidbody");
    schema.replicate<CharacterController>("CharacterController");
    schema.replicate<Ragdoll>("Ragdoll");
    schema.replicate<NetSpawn>(NET_SPAWN_TYPE);
}

} // namespace Vkm::Engine
