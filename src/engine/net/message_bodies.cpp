#include "net/message_bodies.h"

#include "net/latency/command_packet.h"
#include "net/latency/pacing.h"
#include "net/wire/project_path.h"

namespace Vkm::Engine {

void HelloBody::write(Network::BitWriter& out) const {
    out.u32(schema);
    out.u8(actions);
    out.u64(actionNames);
    out.u64(world);
    out.u16(tickRate);
}

void HelloBody::read(Network::BitReader& in) {
    schema      = in.u32();
    actions     = in.u8();
    actionNames = in.u64();
    world       = in.u64();
    tickRate    = in.u16();
}

void WelcomeBody::write(Network::BitWriter& out) const {
    out.u16(player);
    out.u32(slot);
    out.u8(worldGeneration);
    out.boolean(current);
}

void WelcomeBody::read(Network::BitReader& in) {
    player          = in.u16();
    slot            = in.u32();
    worldGeneration = in.u8();
    current         = in.boolean();
}

void WorldBody::write(Network::BitWriter& out) const {
    out.u8(generation);
    out.u64(fingerprint);
    out.u32(slot);
    writeProjectPath(path, out);
}

void WorldBody::read(Network::BitReader& in) {
    generation  = in.u8();
    fingerprint = in.u64();
    slot        = in.u32();
    path        = readProjectPath(in);
}

void SnapshotHeader::write(Network::BitWriter& out) const {
    out.u8(worldGeneration);
    out.u32(serverTick);
    out.u32(confirmedTick);
    out.u32(heard);
    out.bits(lowWater, NET_QUEUE_DEPTH_BITS);
    out.u16(messages);
}

void SnapshotHeader::read(Network::BitReader& in) {
    worldGeneration = in.u8();
    serverTick      = in.u32();
    confirmedTick   = in.u32();
    heard           = in.u32();
    lowWater        = in.bits(NET_QUEUE_DEPTH_BITS);
    messages        = in.u16();
}

void CommandPreamble::write(Network::BitWriter& out) const {
    const uint32_t whole = drawnTick > 0.0 ? static_cast<uint32_t>(drawnTick) : 0u;
    const double   past  = drawnTick > 0.0 ? drawnTick - static_cast<double>(whole) : 0.0;
    out.u8(worldGeneration);
    out.u32(whole);
    out.bits(static_cast<uint32_t>(past * NET_DRAWN_FRACTION_MAX), NET_DRAWN_FRACTION_BITS);
}

void CommandPreamble::read(Network::BitReader& in) {
    worldGeneration = in.u8();

    const uint32_t whole    = in.u32();
    const uint32_t fraction = in.bits(NET_DRAWN_FRACTION_BITS);
    drawnTick = static_cast<double>(whole) + static_cast<double>(fraction) / NET_DRAWN_FRACTION_MAX;
}

} // namespace Vkm::Engine
