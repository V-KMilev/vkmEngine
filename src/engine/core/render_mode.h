#pragma once

#include <cstdint>

#include "base/reflect/enum_names.h"

namespace Vkm::Engine {

/**
 * @brief Every output the composite pass can write, once.
 *
 * Columns: enumerator, editor label, shader constant suffix (`MODE_<suffix>`),
 * and whether the forward pass shades it as radiance (then tonemapped) rather
 * than the composite showing a buffer raw. Albedo, Roughness and Metalness are
 * raw buffers the forward pass writes: the surface as it samples it.
 */
#define VKM_RENDER_MODES(X)                                              \
    X(Lit,              "Lit",                  LIT,               true)  \
    X(Wireframe,        "Wireframe",            WIREFRAME,         true)  \
    X(LightingOnly,     "Lighting Only",        LIGHTING_ONLY,     true)  \
    X(Albedo,           "Albedo",               ALBEDO,            false) \
    X(Roughness,        "Roughness",            ROUGHNESS,         false) \
    X(Metalness,        "Metalness",            METALNESS,         false) \
    X(Normals,          "Normals",              NORMALS,           false) \
    X(Depth,            "Depth",                DEPTH,             false) \
    X(AmbientOcclusion, "Ambient Occlusion",    AMBIENT_OCCLUSION, false) \
    X(GiOnly,           "GI Only",              GI_ONLY,           true)  \
    X(DirectOnly,       "Direct Only",          DIRECT_ONLY,       true)  \
    X(Clusters,         "Light Clusters",       CLUSTERS,          true)  \
    X(Bloom,            "Bloom",                BLOOM,             false) \
    X(ShadowAtlas,      "Shadow Atlas",         SHADOW_ATLAS,      false) \
    X(Fog,              "Fog",                  FOG,               false)

/**
 * @brief What the composite pass writes to the screen.
 *
 * Lit is the final tonemapped image; the rest are debug views.
 */
enum class RenderMode : uint8_t {
#define VKM_RENDER_MODE_ENUMERATOR(name, label, glsl, shaded) name,
    VKM_RENDER_MODES(VKM_RENDER_MODE_ENUMERATOR)
#undef VKM_RENDER_MODE_ENUMERATOR
    Count,  ///< Enum size marker (reflection); not a selectable mode.
};

} // namespace Vkm::Engine

#define VKM_RENDER_MODE_LABEL(name, label, glsl, shaded) label,
VKM_ENUM_NAMES(::Vkm::Engine::RenderMode, VKM_RENDER_MODES(VKM_RENDER_MODE_LABEL))
#undef VKM_RENDER_MODE_LABEL
