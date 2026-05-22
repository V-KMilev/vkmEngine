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
 * Two flavours, distinguished by rgResourceIsPersistent():
 *   - Transient: produced and consumed within a single frame. Lifetime is
 *     (first write -> last read) and ordering errors are real.
 *   - Persistent: ping-pong / history buffers (TAAHistory, AdaptedLuminance)
 *     and externally owned targets (Backbuffer). They survive across frames,
 *     so read-before-write within a frame is not an error - the producer is
 *     "the previous frame".
 *
 * SceneHDRResolved is the single-sample resolve of SceneHDR - it is a derived
 * resource, so reads of it are never flagged as read-before-write either.
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
    AdaptedLuminance,    ///< Persistent ping-pong (eye adaptation history).
    TAAHistory,          ///< Persistent ping-pong (TAA reprojection history).
    PostScratch,         ///< Shared scratch target for in-place post passes (DoF / motion blur).
    Overlay,             ///< HDR FBO overlay attachment; debug passes write, composite reads.
    Backbuffer,          ///< Externally owned (window).

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
        case RGResource::PostScratch:       return "PostScratch";
        case RGResource::Overlay:           return "Overlay";
        case RGResource::Backbuffer:        return "Backbuffer";
        default:                            return "Unknown";
    }
}

/// Persistent across frames: TAA/AE history + externally owned targets.
/// Lifetime tracking does not apply, and read-before-write within a frame is
/// satisfied by the previous frame's write.
inline bool rgResourceIsPersistent(RGResource r) {
    return r == RGResource::TAAHistory
        || r == RGResource::AdaptedLuminance
        || r == RGResource::Backbuffer;
}

/// True for resources whose reads are never flagged as ordering errors:
/// persistent resources, derived resources (SceneHDRResolved), or shared
/// scratch written by callers (PostScratch). All persistent resources are
/// implicit; not every implicit resource is persistent.
inline bool rgResourceIsImplicit(RGResource r) {
    return rgResourceIsPersistent(r)
        || r == RGResource::SceneHDRResolved  // derived from SceneHDR
        || r == RGResource::PostScratch;      // shared scratch, written by callers
}

} // namespace Engine
