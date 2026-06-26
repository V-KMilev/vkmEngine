#define VKM_LOG_CATEGORY "ENGINE"

#include "debug/engine_error_log.h"

#include <utility>

#include "logger.h"

namespace Engine {

namespace {
// The single optional sink. The editor installs its own EngineErrorLog here;
// the runtime leaves it null so errors only reach the log.
EngineErrorLog* g_sink = nullptr;
} // namespace

void reportError(const char* category, std::string source, std::string message) {
    const char* cat = category ? category : "";
    LOG_ERROR("[%s] %s: %s", cat, source.c_str(), message.c_str());
    if (g_sink) g_sink->push(cat, std::move(source), std::move(message));
}

void setErrorSink(EngineErrorLog* sink) {
    g_sink = sink;
}

void EngineErrorLog::push(std::string category, std::string source, std::string message) {
    if (!m_entries.empty()) {
        Entry& last = m_entries.back();
        if (last.category == category && last.source == source && last.message == message) {
            last.repeatCount += 1;
            last.timestamp = std::chrono::system_clock::now();
            return;  // a repeat: leave m_totalPushed alone so it doesn't re-toast
        }
    }

    Entry entry;
    entry.timestamp   = std::chrono::system_clock::now();
    entry.category    = std::move(category);
    entry.source      = std::move(source);
    entry.message     = std::move(message);
    entry.repeatCount = 1;
    m_entries.push_back(std::move(entry));
    ++m_totalPushed;

    if (m_entries.size() > CAPACITY) {
        m_entries.erase(m_entries.begin(),
                        m_entries.begin() + (m_entries.size() - CAPACITY));
    }
}

void EngineErrorLog::clearAll() {
    m_entries.clear();
}

std::vector<EngineErrorLog::Entry> EngineErrorLog::snapshot() const {
    return std::vector<Entry>(m_entries.rbegin(), m_entries.rend());
}

std::size_t EngineErrorLog::size() const {
    return m_entries.size();
}

} // namespace Engine
