#include "net/net_support.h"

#include <algorithm>
#include <iterator>
#include <limits>

#include "core/fnv1a.h"
#include "net/wire/quantize.h"

namespace {

void testBitsAreWrittenAndReadBack() {
    std::printf("Values packed to the bit:\n");

    std::vector<uint8_t> bytes;
    {
        BitWriter out(bytes, 64);
        out.boolean(true);
        out.bits(5u, 3);            // a value that does not fill its width
        out.bits(0x1FFFFu, 17);     // and one that fills it exactly
        out.u16(40000u);
        out.f32(-12.5f);
        out.boolean(false);
        check("nothing overflowed", !out.overflowed());
        check("  and it cost the bits it needed, not the bytes", out.bitCount() == 1 + 3 + 17 + 16 + 32 + 1);
        out.finish();
    }

    BitReader in(bytes.data(), bytes.size());
    check("a flag comes back", in.boolean());
    check("  a narrow field", in.bits(3) == 5u);
    check("  a full one", in.bits(17) == 0x1FFFFu);
    check("  a short", in.u16() == 40000u);
    check("  a float, exactly", in.f32() == -12.5f);
    check("  and the last flag", !in.boolean());
    check("and nothing ran off the end", !in.failed());

    // Reading past the end fails and stays failed: a decoder checks once at the end,
    // not after every field.
    BitReader over(bytes.data(), bytes.size());
    for (int i = 0; i < 200; ++i) (void)over.u32();
    check("reading past the end fails", over.failed());

    // What the packet budget rests on: a writer never writes past what it was given.
    std::vector<uint8_t> small;
    BitWriter tight(small, 4);
    for (int i = 0; i < 100; ++i) tight.u32(0xFFFFFFFFu);
    check("a writer cannot be pushed past its buffer", tight.overflowed());
    check("  and wrote no more than it was given", small.size() == 4);
}

// Every width from one to thirty-two, and spliced runs of odd lengths, so each field
// crosses a byte and a word boundary at some offset. Checked against the bit-at-a-
// time layout the other end reads: bit i of the stream is bit i % 8 of byte i / 8.
void testOddWidthsSurviveEveryBoundary() {
    std::printf("Fields of every width, at every offset:\n");

    struct Field {
        uint32_t value = 0;
        uint32_t width = 0;
    };
    std::vector<Field> fields;
    uint32_t seed = 0x9E3779B9u;
    for (int round = 0; round < 3; ++round) {
        for (uint32_t width = 1; width <= 32; ++width) {
            seed = seed * 1664525u + 1013904223u;
            const uint32_t mask = width == 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
            fields.push_back({seed & mask, width});
        }
    }

    // A run already encoded elsewhere, spliced in at an odd offset.
    const std::vector<uint8_t> spliced = {0xA5u, 0x3Cu, 0xFFu, 0x01u, 0x7Eu};
    constexpr size_t SPLICED_BITS = 37;

    std::vector<uint8_t> bytes;
    BitWriter out(bytes, 1024);
    size_t totalBits = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        out.bits(fields[i].value, fields[i].width);
        totalBits += fields[i].width;
        if (i == 40) {
            out.append(spliced.data(), SPLICED_BITS);
            totalBits += SPLICED_BITS;
        }
    }
    out.finish();
    check("every bit was counted", out.bitCount() == totalBits && !out.overflowed());
    check("  and the buffer holds exactly the bytes they need", bytes.size() == (totalBits + 7) / 8);

    std::vector<uint8_t> expected((totalBits + 7) / 8, 0u);
    size_t at = 0;
    const auto put = [&](uint32_t value, uint32_t width) {
        for (uint32_t b = 0; b < width; ++b, ++at) {
            if ((value >> b) & 1u) expected[at >> 3] |= static_cast<uint8_t>(1u << (at & 7u));
        }
    };
    for (size_t i = 0; i < fields.size(); ++i) {
        put(fields[i].value, fields[i].width);
        if (i == 40) {
            for (size_t b = 0; b < SPLICED_BITS; ++b) put((spliced[b >> 3] >> (b & 7u)) & 1u, 1);
        }
    }
    check("the bytes are the bit-at-a-time layout, exactly", bytes == expected);

