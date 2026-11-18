#pragma once

#include <filesystem>
#include <fstream>
#include <ios>

namespace Vkm::Engine {

/**
 * @brief A file written whole or not at all.
 *
 * What is written goes to a temporary beside the file, its directories made; commit() renames it
 * over the file, which within a directory is atomic, so through a crash of the program the file is
 * its old self or its new one. Nothing is flushed to the disk first, as a save from the frame - a
 * game's own file, the editor's settings - would wait on the disk: whether a power cut keeps the
 * rename behind the data is the file system's.
 * A write never committed - it failed, or its writer gave up - takes its temporary with it. The
 * temporary is this write's own, so two writers of one file - threads, or a cook beside the editor -
 * never write into each other's; the last to commit is the file. One a crash leaves stays.
 */
class WholeFileWrite {
    public:
        /**
         * @brief Open the temporary for @p path.
         *
         * @param path The file to write.
         * @param mode Open mode beyond `out | trunc`, e.g. std::ios::binary.
         * @param what What the file is, for the log, e.g. "Cooked mesh".
         */
        WholeFileWrite(const std::filesystem::path& path, std::ios::openmode mode, const char* what);
        ~WholeFileWrite();

        WholeFileWrite(const WholeFileWrite& other) = delete;
        WholeFileWrite& operator=(const WholeFileWrite& other) = delete;

        WholeFileWrite(WholeFileWrite && other) = delete;
        WholeFileWrite& operator=(WholeFileWrite && other) = delete;

    public:
        /// Where the file's bytes go.
        std::ofstream& stream() { return m_out; }

        /// Whether the temporary opened and every write so far has gone in; a failed open is logged.
        explicit operator bool() const { return static_cast<bool>(m_out); }

        /**
         * @brief Close the temporary and rename it over the file.
         *
         * @return False, having logged, when a write, the close or the rename failed.
         */
        bool commit();

    private:
        std::filesystem::path m_path;
        std::filesystem::path m_temp;
        const char*           m_what;
        std::ofstream         m_out;
        bool                  m_done = false;  ///< Committed, or failed and cleaned up.
};

} // namespace Vkm::Engine
