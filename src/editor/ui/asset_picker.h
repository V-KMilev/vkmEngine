#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include <imgui.h>

#include "io/project_paths.h"

namespace Vkm::Engine {

/**
 * @brief Cached, modal asset-path picker for the editor's file dialogs.
 *
 * A one-frame UI helper that walks the filesystem every frame the modal is open
 * lags with big asset trees, so this one scans once when the popup opens and
 * caches the listing. Call open() with the options, then draw() every frame
 * until it returns true.
 *
 * The picker draws a standard modal titled `Options::title`, which is also its
 * popup id. Give each owner its own picker member so those ids stay unique and
 * the cache survives across frames.
 */
class AssetPicker {
    public:
        enum class Kind { Files, Directories };

        struct Options {
            /**
             * @brief The modal's title, which is also its popup id.
             */
            const char* title        = "Pick asset";
            std::filesystem::path root;               ///< Search root.
            bool recursive           = false;
            Kind kind                = Kind::Files;
            /// Lowercased file extensions ({".png", ".jpg"}). Ignored for Directories.
            std::vector<std::string> extensions;
            int maxResults           = 4000;          ///< Safety cap.
            /**
             * @brief Root that picked paths are reported relative to; empty
             * reports them absolute.
             *
             * The project root by default: a scene stores a reference as a
             * project-relative path.
             */
            std::filesystem::path relativeTo = ProjectPaths::projectRoot();
            std::string hint;                         ///< Optional one-line hint shown above the list.
        };

    public:
        AssetPicker() = default;
        ~AssetPicker() = default;

        AssetPicker(const AssetPicker& other) = delete;
        AssetPicker& operator=(const AssetPicker& other) = delete;

        AssetPicker(AssetPicker && other) = delete;
        AssetPicker& operator=(AssetPicker && other) = delete;

    public:
        /**
         * @brief Scan @p options' root and raise the popup on the next draw().
         *
         * The listing is taken here, once, and reused until the next open().
         *
         * @param options What the picker lists and how it reports a pick, until the next open().
         */
        void open(Options options);

        /**
         * @brief Draw the picker modal.
         *
         * A single click selects; double-click, Enter, or the Open button
         * confirms (Enter in the search field confirms the selection, or the
         * only match when the filter narrows to one). Escape / Cancel dismiss.
         *
         * @param outPath Set to the picked path the frame a pick happens, made
         *        relative to `Options::relativeTo` when that is set.
         * @return True the frame the user picks an entry.
         */
        bool draw(std::string& outPath);

    private:
        /// Walk `Options::root` into m_entries / m_paths, clearing the filter.
        void rescan();

    private:
        Options m_options;
        bool m_wantOpen   = false; ///< Dialog-visible intent, owned by beginDialog.
        char m_filter[64] = {};   ///< Live search needle, cleared on every open.
        int  m_selected   = -1;   ///< Selected row (index into the unfiltered lists), -1 = none.
        bool m_truncated  = false; ///< The listing hit maxResults, so the view is partial.
        /// Each row's text; what draw() reports when relativeTo is set.
        std::vector<std::string> m_entries;
        /// Each row's path as the walk found it; what draw() reports otherwise.
        std::vector<std::filesystem::path> m_paths;
};

} // namespace Vkm::Engine
