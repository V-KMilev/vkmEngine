#pragma once

#include <string>
#include <utility>
#include <vector>

#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/asset/texture_asset.h"
#include "system/terrain/terrain_brush.h"

#include "undo/command.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Undoable edit of a material's parameters; a no-op on an asset since deleted.
 *
 * Name, uid and source descriptor are not restored: carrying the name back would reverse a
 * RenameAssetCommand.
 */
class MaterialEditCommand : public Command {
    public:
        /**
         * @brief Record a material edit that has already been applied.
         *
         * @param resources Manager owning the asset.
         * @param handle    Material edited.
         * @param before    Its parameters before the edit.
         * @param after     Its parameters now.
         * @param label     History entry text.
         */
        MaterialEditCommand(
            ResourceManager& resources,
            MaterialHandle handle,
            MaterialAsset before,
            MaterialAsset after,
            const char* label
        )
            : m_resources(&resources)
            , m_handle(handle)
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~MaterialEditCommand() override = default;

        MaterialEditCommand(const MaterialEditCommand& other) = delete;
        MaterialEditCommand& operator=(const MaterialEditCommand& other) = delete;

        MaterialEditCommand(MaterialEditCommand && other) = delete;
        MaterialEditCommand& operator=(MaterialEditCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

    private:
        /**
         * @brief Put @p value's parameters back on the live asset and commit.
         *
         * @param value The parameter set this direction installs.
         */
        void step(const MaterialAsset& value);

    private:
        ResourceManager* m_resources;
        MaterialHandle   m_handle;
        MaterialAsset    m_before;
        MaterialAsset    m_after;
        const char*      m_label;
};

/**
 * @brief Undoable edit of a terrain's settings (layers, tiling, plants, grid), made by apply().
 *
 * Holds the whole terrain before the edit and takes the after when undone, when every later step is
 * undone and the asset is as the edit left it. A brush stroke is a TerrainStrokeCommand.
 */
class TerrainEditCommand : public Command {
    public:
        /**
         * @brief Record a terrain edit that has already been applied.
         *
         * @param resources Manager owning the asset.
         * @param handle    Terrain edited.
         * @param before    The whole terrain before the edit.
         * @param label     History entry text.
         */
        TerrainEditCommand(
            ResourceManager& resources,
            TerrainHandle handle,
            TerrainAsset before,
            const char* label
        )
            : m_resources(&resources)
            , m_handle(handle)
            , m_before(std::move(before))
            , m_label(label)
        {}
        ~TerrainEditCommand() override = default;

        TerrainEditCommand(const TerrainEditCommand& other) = delete;
        TerrainEditCommand& operator=(const TerrainEditCommand& other) = delete;

        TerrainEditCommand(TerrainEditCommand && other) = delete;
        TerrainEditCommand& operator=(TerrainEditCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

        /**
         * @brief Edit @p handle by @p edit, as a step of @p host's history.
         *
         * A drag's first frame records the terrain before it; the rest edit the asset under that
         * step, rather than each copy the whole terrain to be merged into it.
         *
         * @param resources Manager owning the asset.
         * @param host      Whose history takes the step.
         * @param handle    Terrain edited; alive.
         * @param edit      The change, made on the live asset.
         * @param label     History entry text.
         */
        static void apply(
            ResourceManager& resources,
            CommandHost& host,
            TerrainHandle handle,
            const std::function<void(TerrainAsset&)>& edit,
            const char* label
        );

    private:
        /// Whether an edit of @p handle under @p label goes on this step rather than one of its own.
        bool continues(TerrainHandle handle, const char* label) const;

        /// Put @p value back on the live asset and commit.
        void step(const TerrainAsset& value);

    private:
        ResourceManager* m_resources;
        TerrainHandle    m_handle;
        TerrainAsset     m_before;
        TerrainAsset     m_after;  ///< Taken when undone.
        const char*      m_label;
};

/**
 * @brief Undoable brush stroke, holding only the grid region it touched, before and after.
 *
 * No-ops on a terrain since deleted or given another grid.
 */
class TerrainStrokeCommand : public Command {
    public:
        /**
         * @brief A region's heights, layer shares and plant densities, row by row.
         */
        struct Patch {
            std::vector<float>                heights;
            std::vector<uint8_t>              weights;  ///< Four a vertex.
            std::vector<std::vector<uint8_t>> density;  ///< A plant layer each; empty for one with none.

            /**
             * @brief What @p terrain holds in @p region.
             *
             * @param terrain A terrain whose arrays hold its grid.
             * @param region  Within its grid.
             * @return The patch; zeros for a plant layer with no density map.
             */
            static Patch cut(const TerrainAsset& terrain, const TerrainBrush::Region& region);
        };

    public:
        /**
         * @brief Record a stroke that has already been applied.
         *
         * @param resources Manager owning the asset.
         * @param handle    Terrain stroked.
         * @param region    What the stroke touched.
         * @param before    The region before it.
         * @param after     The region now.
         * @param label     History entry text.
         */
        TerrainStrokeCommand(
            ResourceManager& resources,
            TerrainHandle handle,
            TerrainBrush::Region region,
            Patch before,
            Patch after,
            const char* label
        )
            : m_resources(&resources)
            , m_handle(handle)
            , m_region(region)
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~TerrainStrokeCommand() override = default;

        TerrainStrokeCommand(const TerrainStrokeCommand& other) = delete;
        TerrainStrokeCommand& operator=(const TerrainStrokeCommand& other) = delete;

        TerrainStrokeCommand(TerrainStrokeCommand && other) = delete;
        TerrainStrokeCommand& operator=(TerrainStrokeCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }

    private:
        /// Write @p patch back into the live asset's region and commit.
        void step(const Patch& patch);

    private:
        ResourceManager*     m_resources;
        TerrainHandle        m_handle;
        TerrainBrush::Region m_region;
        Patch                m_before;
        Patch                m_after;
        const char*          m_label;
};

/// Rename an asset, undoable; a no-op on one since deleted, which no undo brings back.
template <typename HandleType>
class RenameAssetCommand : public Command {
    public:
        RenameAssetCommand(
            ResourceManager& resources,
            HandleType handle,
            std::string before,
            std::string after,
            const char* label
        )
            : m_resources(&resources)
            , m_handle(handle)
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~RenameAssetCommand() override = default;

        RenameAssetCommand(const RenameAssetCommand& other) = delete;
        RenameAssetCommand& operator=(const RenameAssetCommand& other) = delete;

        RenameAssetCommand(RenameAssetCommand && other) = delete;
        RenameAssetCommand& operator=(RenameAssetCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }

    private:
        ResourceManager* m_resources;
        HandleType       m_handle;
        std::string      m_before;
        std::string      m_after;
        const char*      m_label;
};

extern template class RenameAssetCommand<MaterialHandle>;
extern template class RenameAssetCommand<MeshHandle>;
extern template class RenameAssetCommand<TextureHandle>;
extern template class RenameAssetCommand<AnimationClipHandle>;
extern template class RenameAssetCommand<AudioClipHandle>;

} // namespace Vkm::Engine
