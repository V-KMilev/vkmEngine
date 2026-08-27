#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include <imgui.h>

namespace Vkm::Engine {

/**
 * @brief Cached, modal asset-path picker shared by every editor file dialog.
 *
 * A one-frame UI helper that walks the filesystem (recursive_directory_iterator
 * etc.) every frame the modal is open lags with big asset trees, so this one
 * scans once when the popup opens, caches the listing, and reuses it until the
 * popup closes. Configure through options(), call open(), then draw() every
 * frame until it returns true.
 *
 * The picker draws a standard modal with a popup id derived from
 * `options().popupId`. Each panel owns its own picker member so popup ids
 * stay unique and so the cache survives across frames.
 */
class AssetPicker {
    public:
        enum class Kind { Files, Directories };

        struct Options {
            const char* popupId      = "AssetPicker"; ///< Unique popup id.
            const char* title        = "Pick asset";  ///< Modal title shown to user.
            std::filesystem::path root;               ///< Search root.
            bool recursive           = false;         ///< Walk subdirectories.
            Kind kind                = Kind::Files;
            std::vector<std::string> extensions;      ///< Lowercased file extensions ({".png", ".jpg"}). Ignored for Directories.
            int maxResults           = 4000;          ///< Safety cap.
            /**
            * @brief If non-empty, the picker returns paths relative to `relativeTo`
            * instead of absolute. Useful when storing as scene references.
            */
            std::filesystem::path relativeTo;
            /**
            * @brief Optional one-line hint shown above the list.
            */
            std::string hint;
        };

        /**
         * @brief Queue the popup to open on the next draw().
         *
         * Sets a deferred flag rather than calling ImGui::OpenPopup directly so
         * the open is issued from inside draw(), where the popup id is in scope.
         */
        void open();

        /**
         * @brief Draw the picker modal. Returns true the frame the user picks an
         * entry; @p outPath is then set to the picked path (made relative to
         * `options().relativeTo` when that is set).
         *
         * A single click selects; double-click, Enter, or the Open button
         * confirms (Enter in the search field confirms the selection, or the
         * only match when the filter narrows to one). Escape / Cancel dismiss.
         */
        bool draw(std::string& outPath);

        /**
         * @brief What the picker lists and how it reports a pick.
         *
         * Read by the next open(); changing it while the popup is up does not
         * re-scan.
         */
        Options& options() { return m_options; }
        const Options& options() const { return m_options; }

    private:
        void refreshIfNeeded();

    private:
        Options m_options;
        bool m_openRequested = false;
        char m_filter[64] = {};   ///< Live search needle, cleared on every open.
        int  m_selected   = -1;   ///< Selected row (index into the unfiltered lists), -1 = none.
        bool m_truncated  = false; ///< The listing hit maxResults, so the view is partial.
        std::vector<std::string> m_entries;  ///< Display strings (filename or relative).
        std::vector<std::filesystem::path> m_paths;  ///< Absolute (or relative-to) paths.
};

} // namespace Vkm::Engine
