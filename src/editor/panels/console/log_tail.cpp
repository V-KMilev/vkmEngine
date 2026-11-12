#include "panels/console/log_tail.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>

namespace Vkm::Engine {

namespace {

constexpr std::uintmax_t MAX_READ = 1u << 20;  ///< The most bytes one poll reads, so a burst never stalls a
                                               ///< frame.

/// @p text as the Console holds it; a line vkmLog did not write is kept whole, as Info.
LogLine lineOf(std::string_view text) {
    LogLine held;
    const std::optional<Log::Line> line = Log::parseLine(text);
    if (!line) {
        held.text = std::string(text);
        return held;
    }
    held.time     = std::string(line->time);
    held.category = std::string(line->category);
    held.text     = std::string(line->message);
    held.level    = line->level;
    return held;
}

} // namespace

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

    bool any     = false;
    size_t start = 0;
    for (size_t end = m_partial.find('\n'); end != std::string::npos; end = m_partial.find('\n', start)) {
        std::string_view line(m_partial.data() + start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        LogLine parsed = lineOf(line);
        any   = true;
        start = end + 1;
        // A line said again at once is one line with a count: a flood is read as one thing, and
        // the counts are of lines, not of how often a driver repeated itself.
        LogLine* last = m_lines.empty() ? nullptr : &m_lines.back();
        if (last && last->level == parsed.level && last->category == parsed.category
            && last->text == parsed.text) {
            ++last->repeats;
            continue;
        }
        m_lines.push_back(std::move(parsed));
        ++m_counts[static_cast<size_t>(m_lines.back().level)];
        if (m_lines.size() > MAX_LINES) {
            --m_counts[static_cast<size_t>(m_lines.front().level)];
            m_lines.pop_front();
            ++m_dropped;
        }
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
