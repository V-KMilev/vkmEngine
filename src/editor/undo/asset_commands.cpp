#include "undo/asset_commands.h"

#include <algorithm>
#include <limits>

#include "resource/resource_manager.h"
#include "undo/command_host.h"

namespace Vkm::Engine {

// swapValue bumps the version a cache watches; a snapshot assigned by hand would
// not, and undo would change the asset but not the picture.
void MaterialEditCommand::step(const MaterialAsset& value) {
    if (!m_resources->isAlive(m_handle)) return;
    MaterialAsset next = value;
    m_resources->swapValue(m_handle, next);
}

void MaterialEditCommand::redo(Scene&, CommandHost&) {
    step(m_after);
}

void MaterialEditCommand::undo(Scene&, CommandHost&) {
    step(m_before);
}

bool MaterialEditCommand::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<MaterialEditCommand*>(&incoming);
    if (!p || p->m_handle != m_handle) return false;

    // Copy-construct then move, as asset assignment is deleted. These were never in
    // the manager, so the identity the constructor leaves defaulted is nobody's.
    m_after = MaterialAsset(p->m_after);
    return true;
}

void TerrainEditCommand::step(const TerrainAsset& value) {
    if (!m_resources->isAlive(m_handle)) return;
    TerrainAsset next = value;
    m_resources->swapValue(m_handle, next);
}

void TerrainEditCommand::redo(Scene&, CommandHost&) {
    step(m_after);
}

void TerrainEditCommand::undo(Scene&, CommandHost&) {
    if (!m_resources->isAlive(m_handle)) return;
    m_after = TerrainAsset(m_resources->get(m_handle));
    step(m_before);
}

bool TerrainEditCommand::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<TerrainEditCommand*>(&incoming);
    // The asset already holds the incoming edit; this step's before is still the gesture's.
    return p && continues(p->m_handle, p->m_label);
}

void TerrainEditCommand::apply(
    ResourceManager& resources,
    CommandHost& host,
    TerrainHandle handle,
    const std::function<void(TerrainAsset&)>& edit,
    const char* label
) {
    const auto* open = dynamic_cast<const TerrainEditCommand*>(host.commandStack().mergeTarget());
    std::unique_ptr<TerrainEditCommand> step;
    if (!open || !open->continues(handle, label)) {
        TerrainAsset before(resources.get(handle));
        step = std::make_unique<TerrainEditCommand>(resources, handle, std::move(before), label);
    }
    edit(resources.edit(handle));
    resources.commit(handle);
    if (step) host.pushStep(std::move(step));
    else      host.markSceneDirty();
}

bool TerrainEditCommand::continues(TerrainHandle handle, const char* label) const {
    return handle == m_handle && std::strcmp(label, m_label) == 0;
}

namespace {

/// Whether @p patch was cut from a region of @p terrain's grid as it is now.
bool fits(
    const TerrainAsset& terrain,
    const TerrainBrush::Region& region,
    const TerrainStrokeCommand::Patch& patch
) {
    const auto   side  = static_cast<int>(terrain.resolution);
    const size_t count = static_cast<size_t>(region.i1 - region.i0 + 1) * (region.j1 - region.j0 + 1);
    return !region.empty() && region.i0 >= 0 && region.j0 >= 0 && region.i1 < side && region.j1 < side
        && terrain.holdsGrid() && patch.heights.size() == count
        && patch.weights.size() == count * TerrainAsset::WEIGHTS_PER_VERTEX
        && patch.density.size() == terrain.foliage.size();
}

} // namespace

TerrainStrokeCommand::Patch TerrainStrokeCommand::Patch::cut(
    const TerrainAsset& terrain,
    const TerrainBrush::Region& region
) {
    Patch patch;
    patch.density.resize(terrain.foliage.size());
    for (int j = region.j0; j <= region.j1; ++j) {
        const size_t row   = static_cast<size_t>(j) * terrain.resolution;
        const size_t first = row + region.i0;
        const size_t last  = row + region.i1 + 1;
        const auto heights = terrain.heights.begin();
        patch.heights.insert(patch.heights.end(), heights + first, heights + last);
        patch.weights.insert(
            patch.weights.end(),
            terrain.weights.begin() + first * TerrainAsset::WEIGHTS_PER_VERTEX,
            terrain.weights.begin() + last * TerrainAsset::WEIGHTS_PER_VERTEX
        );
        // A layer with no map has none anywhere, which a stroke may grow.
        for (size_t f = 0; f < terrain.foliage.size(); ++f) {
            const std::vector<uint8_t>& density = terrain.foliage[f].density;
            std::vector<uint8_t>& out = patch.density[f];
            if (density.size() != terrain.heights.size()) out.insert(out.end(), last - first, 0);
            else out.insert(out.end(), density.begin() + first, density.begin() + last);
        }
    }
    return patch;
}

void TerrainStrokeCommand::step(const Patch& patch) {
    if (!m_resources->isAlive(m_handle)) return;
    TerrainAsset& terrain = m_resources->edit(m_handle);
    if (!fits(terrain, m_region, patch)) return;
    const size_t width = static_cast<size_t>(m_region.i1 - m_region.i0 + 1);
    for (int j = m_region.j0; j <= m_region.j1; ++j) {
        const size_t from = static_cast<size_t>(j - m_region.j0) * width;
        const size_t to   = static_cast<size_t>(j) * terrain.resolution + m_region.i0;
        std::copy_n(patch.heights.begin() + from, width, terrain.heights.begin() + to);
        const size_t shares = TerrainAsset::WEIGHTS_PER_VERTEX;
        const auto   into   = terrain.weights.begin() + to * shares;
        std::copy_n(patch.weights.begin() + from * shares, width * shares, into);
        for (size_t f = 0; f < terrain.foliage.size(); ++f) {
            std::vector<uint8_t>& density = terrain.foliage[f].density;
            if (patch.density[f].empty()) continue;
            density.resize(terrain.heights.size(), 0);
            std::copy_n(patch.density[f].begin() + from, width, density.begin() + to);
        }
    }
    m_resources->commit(m_handle);
}

void TerrainStrokeCommand::redo(Scene&, CommandHost&) {
    step(m_after);
}

void TerrainStrokeCommand::undo(Scene&, CommandHost&) {
    step(m_before);
}

template <typename HandleType>
void RenameAssetCommand<HandleType>::redo(Scene&, CommandHost&) {
    if (!m_resources->isAlive(m_handle)) return;
    m_resources->rename(m_handle, m_after);
}

template <typename HandleType>
void RenameAssetCommand<HandleType>::undo(Scene&, CommandHost&) {
    if (!m_resources->isAlive(m_handle)) return;
    m_resources->rename(m_handle, m_before);
}

template class RenameAssetCommand<MaterialHandle>;
template class RenameAssetCommand<MeshHandle>;
template class RenameAssetCommand<TextureHandle>;
template class RenameAssetCommand<AnimationClipHandle>;
template class RenameAssetCommand<AudioClipHandle>;

} // namespace Vkm::Engine
