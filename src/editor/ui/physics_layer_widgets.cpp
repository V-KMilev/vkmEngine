#include "ui/physics_layer_widgets.h"

#include <algorithm>

#include <imgui.h>

#include "io/project/project.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

std::string physicsLayerName(const std::vector<std::string>& names, int bit) {
    if (bit >= 0 && static_cast<size_t>(bit) < names.size() && !names[bit].empty()) return names[bit];
    return "Layer " + std::to_string(bit + 1);
}

namespace {

constexpr int LAYER_BITS = static_cast<int>(Project::MAX_PHYSICS_LAYERS);

constexpr int MIN_LAYERS_SHOWN = 8;  ///< The least number of layers a list offers, named or not.

/// The first two layers @p bits holds, by name, and a count of the rest.
std::string layersListed(const std::vector<std::string>& names, uint32_t bits) {
    std::string out;
    int count = 0;
    for (int bit = 0; bit < LAYER_BITS; ++bit) {
        if ((bits & (1u << bit)) == 0) continue;
        if (++count > 2) continue;
        if (!out.empty()) out += ", ";
        out += physicsLayerName(names, bit);
    }
    if (count > 2) out += " +" + std::to_string(count - 2);
    return out;
}

/// What a closed layer box reads: Everything, Nothing, what it holds, or what it lacks.
std::string layersPreview(const std::vector<std::string>& names, uint32_t bits) {
    if (bits == ~0u) return "Everything";
    if (bits == 0u) return "Nothing";
    int held = 0;
    for (int bit = 0; bit < LAYER_BITS; ++bit) held += (bits >> bit) & 1u;
    // Most of the bits: a mask that leaves a layer out reads by what it leaves out.
    if (held > LAYER_BITS / 2) return "Everything but " + layersListed(names, ~bits);
    return layersListed(names, bits);
}

/// How many layers, from the first, a list offers: the named, those @p bits holds, and a few more.
int layersShown(const std::vector<std::string>& names, uint32_t bits) {
    int shown = std::max(static_cast<int>(names.size()) + 1, MIN_LAYERS_SHOWN);
    if (bits != ~0u) {
        for (int bit = 0; bit < LAYER_BITS; ++bit) {
            if (bits & (1u << bit)) shown = std::max(shown, bit + 1);
        }
    }
    return std::min(shown, LAYER_BITS);
}

} // namespace

bool layerCombo(int* layer, const std::vector<std::string>& names) {
    const uint32_t bits = static_cast<uint32_t>(*layer);
    const bool single   = bits != 0u && (bits & (bits - 1u)) == 0u;
    std::string preview = bits == 0u ? "None" : layersPreview(names, bits);
    if (!single && bits != 0u) preview += " (several)";
    if (!beginCombo("##v", preview.c_str())) return false;

    bool changed = false;
    if (ImGui::Selectable("None", bits == 0u) && bits != 0u) {
        *layer  = 0;
        changed = true;
    }
    for (int bit = 0; bit < layersShown(names, bits); ++bit) {
        const uint32_t    one  = 1u << bit;
        const std::string name = physicsLayerName(names, bit) + "##" + std::to_string(bit);
        if (ImGui::Selectable(name.c_str(), bits == one) && bits != one) {
            *layer  = static_cast<int>(one);
            changed = true;
        }
    }
    ImGui::EndCombo();
    return changed;
}

bool layerMaskCombo(int* mask, const std::vector<std::string>& names) {
    uint32_t bits = static_cast<uint32_t>(*mask);
    if (!beginCombo("##v", layersPreview(names, bits).c_str())) return false;

    bool changed = false;
    const ImGuiSelectableFlags stay = ImGuiSelectableFlags_DontClosePopups;
    if (ImGui::Selectable("Everything", bits == ~0u, stay) && bits != ~0u) {
        bits    = ~0u;
        changed = true;
    }
    if (ImGui::Selectable("Nothing", bits == 0u, stay) && bits != 0u) {
        bits    = 0u;
        changed = true;
    }
    ImGui::Separator();
    for (int bit = 0; bit < layersShown(names, bits); ++bit) {
        const uint32_t one = 1u << bit;
        const std::string name = physicsLayerName(names, bit) + "##" + std::to_string(bit);
        bool on = (bits & one) != 0u;
        if (ImGui::Checkbox(name.c_str(), &on)) {
            bits ^= one;
            changed = true;
        }
    }
    ImGui::EndCombo();
    if (changed) *mask = static_cast<int>(bits);
    return changed;
}

} // namespace Vkm::Engine
