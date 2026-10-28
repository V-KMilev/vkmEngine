#include "asset_editors/asset_editors.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include <imgui.h>

#include "asset_editors/asset_editor.h"
#include "asset_editors/clip_viewer.h"
#include "asset_editors/material_editor.h"
#include "asset_editors/mesh_editor.h"
#include "asset_editors/texture_viewer.h"
#include "core/system.h"
#include "editor_context.h"
#include "resource/resource_manager.h"
#include "session/material_preview_session.h"
#include "ui/editor_style.h"

namespace Vkm::Engine {

namespace {

/// A new editor window's size, design pixels: room for the viewport and the facts beside it.
constexpr float WINDOW_WIDTH  = 880.0f;
constexpr float WINDOW_HEIGHT = 560.0f;

/// How far each window opened after another sits down and right of it, design pixels.
constexpr float CASCADE_STEP = 28.0f;

/// Where a preview key's kind starts: below it, the asset's slot.
constexpr uint32_t KIND_SHIFT = 40;

/// The bit no thumbnail's key sets.
constexpr uint64_t VIEWPORT_KEY_BIT = 1ull << 56;
static_assert(
    ((static_cast<uint64_t>(AssetType::Count) + 1ull) << KIND_SHIFT) < VIEWPORT_KEY_BIT,
    "A thumbnail's key stops below the viewports'"
);

template <typename Editor>
std::unique_ptr<AssetEditor> makeEditor(StorageIndex key) {
    return std::make_unique<Editor>(key);
}

/// A kind of asset that has an editor: what makes one, and what its window calls the kind.
struct EditorKind {
    AssetType   type;
    const char* word;
    std::unique_ptr<AssetEditor> (*make)(StorageIndex key);
};

constexpr EditorKind EDITOR_KINDS[] = {
    {AssetType::Mesh,          "Mesh",     &makeEditor<MeshEditor>},
    {AssetType::Texture,       "Texture",  &makeEditor<TextureViewer>},
    {AssetType::Material,      "Material", &makeEditor<MaterialEditor>},
    {AssetType::AnimationClip, "Clip",     &makeEditor<ClipViewer>},
};

/**
 * @brief The editor kind of assets of @p type.
 *
 * @param type The asset's kind.
 * @return Its row of EDITOR_KINDS, or null for a kind with no editor.
 */
const EditorKind* editorKind(AssetType type) {
    for (const EditorKind& kind : EDITOR_KINDS) {
        if (kind.type == type) return &kind;
    }
    return nullptr;
}

} // namespace

AssetEditors::AssetEditors()  = default;
AssetEditors::~AssetEditors() = default;

bool AssetEditors::opens(AssetType type) {
    return editorKind(type) != nullptr;
}

uint64_t AssetEditors::thumbnailKey(AssetType type, uint32_t id) {
    return ((static_cast<uint64_t>(type) + 1ull) << KIND_SHIFT) | id;
}

uint64_t AssetEditors::viewportKey(AssetType type, StorageIndex key) {
    return VIEWPORT_KEY_BIT | (static_cast<uint64_t>(type) << KIND_SHIFT) | key.index;
}

void AssetEditors::open(AssetType type, StorageIndex key, const ResourceManager& resources) {
    forgetOtherGraph(resources);
    const auto same = [&](const std::unique_ptr<AssetEditor>& e) {
        return e->type() == type && e->key() == key;
    };
    const auto it = std::find_if(m_open.begin(), m_open.end(), same);
    if (it != m_open.end()) {
        m_reveal = it->get();
        return;
    }
    const EditorKind* kind = editorKind(type);
    if (!kind) return;
    std::unique_ptr<AssetEditor> made = kind->make(key);
    if (!made->asset(resources)) return;
    m_reveal = made.get();
    m_open.push_back(std::move(made));
}

void AssetEditors::draw(EditorContext& ec) {
    const ResourceManager& resources = ec.frame.resources;
    forgetOtherGraph(resources);
    const ImVec2 size(EditorStyle::px(WINDOW_WIDTH), EditorStyle::px(WINDOW_HEIGHT));
    for (size_t i = 0; i < m_open.size();) {
        AssetEditor&    editor = *m_open[i];
        const Resource* asset  = editor.asset(resources);
        bool            open   = asset != nullptr;
        if (open) {
            // The id is the asset's, so a rename keeps the window's place.
            char title[256];
            std::snprintf(
                title,
                sizeof(title),
                "%s - %s###asset/%u/%u/%u",
                asset->name().c_str(),
                editorKind(editor.type())->word,
                static_cast<unsigned>(editor.type()),
                editor.key().index,
                editor.key().generation
            );
            // Each opened after another sits a step down and right of it, so none hides another.
            const float  step   = EditorStyle::px(CASCADE_STEP) * static_cast<float>(i);
            const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
            const ImVec2 at(centre.x + step, centre.y + step);
            ImGui::SetNextWindowPos(at, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
            if (m_reveal == &editor) ImGui::SetNextWindowFocus();
            // Unsaved: the id is the slot's, which another run's asset holds and would open at this
            // one's place; and imgui.ini would keep an entry per asset ever opened.
            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin(title, &open, flags)) editor.draw(ec);
            ImGui::End();
        }
        if (open) {
            ++i;
            continue;
        }
        ec.materialPreviews.evict(viewportKey(editor.type(), editor.key()));
        m_open.erase(m_open.begin() + static_cast<std::ptrdiff_t>(i));
    }
    m_reveal = nullptr;
}

void AssetEditors::forgetOtherGraph(const ResourceManager& resources) {
    if (m_epoch == resources.epoch()) return;
    m_epoch = resources.epoch();
    m_open.clear();
    m_reveal = nullptr;
}

} // namespace Vkm::Engine