    BitReader in(bytes.data(), bytes.size());
    bool same = true;
    for (size_t i = 0; i < fields.size(); ++i) {
        same = same && in.bits(fields[i].width) == fields[i].value;
        if (i == 40) {
            for (size_t b = 0; b < SPLICED_BITS; ++b) {
                same = same && in.bits(1) == ((spliced[b >> 3] >> (b & 7u)) & 1u);
            }
        }
    }
    check("every field reads back as written", same && !in.failed());
    const size_t padding = bytes.size() * 8 - totalBits;
    if (padding > 0) (void)in.bits(static_cast<uint32_t>(padding));
    check("  the padding reads as a field without failing", !in.failed());
    (void)in.bits(1);
    check("  and one bit past the end fails", in.failed());
}

void testQuantisedValuesSurviveTheRoundTrip() {
    std::printf("What a body's state costs on the wire:\n");

    constexpr float EXTENT = Quantize::WORLD_EXTENT;

    std::vector<uint8_t> bytes;
    const glm::vec3 position(123.456f, -7.891f, 0.0f);
    const glm::quat rotation = glm::normalize(glm::quat(0.31f, -0.44f, 0.72f, 0.15f));
    const glm::vec3 velocity(12.34f, -0.05f, 199.0f);

    {
        BitWriter out(bytes, 64);
        for (int i = 0; i < 3; ++i) Quantize::writePosition(out, position[i]);
        Quantize::writeRotation(out, rotation);
        for (int i = 0; i < 3; ++i) Quantize::writeVelocity(out, velocity[i]);
        check("a full body state fits well inside a packet", !out.overflowed());
        out.finish();
    }

    BitReader in(bytes.data(), bytes.size());
    glm::vec3 backPosition;
    for (int i = 0; i < 3; ++i) backPosition[i] = Quantize::readPosition(in);
    const glm::quat backRotation = Quantize::readRotation(in);
    glm::vec3 backVelocity;
    for (int i = 0; i < 3; ++i) backVelocity[i] = Quantize::readVelocity(in);

    // The bound is POSITION_STEP, not twice it: quantize.h promises millimetre
    // resolution, and a looser test would not notice it stop being true.
    check("a position comes back inside a millimetre", glm::length(backPosition - position) < 0.001f);

    // As an angle, not a dot product: a dot of 0.9999 is 2*acos of it, 1.6 degrees -
    // eight times the error this line allows.
    check(
        "a rotation comes back within a fifth of a degree",
        rotationErrorDegrees(backRotation, rotation) < 0.2f
    );

    check("a velocity comes back inside a centimetre a second", glm::length(backVelocity - velocity) < 0.01f);
    check("and the whole read landed", !in.failed());

    // Measured, not restated: a width copied from the encoder is a second source of
    // truth that drifts silently.
    std::vector<uint8_t> oneBody;
    BitWriter measure(oneBody, 256);
    Quantize::writePosition(measure, 1.0f);
    Quantize::writePosition(measure, 2.0f);
    Quantize::writePosition(measure, 3.0f);
    measure.boolean(true);
    measure.boolean(false);
    Quantize::writeRotation(measure, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    Quantize::writeVelocity(measure, 1.0f);
    Quantize::writeVelocity(measure, 2.0f);
    Quantize::writeVelocity(measure, 3.0f);
    const size_t stateBits = measure.bitCount();
    std::printf(
        "      one body: %zu bits (%zu bytes), so %zu fit a 1200-byte packet\n",
        stateBits,
        (stateBits + 7u) / 8u,
        (1200u * 8u) / stateBits
    );
    // The budget as bodies per packet rather than bits per body: that decides whether
    // a scene works.
    check("enough bodies fit one packet to describe a busy world", (1200u * 8u) / stateBits >= 60);

    // Boundaries, where a fixed-point encoder is most likely wrong.
    std::vector<uint8_t> edge;
    BitWriter out(edge, 32);
    Quantize::writePosition(out, -EXTENT);
    Quantize::writePosition(out,  EXTENT);
    Quantize::writePosition(out,  0.0f);
    Quantize::writeVelocity(out,  Quantize::MAX_SPEED * 2.0f);   // past the top
    out.finish();

    BitReader back(edge.data(), edge.size());
    check("the far edge of the world round-trips", std::fabs(Quantize::readPosition(back) + EXTENT) < 0.002f);
    check("  and the other one", std::fabs(Quantize::readPosition(back) - EXTENT) < 0.002f);
    check("  and the origin is the origin", std::fabs(Quantize::readPosition(back)) < 0.002f);
    check(
        "a speed past the top is clamped, not wrapped",
        Quantize::readVelocity(back) > Quantize::MAX_SPEED - 0.02f
    );

    // Past the edge, and off the number line. positionBits sizes the field by
    // doubling, so the top codes decode past +EXTENT, and a NaN is false against both
    // range guards: the width alone catches neither.
    std::vector<uint8_t> wild;
    BitWriter beyond(wild, 32);
    Quantize::writePosition(beyond, EXTENT + 8.0f);
    Quantize::writePosition(beyond, EXTENT * 100.0f);
    Quantize::writePosition(beyond, -EXTENT * 100.0f);
    Quantize::writePosition(beyond, std::numeric_limits<float>::quiet_NaN());
    Quantize::writePosition(beyond, std::numeric_limits<float>::infinity());
    beyond.finish();

    BitReader wide(wild.data(), wild.size());
    check(
        "a coordinate past the edge lands on the edge, not past it",
        std::fabs(Quantize::readPosition(wide) - EXTENT) < 0.002f
    );
    check("  however far past it was", std::fabs(Quantize::readPosition(wide) - EXTENT) < 0.002f);
    check("  and the same on the near side", std::fabs(Quantize::readPosition(wide) + EXTENT) < 0.002f);
    check(
        "a coordinate that is not a number becomes the origin",
        std::fabs(Quantize::readPosition(wide)) < 0.002f
    );
    check("  and neither does infinity reach the corner", std::fabs(Quantize::readPosition(wide)) < 0.002f);
}

void testTheCommonestValuesSurviveExactly() {
    std::printf("What a quantiser owes the value it will be handed most:\n");

    // Identity and rest are most of a scene, so an encoding that cannot say either
    // is wrong about most of the world most of the time.
    std::vector<uint8_t> packet;
    BitWriter writer(packet, 64);
    Quantize::writeRotation(writer, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    Quantize::writeVelocity(writer, 0.0f);
    writer.finish();

    BitReader reader(packet.data(), packet.size());
    const glm::quat back  = Quantize::readRotation(reader);
    const float     still = Quantize::readVelocity(reader);

    check(
        "an unrotated body comes back unrotated, exactly",
        rotationErrorDegrees(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), back) < 1e-4f
    );
    check("and a still one comes back still, exactly", still == 0.0f);

    // A stick is the third: at rest, at either end, and a hair off centre - drift that,
    // read back as deflection, is a player who creeps.
    std::vector<InputCommand> held(1);
    held[0].sequence = 1;
    held[0].tick     = 1;
    held[0].axis[0]  = 0.0f;
    held[0].axis[1]  = 1.0f;
    held[0].axis[2]  = -1.0f;
    held[0].axis[3]  = 0.003f;
    held[0].axis[4]  = -0.003f;
    held[0].axis[5]  = 0.5f;
    std::vector<uint8_t> commandBytes;
    BitWriter commandWriter(commandBytes, 128);
    writeCommands(commandWriter, held, 0, 6);
    commandWriter.finish();
    BitReader commandReader(commandBytes.data(), commandBytes.size());
    std::vector<InputCommand> sticks;
    const bool read = readCommands(commandReader, 6, sticks) && sticks.size() == 1;
    check(
        "a stick at rest, full over and full back come back exactly",
        read && sticks[0].axis[0] == 0.0f && sticks[0].axis[1] == 1.0f && sticks[0].axis[2] == -1.0f
    );
    check(
        "  one a hair off centre comes back centred",
        read && sticks[0].axis[3] == 0.0f && sticks[0].axis[4] == 0.0f
    );
    check(
        "  and one half over within half a step of it",
        read && std::fabs(sticks[0].axis[5] - 0.5f) <= 0.5f / 127.0f
    );

    // A rotation error is an angle, so its cost grows with size: across a 62 m floor a
    // quarter degree drops one end a tenth of a metre, and a character falls through.
    float worst = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const float angle = glm::radians(static_cast<float>(i) * 0.9f);
        const glm::quat q = glm::angleAxis(angle, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));

        std::vector<uint8_t> one;
        BitWriter w(one, 32);
        Quantize::writeRotation(w, q);
        w.finish();
        BitReader r(one.data(), one.size());
        worst = std::max(worst, rotationErrorDegrees(q, Quantize::readRotation(r)));
    }
    check("no rotation is out by more than a tenth of a degree", worst < 0.1f);

    const float acrossTheFloor = 31.0f * std::sin(glm::radians(worst));
    std::printf(
        "      worst rotation error %.4f deg, which is %.1f mm at the far corner of the lab's floor\n",
        static_cast<double>(worst),
        static_cast<double>(acrossTheFloor) * 1000.0
    );
    // Five centimetres: the bound is what a character can stand on, not a round number.
    check("so the far corner of a sixty-metre floor stays inside five centimetres", acrossTheFloor < 0.05f);

    // A body that cannot move is not described at all: the scene file is exact where
    // the wire is not.
    Scene scene;
    const EntityId ground = scene.createEntity();
    scene.add(ground, Transform{});
    Rigidbody stone;
    stone.motion = RigidbodyMotion::Static;
    scene.add(ground, stone);
    check("a static body is not put on the wire", isStaticBody(scene, ground));

    const EntityId crate = scene.createEntity();
    scene.add(crate, Transform{});
    scene.add(crate, Rigidbody{});
    check("while one that can move still is", !isStaticBody(scene, crate));

    // Immovable to the solver and moved by a script, so what it does is news.
    const EntityId platform = scene.createEntity();
    scene.add(platform, Transform{});
    Rigidbody driven;
    driven.motion = RigidbodyMotion::Kinematic;
    scene.add(platform, driven);
    check("  and so is a kinematic one, which a script moves", !isStaticBody(scene, platform));
}

