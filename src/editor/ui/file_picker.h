#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include "io/project/roots.h"

namespace Vkm::Engine {

/**
 * @brief Modal asset-path picker: open() scans once, then draw() every frame until it returns true.
 *
 * `Options::title` is also the popup id, so each owner keeps a picker member of its own.
 */
class FilePicker {
    public:
        enum class Kind { Files, Directories };

        struct Options {
            const char*              title      = "Pick asset";          ///< Also the popup id.
            std::filesystem::path    root;                               ///< Search root.
            bool                     recursive  = false;
            Kind                     kind       = Kind::Files;
            std::vector<std::string> extensions;                         ///< Lowercased, {".png", ".jpg"}.
                                                                         ///< Not for Directories.
            int                      maxResults = 4000;                  ///< Safety cap.
            std::filesystem::path    relativeTo = Roots::projectRoot();  ///< What picks are relative to;
                                                                         ///< empty reports them absolute.
            std::string              hint;                               ///< Optional one-line hint shown
                                                                         ///< above the list.
            bool                     many       = false;                 ///< Ctrl and Shift clicks pick
                                                                         ///< several.
        };

    public:
        FilePicker() = default;
        ~FilePicker() = default;

        FilePicker(const FilePicker& other) = delete;
        FilePicker& operator=(const FilePicker& other) = delete;

        FilePicker(FilePicker && other) = delete;
        FilePicker& operator=(FilePicker && other) = delete;

    public:
        /**
         * @brief Scan @p options' root, once until the next open(), and raise the popup on the next draw().
         *
         * @param options What the picker lists and how it reports a pick.
         */
        void open(Options options);

        /**
         * @brief Draw the picker modal.
         *
         * Enter in the search field confirms the selection, or the only match the filter leaves; a
         * double click confirms the entry clicked.
         *
         * @param outPaths The picks in list order, each relative to `Options::relativeTo` when that is
         *                 set; one unless `Options::many`.
         * @return True the frame the user picks.
         */
        bool draw(std::vector<std::string>& outPaths);

        /**
         * @brief draw() for one pick.
         *
         * @param outPath The pick, as draw() reports one.
         * @return True the frame the user picks an entry.
         */
        bool draw(std::string& outPath);

    private:
        /// Walk `Options::root` into m_entries / m_paths, clearing the filter.
        void rescan();

        /**
         * @brief Change the selection as a click on row @p clicked does: alone, or with Ctrl toggled
         *        and with Shift the shown rows from the last plain click's, when `Options::many`.
         *
         * @param shown   The rows the filter shows, in list order.
         * @param clicked The row clicked.
         */
        void click(const std::vector<int>& shown, int clicked);

    private:
        Options                            m_options;
        bool                               m_wantOpen   = false;  ///< Dialog-visible intent, owned by
                                                                  ///< beginDialog.
        char                               m_filter[64] = {};     ///< Live search needle, cleared on every
                                                                  ///< open.
        std::vector<int>                   m_selected;            ///< Rows into the unfiltered lists, in list
                                                                  ///< order.
        int                                m_anchor     = -1;     ///< The last plain click's row, where Shift
                                                                  ///< ranges from.
        bool                               m_truncated  = false;  ///< The listing hit maxResults, so the view
                                                                  ///< is partial.
        std::vector<std::string>           m_entries;             ///< Each row's text; what draw() reports
                                                                  ///< with relativeTo.
        std::vector<std::filesystem::path> m_paths;               ///< As the walk found it; what draw()
                                                                  ///< reports otherwise.
};

} // namespace Vkm::Engine
