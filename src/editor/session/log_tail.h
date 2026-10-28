#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>

namespace Vkm::Engine {

/**
 * @brief How loud a log line is, as vkmLog writes its level.
 */
enum class LogSeverity : uint8_t {
    Trace,
    Verbose,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
    Count  ///< Sentinel.
};

/**
 * @brief One line of the log, split where vkmLog joins it.
 */
struct LogLine {
    std::string time;      ///< As written: "YYYY-MM-DD HH:MM:SS.mmm".
    std::string category;  ///< The subsystem tag; empty when the line has none.
    std::string text;      ///< The message, or the whole line when it is not one of vkmLog's.
    LogSeverity severity = LogSeverity::Info;
};

/**
 * @brief Split @p line as vkmLog writes one: "[time] [suffix] [category] [LEVEL] message",
 *        the category optional.
 *
 * @param line One line, no newline.
 * @return Its parts; a line of another shape is kept whole as Info text.
 */
LogLine parseLogLine(std::string_view line);

/**
 * @brief The log file followed as it grows: what this session wrote, line by line.
 *
 * The editor's Console reads the file the logger already writes and flushes each line
 * into, so nothing in the logger knows it is watched. Free of ImGui so it is tested
 * headlessly.
 */
class LogTail {
    public:
        /// The most lines held; the oldest go first.
        static constexpr size_t MAX_LINES = 5000;

    public:
        LogTail() = default;
        ~LogTail() = default;

        LogTail(const LogTail& other) = delete;
        LogTail& operator=(const LogTail& other) = delete;

        LogTail(LogTail && other) = delete;
        LogTail& operator=(LogTail && other) = delete;

    public:
        /**
         * @brief Follow @p path from @p offset, where this session's lines begin, forgetting all
         *        that was followed before.
         *
         * @param path   The log file.
         * @param offset Bytes into it to start at.
         */
        void open(const std::filesystem::path& path, std::uintmax_t offset);

        /**
         * @brief Take whatever whole lines were written since the last call.
         *
         * A partial last line waits for its newline.
         *
         * @return Whether any line arrived.
         */
        bool poll();

        /// Forget the lines held; what is written next still arrives.
        void clear();

    public:
        const std::deque<LogLine>& lines() const { return m_lines; }

        /// How many of the lines held are of each severity, kept as they come and go.
        const std::array<size_t, static_cast<size_t>(LogSeverity::Count)>& counts() const { return m_counts; }

        /// Moved by every change to the lines held, for a reader that caches what it made of them.
        uint64_t revision() const { return m_revision; }

        /// How many lines have gone from the front: lines()[i] is the (dropped() + i)th of the run.
        uint64_t dropped() const { return m_dropped; }

        const std::filesystem::path& path() const { return m_path; }

    private:
        std::filesystem::path m_path;
        std::uintmax_t        m_offset = 0;
        std::string           m_partial;  ///< Bytes read past the last newline.
        std::deque<LogLine>   m_lines;
        std::array<size_t, static_cast<size_t>(LogSeverity::Count)> m_counts{};
        uint64_t              m_revision = 0;
        uint64_t              m_dropped  = 0;
};

} // namespace Vkm::Engine
