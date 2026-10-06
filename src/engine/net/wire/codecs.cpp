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

#include "core/memory/slot_allocator.h"
#include "net/wire/protocol.h"
#include "net/wire/schema.h"
#include "net/wire/quantize.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Below this a linear velocity is carried as zero.
 *
 * Slower covers less than a position quantisation step per tick: nothing drawn.
 */
constexpr float LINEAR_THRESHOLD = 0.01f;

/**
 * @brief Below this an angular velocity is carried as zero.
 */
constexpr float ANGULAR_THRESHOLD = 0.01f;

bool isUnitScale(const glm::vec3& scale) {
    return glm::all(glm::epsilonEqual(scale, glm::vec3(1.0f), glm::epsilon<float>()));
}

/// Bits for a prefab path's length. Enough for NET_PREFAB_PATH_MAX.
constexpr uint32_t PATH_BITS = 8;

static_assert(NET_PREFAB_PATH_MAX < (1u << PATH_BITS), "a path has to be able to say how long it is");
static_assert(PATH_BITS <= 8, "NET_SPAWN_MAX_BYTES counts the length as one byte");

/// Bits for how many slots a spawn names, and for one slot.
constexpr uint32_t SLOT_COUNT_BITS = 6;
constexpr uint32_t SLOT_BITS       = 23;

static_assert(NET_SPAWN_MAX_SLOTS < (1u << SLOT_COUNT_BITS), "a spawn has to be able to say how many slots");
static_assert(
    SlotAllocator::MAX_CLAIMED_INDEX < (1u << SLOT_BITS),
    "a slot has to fit what an allocator claims"
);

/// What a prefab file is named with.
constexpr std::string_view PREFAB_EXTENSION = ".json";

/**
 * @brief Whether a path component names a Windows device rather than a file.
 *
 * In any directory and with any extension: "prefabs/con.json" opens the
 * console on Windows and blocks.
 *
 * @param component One component of a '/'-separated path.
 * @return True for a reserved device name, in any case, with or without an extension.
 */
bool isDeviceName(std::string_view component) {
    const std::string_view stem = component.substr(0, component.find('.'));
    if (stem.size() != 3 && stem.size() != 4) return false;

    std::string lower(stem);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul") return true;
    return lower.size() == 4 && (lower.compare(0, 3, "com") == 0 || lower.compare(0, 3, "lpt") == 0)
        && lower[3] >= '0' && lower[3] <= '9';
}

/**
 * @brief Whether @p path can only name a prefab file inside the project.
 *
 * Judged on characters, not std::filesystem, so platforms agree: Linux would
 * read \\host\share as relative.
 *
 * @param path The path a NetSpawn names.
 * @return True when it is relative, ends in .json and can name nothing outside.
 */
bool isProjectPrefabPath(std::string_view path) {
    if (path.empty() || path.size() > NET_PREFAB_PATH_MAX) return false;
    if (path.size() <= PREFAB_EXTENSION.size()
        || path.substr(path.size() - PREFAB_EXTENSION.size()) != PREFAB_EXTENSION) {
        return false;
    }
    // A root, a drive, a share or a stream; and a character no file name holds.
    if (path.front() == '/') return false;
    for (const char c : path) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\' || c == ':' || byte < 0x20 || byte == 0x7F) return false;
    }
    size_t begin = 0;
    while (begin <= path.size()) {
        const size_t end = std::min(path.find('/', begin), path.size());
        const std::string_view component = path.substr(begin, end - begin);
        if (component == ".." || isDeviceName(component)) return false;
        begin = end + 1;
    }
    return true;
}

} // namespace

glm::vec3 netScale(const glm::vec3& scale) {
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z)) {
        return glm::vec3(1.0f);
    }
    return glm::clamp(scale, glm::vec3(-NET_MAX_SCALE), glm::vec3(NET_MAX_SCALE));
}

void netEncode(const Transform& value, BitWriter& out) {
    Quantize::writePosition(out, value.position.x);
    Quantize::writePosition(out, value.position.y);
    Quantize::writePosition(out, value.position.z);
    Quantize::writeRotation(out, value.rotation);

    const glm::vec3 scale  = netScale(value.scale);
    const bool      scaled = !isUnitScale(scale);
    out.boolean(scaled);
    if (scaled) {
        out.f32(scale.x);
        out.f32(scale.y);
        out.f32(scale.z);
    }
}

void netDecode(Transform& value, BitReader& in) {
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

        // Unquantised, so nothing else bounds it; an honest sender is unchanged.
        value.scale = netScale(glm::vec3(x, y, z));
    }
}

void netEncode(const Rigidbody& value, BitWriter& out) {
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

void netDecode(Rigidbody& value, BitReader& in) {
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

void netEncode(const CharacterController& value, BitWriter& out) {
    out.boolean(value.grounded);
}

void netDecode(CharacterController& value, BitReader& in) {
    value.grounded = in.boolean();
}

void netEncode(const Ragdoll& value, BitWriter& out) {
    out.boolean(value.active);
}

void netDecode(Ragdoll& value, BitReader& in) {
    value.active = in.boolean();
}

bool isSayable(const NetSpawn& spawn) {
    if (!isProjectPrefabPath(spawn.prefab)) return false;

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
    // Unquantised, and a NaN in a Transform spreads to everything.
    for (const float value : values) {
        if (!std::isfinite(value)) return false;
    }
    // Refused, not clamped: the server would build at a scale the client did not.
    if (glm::any(glm::greaterThan(glm::abs(at.scale), glm::vec3(NET_MAX_SCALE)))) return false;

    // Not normalised, so both ends hold the same bits; only zero length is refused.
    if (glm::length(at.rotation) < glm::epsilon<float>()) return false;

    // Each must be a slot an allocator will claim, not the null slot.
    if (spawn.slots.size() > NET_SPAWN_MAX_SLOTS) return false;
    return std::all_of(spawn.slots.begin(), spawn.slots.end(), [](const auto& entry) {
        return entry.second != 0 && entry.second <= SlotAllocator::MAX_CLAIMED_INDEX;
    });
}

void netEncode(const NetSpawn& value, BitWriter& out) {
    // Unsayable goes with no path: a wrapped length would misalign every entry after.
    const bool sayable = isSayable(value);
    out.bits(sayable ? static_cast<uint32_t>(value.prefab.size()) : 0u, PATH_BITS);
    if (sayable) {
        for (const char c : value.prefab) out.bits(static_cast<uint8_t>(c), 8);
    }

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

    out.bits(sayable ? static_cast<uint32_t>(value.slots.size()) : 0u, SLOT_COUNT_BITS);
    if (!sayable) return;
    for (const auto& [uid, slot] : value.slots) {
        out.bits(uid, 32);
        out.bits(slot, SLOT_BITS);
    }
}

void netDecode(NetSpawn& value, BitReader& in) {
    // Every byte is read, refused or not, to stay in step; the width bounds the cost.
    const uint32_t length = in.bits(PATH_BITS);
    std::string prefab(length, '\0');
    for (char& c : prefab) c = static_cast<char>(in.bits(8));

    // One read per statement: operand evaluation order is unspecified.
    float values[10];
    for (float& read : values) read = in.f32();

    std::map<uint32_t, uint32_t> slots;
    const uint32_t count = in.bits(SLOT_COUNT_BITS);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t uid = in.bits(32);
        slots[uid] = in.bits(SLOT_BITS);
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
