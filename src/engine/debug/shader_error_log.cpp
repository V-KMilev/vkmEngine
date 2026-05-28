#include "debug/shader_error_log.h"

#include <algorithm>

namespace Engine {

ShaderErrorLog& ShaderErrorLog::get() {
    static ShaderErrorLog instance;
    return instance;
}

void ShaderErrorLog::push(
    std::string shaderName,
    std::string definesSummary,
    std::string message
) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_entries.empty()) {
        Entry& last = m_entries.back();
        if (last.shaderName == shaderName
            && last.definesSummary == definesSummary
            && last.message == message) {
            last.repeatCount += 1;
            last.timestamp = std::chrono::system_clock::now();
            return;
        }
    }

    Entry entry;
    entry.timestamp       = std::chrono::system_clock::now();
    entry.shaderName      = std::move(shaderName);
    entry.definesSummary  = std::move(definesSummary);
    entry.message         = std::move(message);
    entry.repeatCount     = 1;
    m_entries.push_back(std::move(entry));

    if (m_entries.size() > CAPACITY) {
        m_entries.erase(m_entries.begin(),
                        m_entries.begin() + (m_entries.size() - CAPACITY));
    }
}

void ShaderErrorLog::clearFor(const std::string& shaderName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
            [&](const Entry& e) { return e.shaderName == shaderName; }),
        m_entries.end());
}

void ShaderErrorLog::clearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

std::vector<ShaderErrorLog::Entry> ShaderErrorLog::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Entry> result(m_entries.rbegin(), m_entries.rend());
    return result;
}

std::size_t ShaderErrorLog::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

} // namespace Engine
