#include "debug/behavior_error_log.h"

namespace Engine {

BehaviorErrorLog& BehaviorErrorLog::get() {
    static BehaviorErrorLog instance;
    return instance;
}

void BehaviorErrorLog::push(std::string behaviorName, std::string hook, std::string message) {
    if (!m_entries.empty()) {
        Entry& last = m_entries.back();
        if (last.behaviorName == behaviorName && last.hook == hook && last.message == message) {
            last.repeatCount += 1;
            last.timestamp = std::chrono::system_clock::now();
            return;  // a repeat: leave m_totalPushed alone so it doesn't re-toast
        }
    }

    Entry entry;
    entry.timestamp    = std::chrono::system_clock::now();
    entry.behaviorName = std::move(behaviorName);
    entry.hook         = std::move(hook);
    entry.message      = std::move(message);
    entry.repeatCount  = 1;
    m_entries.push_back(std::move(entry));
    ++m_totalPushed;

    if (m_entries.size() > CAPACITY) {
        m_entries.erase(m_entries.begin(),
                        m_entries.begin() + (m_entries.size() - CAPACITY));
    }
}

void BehaviorErrorLog::clearAll() {
    m_entries.clear();
}

std::vector<BehaviorErrorLog::Entry> BehaviorErrorLog::snapshot() const {
    return std::vector<Entry>(m_entries.rbegin(), m_entries.rend());
}

std::size_t BehaviorErrorLog::size() const {
    return m_entries.size();
}

} // namespace Engine
