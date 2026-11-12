#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "base/container/storage_index.h"
#include "resource/asset_type.h"

namespace Vkm::Engine {

struct EditorContext;
class AssetEditor;
class ResourceManager;

/**
 * @brief The open asset editors, each in a window of its own that docks, floats or leaves the main window.
 *
 * The Assets panel opens them by way of EditorState::openAssetEditor.
 */
class AssetEditors {
    public:
        AssetEditors();
        ~AssetEditors();

        AssetEditors(const AssetEditors& other) = delete;
        AssetEditors& operator=(const AssetEditors& other) = delete;

        AssetEditors(AssetEditors && other) = delete;
        AssetEditors& operator=(AssetEditors && other) = delete;

    public:
        /**
         * @brief Whether assets of @p type have an editor.
         *
         * @param type The kind.
         * @return True when open() makes one.
         */
        static bool opens(AssetType type);

        /**
         * @brief Open the editor of the asset @p key, or bring its window forward if it is open.
         *
         * @param type      Its kind; one opens() refuses is ignored.
         * @param key       Its handle's slot and generation; an empty one is ignored.
         * @param resources Where it is loaded.
         */
        void open(AssetType type, StorageIndex key, const ResourceManager& resources);

        /**
         * @brief Draw every open editor, closing those whose window was closed or asset has gone.
         *
         * @param ec The frame's editor context.
         */
        void draw(EditorContext& ec);

    private:
        /**
         * @brief Close every editor once the asset graph is another (ResourceManager::epoch).
         *
         * A new graph reuses slots and generations, so each would show another asset.
         *
         * @param resources The graph now loaded.
         */
        void forgetOtherGraph(const ResourceManager& resources);

    private:
        std::vector<std::unique_ptr<AssetEditor>> m_open;
        const AssetEditor*                        m_reveal = nullptr;  ///< The one whose window comes forward
                                                                       ///< next frame.
        uint64_t                                  m_epoch  = 0;        ///< The graph the editors are of.
};

} // namespace Vkm::Engine
