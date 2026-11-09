#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"

namespace Vkm::Engine {

/**
 * @brief Every display transform the composite pass can end a frame with, once.
 *
 * Columns: enumerator, label (shown, and stored in project.json as
 * `render.tonemap` - renaming one breaks saved projects), shader constant
 * suffix (`TONEMAP_<suffix>`).
 */
#define VKM_TONEMAPS(X)                \
    X(ACES,  "ACES (filmic)",  ACES)    \
    X(Hable, "Hable (filmic)", HABLE)

/**
 * @brief How linear HDR radiance is landed into the display range; a fixed curve.
 *
 * ACES (the default) is Hill's fit of the RRT and ODT: contrast, and highlights that
 * reach white. Hable's filmic curve is gentler: lighter darks, softer color, and white
 * reached sooner.
 */
enum class Tonemap : uint8_t {
#define VKM_TONEMAP_ENUMERATOR(name, label, glsl) name,
    VKM_TONEMAPS(VKM_TONEMAP_ENUMERATOR)
#undef VKM_TONEMAP_ENUMERATOR
    Count,  ///< Enum size marker (reflection); not a selectable curve.
};

/**
 * @brief Editable render tuning: pass toggles + per-effect parameters.
 *
 * Held for the frame by Graphics; borrowed by RenderView::settings each frame. A project ships
 * every field, by its reflect block; the debug view and the grid are the host's (HostChrome::Display).
 */
struct RenderSettings {
    // Readout
    bool showFrameRate = false;  ///< The frame rate read out at the viewport's top right, over the game's UI
                                 ///< (UISystem); a host authoring the world shows its own instead.

    // Pass toggles
    bool gtao  = true;
    bool bloom = true;
    bool ssr   = true;  ///< Screen-space reflections, over the probes and the sky.

    bool ssrHalfResolution = false;  ///< A ray every two by two pixels, gathered back to each by the surfaces
                                     ///< they lie on: the reflected picture keeps its detail, only where a
                                     ///< ray lands is coarser.

    // Display transform
    Tonemap tonemap = Tonemap::ACES;

    float exposure = 1.0f;  ///< The exposure in stops: the frame is scaled by 2^exposure before the tonemap.
                            ///< 0 is as lit. One stop up by default, the daylight balance the procedural sky
                            ///< is built for. With autoExposure it is added to the adapted stops.

    bool  autoExposure    = false;  ///< Adapt the exposure to the frame's brightness, as an eye does
                                    ///< (system/render/exposure.h).
    float exposureMinimum = -4.0f;  ///< The adapted stops' range, before exposure is added: how far it
                                    ///< darkens a bright frame and brightens a dark one.
    float exposureMaximum = 8.0f;

    // Color grading
    std::string gradingLut;              ///< The lookup table the tonemapped frame is graded through: an
                                         ///< image, relative to the project root; empty for none. A strip of
                                         ///< N squares side by side, each N texels on a side (gradingLutSize;
                                         ///< 32 squares are 1024 by 32): red rises to the right and green
                                         ///< upward within a square, blue from square to square. Its texels
                                         ///< are display values, as a screenshot holds them.
    float       gradingStrength = 1.0f;  ///< 0..1: how much of the graded color replaces the frame's.

    // Culling
    bool occlusionCulling = false;  ///< Cull the opaque geometry other geometry hides, on the GPU. Off by
                                    ///< default: it pays only where the GPU bounds the frame and much of the
                                    ///< scene is hidden (docs/reference/decisions.md).

    // Anti-aliasing
    uint32_t msaaSamples = 4;  ///< Scene-pass MSAA samples, one of MSAA_SAMPLE_COUNTS; post runs on the
                               ///< resolved buffer.

    uint32_t textureAnisotropy = 16;  ///< A texture's degree of anisotropic filtering (1 = off), clamped to
                                      ///< the driver's ceiling; one whose TextureParams::filterOverride says
                                      ///< Nearest takes none, and a map of normals or data at most 4x, as a
                                      ///< slant's specular shows less of it than its color.

    // Shadows
    uint32_t shadowResolution = 4096;  ///< A held shadow layer's edge, one of SHADOW_RESOLUTIONS; a tile
                                       ///< takes half at most. Costly to raise.
    bool     softShadows      = true;  ///< A sun with a size (Light::sourceRadius) filters what moves under
                                       ///< it across three texels of its movers' tile; off, one compare, as a
                                       ///< sun of no size does.

    // Resolution
    float renderScale = 1.0f;  ///< The scene's pixels as a share of the viewport's on each axis,
                               ///< MIN_RENDER_SCALE..MAX_RENDER_SCALE. The scene, its effects and their
                               ///< targets render at RenderView::sceneWidth/sceneHeight, and the backend's
                               ///< upscale (edge-adaptive and sharpened, Lottes 2021) widens the tonemapped
                               ///< frame to the viewport, where the UI draws at full resolution.