void testAGamesBytesRideACommand() {
    std::printf("A command's payload:\n");
    std::vector<InputCommand> sent(2);
    sent[0].sequence = 7;
    sent[0].tick     = 40;
    sent[1].sequence = 8;
    sent[1].tick     = 41;
    const uint8_t move[] = {3, 12, 28, 0, 255};
    std::copy(std::begin(move), std::end(move), sent[0].payload.begin());
    sent[0].payloadSize = sizeof(move);
    std::vector<uint8_t> bytes;
    BitWriter writer(bytes, 256);
    writeCommands(writer, sent, 0, 4);
    writer.finish();
    BitReader reader(bytes.data(), bytes.size());
    std::vector<InputCommand> got;
    const bool read = readCommands(reader, 4, got) && got.size() == 2;
    const bool same = read && got[0].payloadSize == sizeof(move)
        && std::equal(std::begin(move), std::end(move), got[0].payload.begin());
    check("a command's bytes come back as they went", same);
    check("  and one with none carries none", read && got[1].payloadSize == 0);

    // A length past the most a command holds is a hostile packet, refused.
    std::vector<uint8_t> forged;
    BitWriter liar(forged, 256);
    liar.bits(1, 5);   // one command
    liar.u32(1);
    liar.u32(1);
    for (int a = 0; a < 4; ++a) liar.boolean(false);
    liar.bits(0, 4);
    liar.bits(0, 4);
    Quantize::writeRotation(liar, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    liar.bits(31, 5);  // a payload longer than any command carries
    liar.finish();
    BitReader hostile(forged.data(), forged.size());
    std::vector<InputCommand> refused;
    check("a payload longer than a command holds is refused", !readCommands(hostile, 4, refused));
}

void testTheSchemaCarriesComponentsThroughAScene() {
    std::printf("What a registered component costs, and what survives the trip:\n");

    NetSchema schema;
    schema.replicate<Transform>("Transform");
    schema.replicate<Rigidbody>("Rigidbody");

    check("both types registered", schema.size() == 2);
    check("and are found by name", schema.indexOf("Transform") == 0 && schema.indexOf("Rigidbody") == 1);
    check("a name nobody registered is not found", schema.indexOf("Health") < 0);

    Scene sender;
    const EntityId crate = sender.createEntity();
    Transform placed;
    placed.position = {12.5f, 3.25f, -40.125f};
    placed.rotation = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    sender.add(crate, placed);
    Rigidbody moving;
    moving.linearVelocity  = {2.0f, -9.8f, 0.5f};
    moving.angularVelocity = {0.0f, 1.5f, 0.0f};
    sender.add(crate, moving);

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 256);
    for (const NetType& type : schema.types()) {
        check("the sender has this component to write", type.has(sender, crate));
        type.encode(sender, crate, writer);
    }
    writer.finish();

    // The receiver has the entity and neither component - a client seeing a body it
    // was never told about start moving. Decoding must build them, not refuse.
    Scene receiver;
    const EntityId mirror = receiver.createEntity();
    check("the two worlds agree on the slot", mirror.slot() == crate.slot());
    check(
        "and the receiver starts without the components",
        !receiver.has<Transform>(mirror) && !receiver.has<Rigidbody>(mirror)
    );

    BitReader reader(packet.data(), packet.size());
    for (const NetType& type : schema.types()) {
        check("the receiver takes it", type.decode(receiver, mirror, reader));
    }
    check("and the whole read landed", !reader.failed());

    const Transform& got = receiver.get<Transform>(mirror);
    const Rigidbody& body = receiver.get<Rigidbody>(mirror);
    check("the position arrived inside a millimetre", glm::length(got.position - placed.position) < 0.001f);
    check(
        "the rotation arrived inside a fifth of a degree",
        rotationErrorDegrees(placed.rotation, got.rotation) < 0.2f
    );
    check(
        "an unscaled body spent one bit on its scale, not ninety-six",
        glm::length(got.scale - glm::vec3(1.0f)) < 1e-6f
    );
    check(
        "the velocity arrived inside a centimetre a second",
        glm::length(body.linearVelocity - moving.linearVelocity) < 0.01f
    );

    std::printf("      transform and body together: %zu bytes\n", packet.size());

    // A body at rest is the common case and must be cheap, or a settled tower of a
    // hundred crates costs a packet a tick forever.
    Scene still;
    const EntityId resting = still.createEntity();
    still.add<Rigidbody>(resting, Rigidbody{});
    std::vector<uint8_t> restingPacket;
    BitWriter restingWriter(restingPacket, 64);
    schema.types()[1].encode(still, resting, restingWriter);
    check("a body at rest costs three bits", restingWriter.bitCount() == 3);
}

