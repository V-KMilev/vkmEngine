#pragma once

#include "core/memory/types.h"
#include "resource/asset_type.h"

namespace Vkm::Engine {

struct EditorContext;
class Resource;
class ResourceManager;

/**
 * @brief One asset's own window: the asset alone in a viewport of its own, and what it is.
 *
 * AssetEditors owns them, one for each asset open. An editor knows its asset by handle, whose
 * generation a reused slot does not match, so one whose asset is deleted is closed, never shown
 * another; a replaced asset graph, which reuses generations too, closes them all
 * (AssetEditors::forgetOtherGraph).
 */
class AssetEditor {
    public:
        /**
         * @brief An editor of the asset of kind @p type at @p key.
         *
         * @param type Its kind.
         * @param key  Its handle's slot and generation.
         */
        AssetEditor(AssetType type, StorageIndex key)
            : m_type(type)
            , m_key(key)
        {}
        virtual ~AssetEditor() = default;

        AssetEditor(const AssetEditor& other) = delete;
        AssetEditor& operator=(const AssetEditor& other) = delete;

        AssetEditor(AssetEditor && other) = delete;
        AssetEditor& operator=(AssetEditor && other) = delete;

    public:
        /**
         * @brief Its asset, while it is loaded.
         *
         * @param resources Where it is loaded.
         * @return The asset, or null once it has gone.
         */
        virtual const Resource* asset(const ResourceManager& resources) const = 0;

        /**
         * @brief Draw the window's content; asset() holds.
         *
         * @param ec The frame's editor context.
         */
        virtual void draw(EditorContext& ec) = 0;

    public:
        AssetType    type() const { return m_type; }
        StorageIndex key() const { return m_key; }

    private:
        AssetType    m_type;
        StorageIndex m_key;
};

} // namespace Vkm::Engine
