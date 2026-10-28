#include "session/log_tail.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <vector>

#include "logger.h"

namespace Vkm::Engine {

namespace {

static_assert(
    static_cast<size_t>(Vkm::Log::LogLevel::FATAL) + 1 == static_cast<size_t>(LogSeverity::Count),
    "a LogSeverity for each of vkmLog's levels"
);

/// vkmLog's level names, in LogSeverity order: its LogLevel's, one for one.
constexpr std::array<std::string_view, static_cast<size_t>(LogSeverity::Count)> LEVELS = {
    "TRACE",
    "VERBOSE",
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR",
    "FATAL"
};

/// The most bytes one poll reads, so a burst never stalls a frame.
constexpr std::uintmax_t MAX_READ = 1u << 20;

} // namespace

LogLine parseLogLine(std::string_view line) {
    // The bracketed fields at the front, up to four: time, suffix, category, level.
    // Each field's text, and where what follows it begins.
    std::vector<std::string_view> fields;
    std::vector<size_t>           after;
    size_t at = 0;
    while (fields.size() < 4 && at < line.size() && line[at] == '[') {
        const size_t close = line.find(']', at);
        if (close == std::string_view::npos) break;
        fields.push_back(line.substr(at + 1, close - at - 1));
        at = close + 1;
        if (at < line.size() && line[at] == ' ') ++at;
        after.push_back(at);
    }

    LogLine parsed;
    // The level is the first field after the suffix holding a level name; after it come the
    // message's own brackets. Without one, the line is plain text.
    for (size_t field = 2; field < fields.size(); ++field) {
        for (size_t level = 0; level < LEVELS.size(); ++level) {
            if (fields[field] != LEVELS[level]) continue;
            parsed.severity = static_cast<LogSeverity>(level);
            parsed.time     = std::string(fields[0]);
            if (field == 3) parsed.category = std::string(fields[2]);
            parsed.text = std::string(line.substr(after[field]));
            return parsed;
        }
    }
    parsed.text = std::string(line);
    return parsed;
}

void LogTail::open(const std::filesystem::path& path, std::uintmax_t offset) {
    m_path   = path;
    m_offset = offset;
    m_partial.clear();
    m_lines.clear();
    m_counts.fill(0);
    m_dropped = 0;
    ++m_revision;
}

bool LogTail::poll() {
    if (m_path.empty()) return false;
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(m_path, ec);
    if (ec) return false;
    // A file cut short was started again: read it from its start.
    if (size < m_offset) {
        m_offset = 0;
        m_partial.clear();
    }
    if (size == m_offset) return false;

    const std::uintmax_t count = std::min(size - m_offset, MAX_READ);
    std::ifstream in(m_path, std::ios::binary);
    in.seekg(static_cast<std::streamoff>(m_offset));
    std::string chunk(static_cast<size_t>(count), '\0');
    in.read(chunk.data(), static_cast<std::streamsize>(count));
    chunk.resize(static_cast<size_t>(in.gcount()));
    m_offset += chunk.size();
    m_partial += chunk;

    bool any = false;
    size_t start = 0;
    for (size_t end = m_partial.find('\n'); end != std::string::npos; end = m_partial.find('\n', start)) {
        std::string_view line(m_partial.data() + start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        m_lines.push_back(parseLogLine(line));
        ++m_counts[static_cast<size_t>(m_lines.back().severity)];
        if (m_lines.size() > MAX_LINES) {
            --m_counts[static_cast<size_t>(m_lines.front().severity)];
            m_lines.pop_front();
            ++m_dropped;
        }
        any   = true;
        start = end + 1;
    }
    m_partial.erase(0, start);
    if (any) ++m_revision;
    return any;
}

void LogTail::clear() {
    m_dropped += m_lines.size();
    m_lines.clear();
    m_counts.fill(0);
    ++m_revision;
}

} // namespace Vkm::Engine
