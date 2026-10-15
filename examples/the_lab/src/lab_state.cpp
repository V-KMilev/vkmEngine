#include "lab_state.h"

#include <string>

#include "debug/engine_error_log.h"
#include "ecs/component/core/name.h"
#include "ecs/scene.h"
#include "net/replication/silence.h"
#include "net/wire/quantize.h"
#include "net/wire/schema.h"

namespace Lab {

namespace {

// Bits a component of a shot's normal takes: a unit vector needs no more to draw a spark by.
constexpr uint32_t NORMAL_BITS = 10;

void writePoint(const glm::vec3& v, Vkm::Engine::BitWriter& out) {
    Vkm::Engine::Quantize::writePosition(out, v.x);
    Vkm::Engine::Quantize::writePosition(out, v.y);
    Vkm::Engine::Quantize::writePosition(out, v.z);
}

glm::vec3 readPoint(Vkm::Engine::BitReader& in) {
    const float x = Vkm::Engine::Quantize::readPosition(in);
    const float y = Vkm::Engine::Quantize::readPosition(in);
    const float z = Vkm::Engine::Quantize::readPosition(in);
    return {x, y, z};
}

void writeNormal(const glm::vec3& v, Vkm::Engine::BitWriter& out) {
    for (int i = 0; i < 3; ++i) {
        out.bits(Vkm::Engine::Quantize::toSigned(v[i], 1.0f, NORMAL_BITS), NORMAL_BITS);
    }
}

glm::vec3 readNormal(Vkm::Engine::BitReader& in) {
    glm::vec3 v{0.0f};
    for (int i = 0; i < 3; ++i) {
        v[i] = Vkm::Engine::Quantize::fromSigned(in.bits(NORMAL_BITS), 1.0f, NORMAL_BITS);
    }
    return v;
}

} // namespace

bool CountWatch::rose(uint32_t now) {
    const bool rising = m_watched && now > m_seen;
    m_seen    = now;
    m_watched = true;
    return rising;
}

void netEncode(const LabTally& value, Vkm::Engine::BitWriter& out) {
    out.u32(value.count);
}

void netDecode(LabTally& value, Vkm::Engine::BitReader& in) {
    value.count = in.u32();
}

void netEncode(const LabScore& value, Vkm::Engine::BitWriter& out) {
    out.u32(value.shots);
    writePoint(value.lastHit, out);
    writeNormal(value.lastNormal, out);
    out.u8(static_cast<uint8_t>(value.lastShot));
    out.u32(value.tokens);
}

void netDecode(LabScore& value, Vkm::Engine::BitReader& in) {
    value.shots      = in.u32();
    value.lastHit    = readPoint(in);
    value.lastNormal = readNormal(in);
    // From the wire: a kind this build does not know reads as a miss.
    const uint8_t shot = in.u8();
    const bool known = shot <= static_cast<uint8_t>(LabShot::Player);
    value.lastShot = known ? static_cast<LabShot>(shot) : LabShot::Miss;
    value.tokens   = in.u32();
}

void registerLabNetTypes() {
    Vkm::Engine::NetSchema& schema = Vkm::Engine::NetSchema::get();
    schema.replicate<LabTally>("LabTally");
    schema.replicate<LabScore>("LabScore");
}

void reportIfSilenced(Vkm::Engine::Scene& scene, Vkm::Engine::EntityId entity, const char* who) {
    const char* why = Vkm::Engine::toString(Vkm::Engine::netSilence(scene, entity));
    if (!why) return;
    const Vkm::Engine::Name* name = scene.tryGet<Vkm::Engine::Name>(entity);
    Vkm::Engine::reportError(
        "LAB",
        name ? name->value : "an entity",
        std::string(who) + " needs this entity on the wire, and it is not: " + why
    );
}

} // namespace Lab
