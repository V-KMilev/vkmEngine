#pragma once

#include <imgui.h>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace Engine {

/**
 * @brief Cached, modal asset-path picker shared by every editor file dialog.
 *
 * One-frame UI helpers that walk the filesystem (recursive_directory_iterator
 * etc.) every frame the modal is open used to lag with big asset trees.
 * This helper scans once when the popup opens, caches the listing, and reuses
 * it until the popup closes.
 *
 * Usage:
 *   m_picker.options = { ... };
 *   m_picker.open();            // queues OpenPopup on the next draw()
 *   if (std::string picked; m_picker.draw(picked)) { ... }
 *
 * The picker draws a standard modal with a popup id derived from
 * `options.popupId`. Each panel owns its own picker member so popup ids
 * stay unique and so the cache survives across frames.
 */
class AssetPicker {
    public:
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
         * `options.relativeTo` when that is set).
         *
         * A single click selects; double-click, Enter, or the Open button
         * confirms (Enter in the search field confirms the selection, or the
         * only match when the filter narrows to one). Escape / Cancel dismiss.
         */
        bool draw(std::string& outPath);

    private:
        void refreshIfNeeded();

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

        Options options;

    private:
        bool m_openRequested = false;
        char m_filter[64] = {};   ///< Live search needle, cleared on every open.
        int  m_selected   = -1;   ///< Selected row (index into the unfiltered lists), -1 = none.
        bool m_truncated  = false; ///< The listing hit maxResults, so the view is partial.
        std::vector<std::string> m_entries;  ///< Display strings (filename or relative).
        std::vector<std::filesystem::path> m_paths;  ///< Absolute (or relative-to) paths.
};

} // namespace Engine
