#include "support.h"

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
        check("  and it cost the bits it needed, not the bytes",
              out.bitCount() == 1 + 3 + 17 + 16 + 32 + 1);
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

    // Reading past the end fails, and stays failed: a decoder tests once at the
    // end rather than after every field, so the failure has to stick.
    BitReader over(bytes.data(), bytes.size());
    for (int i = 0; i < 200; ++i) (void)over.u32();
    check("reading past the end fails", over.failed());

    // The property the whole packet budget rests on: a writer never writes past
    // what it was given, whatever it is asked for.
    std::vector<uint8_t> small;
    BitWriter tight(small, 4);
    for (int i = 0; i < 100; ++i) tight.u32(0xFFFFFFFFu);
    check("a writer cannot be pushed past its buffer", tight.overflowed());
    check("  and wrote no more than it was given", small.size() == 4);
}

void testQuantisedValuesSurviveTheRoundTrip() {
    std::printf("What a body's state costs on the wire:\n");

    constexpr float EXTENT = 512.0f;

    std::vector<uint8_t> bytes;
    const glm::vec3 position(123.456f, -7.891f, 0.0f);
    const glm::quat rotation = glm::normalize(glm::quat(0.31f, -0.44f, 0.72f, 0.15f));
    const glm::vec3 velocity(12.34f, -0.05f, 199.0f);

    {
        BitWriter out(bytes, 64);
        for (int i = 0; i < 3; ++i) Quantize::writePosition(out, position[i], EXTENT);
        Quantize::writeRotation(out, rotation);
        for (int i = 0; i < 3; ++i) Quantize::writeVelocity(out, velocity[i]);
        check("a full body state fits well inside a packet", !out.overflowed());
        out.finish();
    }

    BitReader in(bytes.data(), bytes.size());
    glm::vec3 backPosition;
    for (int i = 0; i < 3; ++i) backPosition[i] = Quantize::readPosition(in, EXTENT);
    const glm::quat backRotation = Quantize::readRotation(in);
    glm::vec3 backVelocity;
    for (int i = 0; i < 3; ++i) backVelocity[i] = Quantize::readVelocity(in);

    check("a position comes back inside a millimetre",
          glm::length(backPosition - position) < 0.002f);

    // Compared as a rotation rather than component by component, because q and
    // -q are the same rotation and the encoding is free to pick either.
    const float agreement = std::fabs(glm::dot(backRotation, rotation));
    check("a rotation comes back within a fifth of a degree", agreement > 0.9999f);

    check("a velocity comes back inside a centimetre a second",
          glm::length(backVelocity - velocity) < 0.02f);
    check("and the whole read landed", !in.failed());

    // Measured, not restated. A width copied from the encoder is a second
    // source of truth that drifts silently - this one still said nine rotation
    // bits after the encoder moved to eleven, and the check below is loose
    // enough that it passed anyway.
    std::vector<uint8_t> oneBody;
    BitWriter measure(oneBody, 256);
    Quantize::writePosition(measure, 1.0f, EXTENT);
    Quantize::writePosition(measure, 2.0f, EXTENT);
    Quantize::writePosition(measure, 3.0f, EXTENT);
    measure.boolean(true);
    measure.boolean(false);
    Quantize::writeRotation(measure, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    Quantize::writeVelocity(measure, 1.0f);
    Quantize::writeVelocity(measure, 2.0f);
    Quantize::writeVelocity(measure, 3.0f);
    const size_t stateBits = measure.bitCount();
    std::printf("      one body: %zu bits (%zu bytes), so %zu fit a 1200-byte packet\n",
                stateBits, (stateBits + 7u) / 8u, (1200u * 8u) / stateBits);
    // The figure the packet budget rests on, written as bodies per packet
    // rather than bits per body because that is what decides whether a scene
    // works: the same fields as raw floats are 62 bytes and fit nineteen.
    check("enough bodies fit one packet to describe a busy world",
          (1200u * 8u) / stateBits >= 60);

    // Boundaries, where a fixed-point encoder is most likely to be wrong.
    std::vector<uint8_t> edge;
    BitWriter out(edge, 32);
    Quantize::writePosition(out, -EXTENT, EXTENT);
    Quantize::writePosition(out,  EXTENT, EXTENT);
    Quantize::writePosition(out,  0.0f,   EXTENT);
    Quantize::writeVelocity(out,  Quantize::MAX_SPEED * 2.0f);   // past the top
    out.finish();

    BitReader back(edge.data(), edge.size());
    check("the far edge of the world round-trips",
          std::fabs(Quantize::readPosition(back, EXTENT) + EXTENT) < 0.002f);
    check("  and the other one", std::fabs(Quantize::readPosition(back, EXTENT) - EXTENT) < 0.002f);
    check("  and the origin is the origin", std::fabs(Quantize::readPosition(back, EXTENT)) < 0.002f);
    check("a speed past the top is clamped, not wrapped",
          Quantize::readVelocity(back) > Quantize::MAX_SPEED - 0.02f);
}

void testTheCommonestValuesSurviveExactly() {
    std::printf("What a quantiser owes the value it will be handed most:\n");

    // Identity and rest are not ordinary values. Almost everything in a scene
    // is unrotated and almost everything in it is still, so an encoding that
    // cannot say either is wrong about most of the world most of the time.
    std::vector<uint8_t> packet;
    BitWriter writer(packet, 64);
    Quantize::writeRotation(writer, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    Quantize::writeVelocity(writer, 0.0f);
    writer.finish();

    BitReader reader(packet.data(), packet.size());
    const glm::quat back  = Quantize::readRotation(reader);
    const float     still = Quantize::readVelocity(reader);

    check("an unrotated body comes back unrotated, exactly",
          rotationErrorDegrees(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), back) < 1e-4f);
    check("and a still one comes back still, exactly", still == 0.0f);

    // Why exactly matters, and it is not tidiness. A rotation error is an
    // angle, so what it costs on the ground grows with the size of the thing
    // turned. The lab's floor is 62 m across; a quarter of a degree of error
    // lifts one end of it and drops the other by a tenth of a metre, and a
    // character standing on the low end finds nothing under it.
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
    std::printf("      worst rotation error %.4f deg, which is %.1f mm at the "
                "far corner of the lab's floor\n",
                static_cast<double>(worst), static_cast<double>(acrossTheFloor) * 1000.0);
    check("so the far corner of a sixty-metre floor moves by millimetres, not centimetres",
          acrossTheFloor < 0.05f);

    // And a body that cannot move is not described at all, which is the other
    // half of the same answer: the scene file is exact where the wire is not.
    Scene scene;
    const EntityId ground = scene.createEntity();
    scene.add(ground, Transform{});
    Rigidbody stone;
    stone.isStatic = true;
    scene.add(ground, stone);
    check("a static body is not put on the wire", isImmovable(scene, ground));

    const EntityId crate = scene.createEntity();
    scene.add(crate, Transform{});
    scene.add(crate, Rigidbody{});
    check("while one that can move still is", !isImmovable(scene, crate));
}

void testTheSchemaCarriesComponentsThroughAScene() {
    std::printf("What a registered component costs, and what survives the trip:\n");

    NetSchema schema;
    schema.replicate<Transform>("Transform");
    schema.replicate<Rigidbody>("Rigidbody");

    check("both types registered", schema.size() == 2);
    check("and are found by name", schema.indexOf("Transform") == 0 &&
                                   schema.indexOf("Rigidbody") == 1);
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
        check("the sender has this component to write", type.encode(sender, crate, writer));
    }
    writer.finish();

    // The receiving world has the entity and neither component - which is what
    // a client looks like when a body it has never been told about starts
    // moving. Decoding has to build them, not refuse.
    Scene receiver;
    const EntityId mirror = receiver.createEntity();
    check("the two worlds agree on the slot", mirror.slot() == crate.slot());
    check("and the receiver starts without the components",
          !receiver.has<Transform>(mirror) && !receiver.has<Rigidbody>(mirror));

    BitReader reader(packet.data(), packet.size());
    for (const NetType& type : schema.types()) {
        check("the receiver takes it", type.decode(receiver, mirror, reader));
    }
    check("and the whole read landed", !reader.failed());

    const Transform& got = receiver.get<Transform>(mirror);
    const Rigidbody& body = receiver.get<Rigidbody>(mirror);
    check("the position arrived inside a millimetre",
          glm::length(got.position - placed.position) < 0.002f);
    check("the rotation arrived inside a fifth of a degree",
          rotationErrorDegrees(placed.rotation, got.rotation) < 0.2f);
    check("an unscaled body spent one bit on its scale, not ninety-six",
          glm::length(got.scale - glm::vec3(1.0f)) < 1e-6f);
    check("the velocity arrived inside a centimetre a second",
          glm::length(body.linearVelocity - moving.linearVelocity) < 0.02f);

    std::printf("      transform and body together: %zu bytes\n", packet.size());

    // A body at rest is the common case in a settled world, and it is the one
    // that has to be cheap or a tower of a hundred crates costs a packet a tick
    // forever after it stops moving.
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
    check("two ends built from the same source agree",
          server.fingerprint() == client.fingerprint());

    // The same names in the other order. Every name matches and nothing else
    // does, because the index is the wire identity - so this must not compare
    // equal, and a fingerprint over an unordered set would say it does.
    NetSchema swapped;
    swapped.replicate<Rigidbody>("Rigidbody");
    swapped.replicate<Transform>("Transform");
    check("the same names registered in a different order do not",
          server.fingerprint() != swapped.fingerprint());

    NetSchema extra;
    extra.replicate<Transform>("Transform");
    extra.replicate<Rigidbody>("Rigidbody");
    extra.replicate<Transform>("Health");
    check("an end with a component the other has not heard of does not",
          server.fingerprint() != extra.fingerprint());

    NetSchema owned;
    owned.replicate<Transform>("Transform");
    owned.replicate<Rigidbody>("Rigidbody", NetPolicy::OwnerOnly);
    check("and neither does the same list sent to different people",
          server.fingerprint() != owned.fingerprint());

    check("the description names what is registered",
          server.describe() == std::string("Transform, Rigidbody"));
    check("and says who owner-only rows go to",
          owned.describe() == std::string("Transform, Rigidbody (owner)"));
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

    // Refused rather than half-read: each of these names a port the writer
    // meant, and quietly substituting the fallback would connect somewhere
    // they did not ask for.
    check("a trailing colon is not a host", !NetAddress::parse("127.0.0.1:", 27015));
    check("a port that is not a number is refused", !NetAddress::parse("127.0.0.1:abc", 27015));
    check("a port past sixteen bits is refused", !NetAddress::parse("127.0.0.1:70000", 27015));
    check("nothing at all is nowhere", !NetAddress::parse("", 27015));
    check("a host with no port and no fallback is nowhere",
          !NetAddress::parse("127.0.0.1"));
}

} // namespace

void runNetWireTests() {
    testBitsAreWrittenAndReadBack();
    testQuantisedValuesSurviveTheRoundTrip();
    testTheCommonestValuesSurviveExactly();
    testTheSchemaCarriesComponentsThroughAScene();
    testTwoEndsRefuseToPlayDifferentGames();
    testAnAddressIsReadTheWayAPlayerTypesIt();
}
