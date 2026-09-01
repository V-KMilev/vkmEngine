#include "net/wire/codecs.h"

#include <cmath>

#include <glm/gtc/epsilon.hpp>

#include "net/wire/protocol.h"
#include "net/wire/schema.h"
#include "net/wire/quantize.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Below this a linear velocity is carried as zero.
 *
 * A body moving slower than a centimetre a second covers less than a quantisation step
 * of position in the tick the value survives, so the bits describe nothing that is
 * drawn.
 */
constexpr float LINEAR_THRESHOLD = 0.01f;

/**
 * @brief The same argument for spin: a hundredth of a radian a second is a degree every
 * two seconds.
 */
constexpr float ANGULAR_THRESHOLD = 0.01f;

bool isUnitScale(const glm::vec3& scale) {
    return glm::all(glm::epsilonEqual(scale, glm::vec3(1.0f), glm::epsilon<float>()));
}

} // namespace

void netEncode(const Transform& value, BitWriter& out) {
    Quantize::writePosition(out, value.position.x, NET_WORLD_EXTENT);
    Quantize::writePosition(out, value.position.y, NET_WORLD_EXTENT);
    Quantize::writePosition(out, value.position.z, NET_WORLD_EXTENT);
    Quantize::writeRotation(out, value.rotation);

    const bool scaled = !isUnitScale(value.scale);
    out.boolean(scaled);
    if (scaled) {
        out.f32(value.scale.x);
        out.f32(value.scale.y);
        out.f32(value.scale.z);
    }
}

void netDecode(Transform& value, BitReader& in) {
    value.position.x = Quantize::readPosition(in, NET_WORLD_EXTENT);
    value.position.y = Quantize::readPosition(in, NET_WORLD_EXTENT);
    value.position.z = Quantize::readPosition(in, NET_WORLD_EXTENT);
    value.rotation   = Quantize::readRotation(in);

    // Absent means one, not "leave it alone". One component at a time rather
    // than one vec3 construction: argument evaluation order is unspecified, so
    // three reads in one call is a format that differs between builds.
    value.scale = glm::vec3(1.0f);
    if (in.boolean()) {
        const float x = in.f32();
        const float y = in.f32();
        const float z = in.f32();

        // Checked because it is the one field carried raw - see NET_MAX_SCALE.
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
            value.scale = glm::clamp(glm::vec3(x, y, z),
                                     glm::vec3(-NET_MAX_SCALE), glm::vec3(NET_MAX_SCALE));
        }
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

void registerEngineNetTypes() {
    NetSchema& schema = NetSchema::get();
    if (schema.indexOf("Transform") >= 0) return;

    schema.replicate<Transform>("Transform");
    schema.replicate<Rigidbody>("Rigidbody");
    schema.replicate<CharacterController>("CharacterController");
    schema.replicate<Ragdoll>("Ragdoll");
}

} // namespace Vkm::Engine
