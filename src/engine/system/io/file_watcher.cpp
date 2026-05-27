#define VKM_LOG_CATEGORY "FILEWATCH"

#include "system/io/file_watcher.h"

#include <system_error>

#include "logger.h"

#include "debug/profiler.h"
#include "core/system.h"

namespace Engine {

namespace fs = std::filesystem;

FileWatcher::FileWatcher(float intervalSeconds)
    : m_interval(intervalSeconds)
{
}

void FileWatcher::watch(std::string dirPath, OnChange onChange) {
    Entry entry;
    entry.path     = std::move(dirPath);
    entry.onChange = std::move(onChange);

    // Seed mtimes so the next poll doesn't fire a spurious "everything changed".
    std::error_code ec;
    for (const auto& file : fs::directory_iterator(entry.path, ec)) {
        if (!file.is_regular_file(ec)) continue;
        entry.mtimes[file.path().filename().string()] = file.last_write_time(ec);
    }

    LOG_TRACE("Watching '%s' (%zu file(s) seeded)",
        entry.path.c_str(), entry.mtimes.size());
    m_entries.push_back(std::move(entry));
}

void FileWatcher::update(FrameContext& ctx) {
    m_accumulator += ctx.deltaTime;
    if (m_accumulator < m_interval) return;
    m_accumulator = 0.0f;

    // Only emit the zone on poll ticks - skipping it on the no-op frames keeps
    // the Tracy timeline clean (this fires once every m_interval seconds).
    PROFILE_SCOPE("FileWatcher/Poll");
    for (auto& entry : m_entries) checkOne(entry);
}

void FileWatcher::checkOne(Entry& entry) {
    bool changed = false;
    std::error_code ec;

    if (!fs::is_directory(entry.path, ec)) return;

    for (const auto& file : fs::directory_iterator(entry.path, ec)) {
        if (!file.is_regular_file(ec)) continue;
        const std::string name = file.path().filename().string();
        const fs::file_time_type now = file.last_write_time(ec);
        if (ec) continue;

        auto it = entry.mtimes.find(name);
        if (it == entry.mtimes.end()) {
            entry.mtimes[name] = now;
            changed = true;
            continue;
        }
        if (now != it->second) {
            it->second = now;
            changed = true;
        }
    }

    if (changed && entry.onChange) {
        LOG_INFO("Change detected in '%s'", entry.path.c_str());
        entry.onChange();
    }
}

} // namespace Engine