    static constexpr uint32_t MSAA_SAMPLE_COUNTS[] = {1, 2, 4, 8};        ///< 1 is off.
    static constexpr uint32_t SHADOW_RESOLUTIONS[] = {1024, 2048, 4096};  ///< What shadowResolution may be.

    static constexpr float MIN_RENDER_SCALE = 0.5f;  ///< Below half, the upscale is a blur no tier wants.
    static constexpr float MAX_RENDER_SCALE = 1.0f;  ///< The scene never renders past the viewport's
                                                     ///< resolution.

    static constexpr uint32_t MAX_GRADING_LUT_SIZE = 64;  ///< The most squares a gradingLut strip may hold:
                                                          ///< 4096 by 64 texels.

    /**
     * @brief @p viewportPixels at the scene's scale, renderScale clamped to
     *        MIN_RENDER_SCALE..MAX_RENDER_SCALE.
     *
     * @param viewportPixels A viewport extent.
     * @return The scene's extent; 0 only for 0.
     */
    uint32_t sceneExtent(uint32_t viewportPixels) const {
        const float scale = std::clamp(renderScale, MIN_RENDER_SCALE, MAX_RENDER_SCALE);
        return static_cast<uint32_t>(std::lround(static_cast<float>(viewportPixels) * scale));
    }

    /**
     * @brief Whether @p samples is one of MSAA_SAMPLE_COUNTS.
     *
     * @param samples A requested msaaSamples.
     * @return True when it is allowed.
     */
    static bool isMsaaSampleCount(uint32_t samples) {
        for (const uint32_t count : MSAA_SAMPLE_COUNTS) {
            if (count == samples) return true;
        }
        return false;
    }

    /**
     * @brief How many squares an image of @p width by @p height holds as a gradingLut strip.
     *
     * @param width  The image's width in texels.
     * @param height Its height.
     * @return N, for N squares of N by N texels side by side, 2..MAX_GRADING_LUT_SIZE; 0 for any
     *         other shape.
     */
    static uint32_t gradingLutSize(uint32_t width, uint32_t height) {
        if (height < 2 || height > MAX_GRADING_LUT_SIZE || width != height * height) return 0;
        return height;
    }
};

} // namespace Vkm::Engine

#define VKM_TONEMAP_LABEL(name, label, glsl) label,
VKM_ENUM_NAMES(::Vkm::Engine::Tonemap, VKM_TONEMAPS(VKM_TONEMAP_LABEL))
#undef VKM_TONEMAP_LABEL

VKM_REFLECT_BEGIN(::Vkm::Engine::RenderSettings)
    VKM_F(showFrameRate, "The frames a second at the top right of the game's view; the editor shows its own")
    VKM_F(tonemap, "How the frame's light is landed in the display's range")
    VKM_F(exposure, -8.0f, 8.0f, "Stops before the tonemap: +1 doubles the light, -1 halves it")
    VKM_F(autoExposure, "Adapt the exposure to the frame's brightness, as an eye does")
    VKM_F(exposureMinimum, -12.0f, 4.0f, "The adapted stops' floor, before Exposure is added")
    VKM_F(exposureMaximum, -4.0f, 12.0f, "The adapted stops' ceiling, before Exposure is added")
    VKM_F(gradingLut, "A strip the frame is graded through after the tonemap; Neutral changes nothing")
    VKM_F(gradingStrength, 0.0f, 1.0f, "How much of the graded color replaces the frame's")
    VKM_F(msaaSamples, "Scene-pass MSAA; the post chain runs on the resolved buffer")
    VKM_F(renderScale, RenderSettings::MIN_RENDER_SCALE, RenderSettings::MAX_RENDER_SCALE, "The scene's resolution as a share of the viewport's; the UI stays sharp")
    VKM_F(textureAnisotropy, "Samples along a stretched footprint, so ground keeps its detail far off")
    VKM_F(gtao, "Ground-truth ambient occlusion, applied to the indirect light")
    VKM_F(bloom, "Bright light bleeds into its surroundings")
    VKM_F(ssr, "Screen-space reflections: glossy surfaces reflect what the screen shows")
    VKM_F(ssrHalfResolution, "A reflection ray every two by two pixels: cheaper, coarser where one lands")
    VKM_F(occlusionCulling, "Cull hidden geometry on the GPU; it pays only where the GPU bounds the frame")
    VKM_F(shadowResolution, "A held shadow layer's edge; the largest tile is half of it")
    VKM_F(softShadows, "A sun with a size filters what moves under it; off, one compare")
VKM_REFLECT_END()
