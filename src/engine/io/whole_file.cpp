#define VKM_LOG_CATEGORY "IO"

#include "io/whole_file.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <random>
#include <system_error>

#include "logger.h"

namespace Vkm::Engine {

namespace {

// A temporary no other writer of the same file names: another thread, or another process - a cook
// run beside the editor - writing it at once.
std::filesystem::path temporaryFor(const std::filesystem::path& path) {
    static const uint32_t        s_process = std::random_device{}();
    static std::atomic<uint32_t> s_next{0};
    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), ".%08x.%u.tmp", s_process, s_next.fetch_add(1));
    return std::filesystem::path(path).concat(suffix);
}

} // namespace

WholeFileWrite::WholeFileWrite(const std::filesystem::path& path, std::ios::openmode mode, const char* what)
    : m_path(path)
    , m_temp(temporaryFor(path))
    , m_what(what)
{
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    m_out.open(m_temp, mode | std::ios::out | std::ios::trunc);
    if (!m_out) LOG_ERROR("%s '%s': cannot open for writing", m_what, m_path.string().c_str());
}

WholeFileWrite::~WholeFileWrite() {
    if (m_done) return;
    m_out.close();
    std::error_code ec;
    std::filesystem::remove(m_temp, ec);
}

bool WholeFileWrite::commit() {
    // The close flushes the tail, which is where a full disk says so.
    m_out.close();
    m_done = true;
    std::error_code ec;
    if (!m_out) {
        LOG_ERROR("%s '%s': write failed", m_what, m_path.string().c_str());
        std::filesystem::remove(m_temp, ec);
        return false;
    }
    std::filesystem::rename(m_temp, m_path, ec);
    if (ec) {
        LOG_ERROR("%s '%s': cannot publish (%s)", m_what, m_path.string().c_str(), ec.message().c_str());
        std::filesystem::remove(m_temp, ec);
        return false;
    }
    return true;
}

} // namespace Vkm::Engine
