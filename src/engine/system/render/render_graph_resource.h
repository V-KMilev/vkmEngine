#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Logical transient resources passes read from / write to.
 *
 * These name the data that flows between render passes (not the concrete GPU
 * objects, which the backend still owns for now). The RenderGraph uses the
 * per-pass read/write declarations for ordering validation, lifetime
 * computation (first write -> last read), and debug introspection; a later
 * step moves the concrete allocation into a graph-owned pool keyed by these.
 *
 * SceneHDRResolved is the single-sample resolve of SceneHDR - it is a derived
 * resource, so reads of it are never flagged as read-before-write.
 */
enum class RGResource : uint8_t {
    ShadowAtlas = 0,
    IBL,
    GBufferNormal,
    GBufferPosition,
    AO,
    SceneHDR,
    SceneHDRResolved,
    BloomChain,
    AdaptedLuminance,
    TAAHistory,
    Backbuffer,

    Count
};

constexpr uint32_t RG_RESOURCE_COUNT = static_cast<uint32_t>(RGResource::Count);

inline const char* rgResourceName(RGResource r) {
    switch (r) {
        case RGResource::ShadowAtlas:       return "ShadowAtlas";
        case RGResource::IBL:               return "IBL";
        case RGResource::GBufferNormal:     return "GBufferNormal";
        case RGResource::GBufferPosition:   return "GBufferPosition";
        case RGResource::AO:                return "AO";
        case RGResource::SceneHDR:          return "SceneHDR";
        case RGResource::SceneHDRResolved:  return "SceneHDRResolved";
        case RGResource::BloomChain:        return "BloomChain";
        case RGResource::AdaptedLuminance:  return "AdaptedLuminance";
        case RGResource::TAAHistory:        return "TAAHistory";
        case RGResource::Backbuffer:        return "Backbuffer";
        default:                            return "Unknown";
    }
}

/// True for resources that exist without an explicit producing pass (derived
/// or externally owned), so reading them is not a graph ordering error.
inline bool rgResourceIsImplicit(RGResource r) {
    return r == RGResource::SceneHDRResolved
        || r == RGResource::Backbuffer
        || r == RGResource::TAAHistory;  // persistent ping-pong, no producer pass
}

} // namespace Engine
