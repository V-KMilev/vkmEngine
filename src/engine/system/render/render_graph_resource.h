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
    OITAccum,            ///< Weighted-Blended OIT accumulation (RGBA16F).
    OITRevealage,        ///< Weighted-Blended OIT revealage (R8).
    HiZPyramid,          ///< Max-Z depth pyramid for future occlusion culling.
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
        case RGResource::OITAccum:          return "OITAccum";
        case RGResource::OITRevealage:      return "OITRevealage";
        case RGResource::HiZPyramid:        return "HiZPyramid";
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
        || r == RGResource::PostScratch       // shared scratch, written by callers
        // Overlay is a physical attachment on the HDR FBO; it always exists
        // and is cleared each frame, so "no pass wrote it" is a valid state
        // (composite samples zeros = no overlay visible) rather than an
        // ordering bug. Marks as implicit so disabling the debug passes
        // (e.g. during material preview) does not spam read-before-write
        // warnings.
        || r == RGResource::Overlay;
}

/**
 * @brief Coarse-grained descriptor for a render-graph resource.
 *
 * Drives the alias-group solver: two resources can share physical storage
 * only when their lifetimes are disjoint AND their descriptors match. The
 * fields are intentionally coarse - exact widths/heights depend on the
 * runtime viewport, so we classify by shape (Viewport / HalfViewport /
 * Pyramid / Atlas / Custom) instead of pixels. Format is also coarse-grained
 * by visual channel layout, not exact GL internal format - "RGBA16F" covers
 * any 4-channel half-float, "MixedHDR" is a stand-in for multi-attachment
 * wrappers (G-buffer) that don't reduce to a single attachment.
 *
 * Used by the alias-group analysis to produce a "could physically share"
 * grouping; the visualizer surfaces this and a follow-up pool refactor can
 * consume it directly. Cheap pure function; defined inline.
 */
struct RGResourceDescriptor {
    enum class Shape : uint8_t {
        None,           ///< No physical resource (Backbuffer, externally owned).
        Viewport,       ///< Single image at the current viewport size.
        HalfViewport,   ///< Half resolution (GTAO, future half-res post intermediates).
        Pyramid,        ///< Viewport-sized mip pyramid (BloomChain, HiZPyramid).
        Atlas,          ///< Fixed backend-owned atlas (ShadowAtlas, IBL).
        Tiny,           ///< Sub-pixel-class (1x1 reduction targets, AdaptedLuminance).
    };
    enum class Format : uint8_t {
        None,
        RGBA16F,
        RGBA8,
        R16F,
        R8,
        Depth24Stencil8,
        DepthFloat,
        MixedHDR,       ///< Multi-attachment HDR wrapper (G-buffer normal + position + AO).
    };
    Shape   shape   = Shape::None;
    Format  format  = Format::None;
    uint8_t samples = 0;       ///< 1 = single-sample, 4 = 4x MSAA, 0 = N/A.

    bool operator==(const RGResourceDescriptor& other) const {
        return shape == other.shape && format == other.format && samples == other.samples;
    }
    bool operator!=(const RGResourceDescriptor& other) const { return !(*this == other); }
};

/**
 * @brief Coarse descriptor for each render-graph resource.
 *
 * Used by the alias-group solver to reject lifetime-compatible pairs whose
 * physical shapes disagree (no point grouping an R8 with an RGBA16F even if
 * their lifetimes don't overlap). Values are chosen to match what the
 * OpenGL backend wrappers actually allocate; if a wrapper's shape ever
 * changes, this table needs to track it.
 */
inline RGResourceDescriptor rgResourceDescriptor(RGResource r) {
    using S = RGResourceDescriptor::Shape;
    using F = RGResourceDescriptor::Format;
    switch (r) {
        case RGResource::SceneHDR:          return {S::Viewport,     F::RGBA16F,         4};
        case RGResource::SceneHDRResolved:  return {S::Viewport,     F::RGBA16F,         1};
        case RGResource::PostScratch:       return {S::Viewport,     F::RGBA16F,         1};
        case RGResource::TAAHistory:        return {S::Viewport,     F::RGBA16F,         1};
        case RGResource::OITAccum:          return {S::Viewport,     F::RGBA16F,         1};
        case RGResource::OITRevealage:      return {S::Viewport,     F::R8,              1};
        case RGResource::Overlay:           return {S::Viewport,     F::RGBA8,           4};
        case RGResource::AO:                return {S::HalfViewport, F::R16F,            1};
        case RGResource::BloomChain:        return {S::Pyramid,      F::RGBA16F,         1};
        case RGResource::HiZPyramid:        return {S::Pyramid,      F::DepthFloat,      1};
        case RGResource::GBufferNormal:     return {S::Viewport,     F::MixedHDR,        1};
        case RGResource::GBufferPosition:   return {S::Viewport,     F::MixedHDR,        1};
        case RGResource::AdaptedLuminance:  return {S::Tiny,         F::R16F,            1};
        case RGResource::ShadowAtlas:       return {S::Atlas,        F::Depth24Stencil8, 1};
        case RGResource::IBL:               return {S::Atlas,        F::RGBA16F,         1};
        case RGResource::Backbuffer:        return {S::None,         F::None,            0};
        default:                            return {S::None,         F::None,            0};
    }
}

} // namespace Engine
