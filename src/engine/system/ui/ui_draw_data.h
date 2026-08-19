#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "resource/asset/font_asset.h"

namespace Vkm::Engine {

/**
 * @brief One vertex of the UI's 2D triangle stream.
 *
 * `pos` is in screen pixels (top-left origin); the backend's UI pass maps it to
 * clip space with an orthographic projection. `uv` is the atlas coordinate for
 * image and text commands (ignored by solid fills). `color` is straight
 * (non-premultiplied) RGBA.
 */
struct UIVertex {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;
};

/**
 * @brief What a draw command samples, so the backend picks the matching state.
 */
enum class UIDrawKind : uint8_t {
    Solid = 0,  ///< Flat colour, no texture.
    Text  = 1,  ///< Sample the command's font atlas as an SDF.
};

/**
 * @brief A contiguous run of UI vertices that share draw state.
 *
 * The UISystem appends vertices and emits one command per run; the backend draws
 * each command's [firstVertex, firstVertex + vertexCount) range with the state
 * its kind names. Text commands carry the FontHandle whose SDF atlas the run
 * samples; `font` is empty for Solid fills.
 */
struct UIDrawCmd {
    uint32_t   firstVertex = 0;
    uint32_t   vertexCount = 0;
    FontHandle font        = {};
    UIDrawKind kind        = UIDrawKind::Solid;
};

/**
 * @brief The backend-agnostic UI overlay snapshot for one frame.
 *
 * Built by the UISystem and copied into the RenderView, mirroring how the 3D
 * scene snapshot reaches the backend. The vectors keep their capacity across
 * frames; clear() resets the contents without freeing.
 */
struct UIDrawData {
    std::vector<UIVertex>  vertices;
    std::vector<UIDrawCmd> commands;

    void clear() {
        vertices.clear();
        commands.clear();
    }
};

} // namespace Vkm::Engine