void testTwoEndsRefuseToPlayDifferentGames() {
    std::printf("What the handshake catches before it becomes a decoded-wrong world:\n");

    NetSchema server, client;
    server.replicate<Transform>("Transform");
    server.replicate<Rigidbody>("Rigidbody");
    client.replicate<Transform>("Transform");
    client.replicate<Rigidbody>("Rigidbody");
    check("two ends built from the same source agree", server.fingerprint() == client.fingerprint());

    // A field's bits depend on more than the list: the quantiser's steps and widths
    // too, and another position step reads every coordinate at another scale.
    NetSchema empty;
    check(
        "the codecs' layout is in the fingerprint, list or no list",
        empty.fingerprint() != FNV1A32_OFFSET_BASIS
    );

    // The same names in the other order: the index is the wire identity, so this must
    // not compare equal, as a fingerprint over an unordered set would.
    NetSchema swapped;
    swapped.replicate<Rigidbody>("Rigidbody");
    swapped.replicate<Transform>("Transform");
    check(
        "the same names registered in a different order do not",
        server.fingerprint() != swapped.fingerprint()
    );

    NetSchema extra;
    extra.replicate<Transform>("Transform");
    extra.replicate<Rigidbody>("Rigidbody");
    extra.replicate<Transform>("Health");
    check(
        "an end with a component the other has not heard of does not",
        server.fingerprint() != extra.fingerprint()
    );

    NetSchema joined, split;
    joined.replicate<Transform>("Transform");
    joined.replicate<Rigidbody>("Body");
    split.replicate<Transform>("TransformBody");
    check(
        "nor does a list whose names run together differently",
        joined.fingerprint() != split.fingerprint()
    );

    check(
        "the description names what is registered",
        server.describe() == std::string("Transform, Rigidbody")
    );
}

void testAnAddressIsReadTheWayAPlayerTypesIt() {
    std::printf("An address, as somebody would write it:\n");

    const NetAddress full = NetAddress::parse("127.0.0.1:27015");
    check("host and port", full && full.ipv4 == 0x7F000001u && full.port == 27015);
    check("  and reads back the same", full.toString() == "127.0.0.1:27015");

    // A player is told a machine, not a number - the port belongs to the game.
    const NetAddress bare = NetAddress::parse("127.0.0.1", 27015);
    check("a bare host takes the game's port", bare && bare.port == 27015);

    const NetAddress named = NetAddress::parse("localhost", 27015);
    check("a name is resolved", named && named.port == 27015);

    // Refused rather than half-read: each names a port the writer meant, and the
    // fallback would connect somewhere they did not ask for.
    check("a trailing colon is not a host", !NetAddress::parse("127.0.0.1:", 27015));
    check("a port that is not a number is refused", !NetAddress::parse("127.0.0.1:abc", 27015));
    check("a port past sixteen bits is refused", !NetAddress::parse("127.0.0.1:70000", 27015));
    check("nothing at all is nowhere", !NetAddress::parse("", 27015));
    check("a host with no port and no fallback is nowhere", !NetAddress::parse("127.0.0.1"));

    // The same rule for a bare port, as vkm_server is told where to listen. A number
    // with something after it is not a shorter number.
    uint16_t port = 0;
    check("a port on its own is read", NetAddress::parsePort("27015", port) && port == 27015);
    port = 0;
    check(
        "  one with anything after the digits is refused",
        !NetAddress::parsePort("27015x", port) && port == 0
    );
    check(
        "  as are zero, a sign and a port past sixteen bits",
        !NetAddress::parsePort("0", port)
            && !NetAddress::parsePort("-1", port)
            && !NetAddress::parsePort("+27015", port)
            && !NetAddress::parsePort("65536", port)
    );
    check("  and nothing at all", !NetAddress::parsePort("", port));
}

} // namespace

void runNetWireTests() {
    testBitsAreWrittenAndReadBack();
    testOddWidthsSurviveEveryBoundary();
    testQuantisedValuesSurviveTheRoundTrip();
    testTheCommonestValuesSurviveExactly();
    testAGamesBytesRideACommand();
    testTheSchemaCarriesComponentsThroughAScene();
    testTwoEndsRefuseToPlayDifferentGames();
    testAnAddressIsReadTheWayAPlayerTypesIt();
}
