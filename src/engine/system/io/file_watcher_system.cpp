#define VKM_LOG_CATEGORY "FILEWATCH"

#include "system/io/file_watcher_system.h"

#include <system_error>
#include <unordered_set>

#include "logger.h"

#include "core/clock.h"
#include "debug/profiler.h"

namespace Engine {

namespace fs = std::filesystem;

namespace {

// Invoke `fn(directory_entry)` for each regular file directly in `dir`
// (non-recursive). Errors opening the directory or stat-ing an entry are
// swallowed (the entry is skipped) - polling must never throw out of the frame
// loop. Shared by the seed (watch) and detect (checkOne) passes so both filter
// identically.
template<typename F>
void forEachRegularFile(const std::string& dir, F&& fn) {
    std::error_code ec;
    for (const auto& file : fs::directory_iterator(dir, ec)) {
        if (!file.is_regular_file(ec)) continue;
        fn(file);
    }
}

} // namespace

FileWatcherSystem::FileWatcherSystem(float intervalSeconds)
    : m_interval(intervalSeconds) {}

void FileWatcherSystem::watch(std::string dirPath, OnChange onChange) {
    Entry entry;
    entry.path     = std::move(dirPath);
    entry.onChange = std::move(onChange);

    // Seed mtimes so the next poll doesn't fire a spurious "everything changed".
    forEachRegularFile(entry.path, [&](const fs::directory_entry& file) {
        std::error_code ec;
        const fs::file_time_type mtime = file.last_write_time(ec);
        if (ec) return;  // unreadable now; the first poll will pick it up as new
        entry.mtimes[file.path().filename().string()] = mtime;
    });

    LOG_TRACE("Watching '%s' (%zu file(s) seeded)",
        entry.path.c_str(), entry.mtimes.size());
    m_entries.push_back(std::move(entry));
}

void FileWatcherSystem::update(FrameContext& ctx) {
    m_accumulator += ctx.clock.getDeltaTime();
    if (m_accumulator < m_interval) return;
    m_accumulator -= m_interval;
    // A frame longer than the interval (hitch, breakpoint) would otherwise let
    // the accumulator pile up and never drain; clamp so we poll at most once per
    // frame without drifting upward.
    if (m_accumulator >= m_interval) m_accumulator = 0.0f;

    // Only emit the zone on poll ticks - skipping it on the no-op frames keeps
    // the Tracy timeline clean (this fires once every m_interval seconds).
    PROFILE_SCOPE("FileWatcherSystem/Poll");
    for (auto& entry : m_entries) checkOne(entry);
}

void FileWatcherSystem::checkOne(Entry& entry) {
    std::error_code ec;
    // A vanished watch directory is a different condition from file churn; leave
    // the tracked state intact and don't fire rather than reporting every file
    // as deleted.
    if (!fs::is_directory(entry.path, ec)) return;

    bool changed = false;
    std::unordered_set<std::string> seen;
    seen.reserve(entry.mtimes.size());

    forEachRegularFile(entry.path, [&](const fs::directory_entry& file) {
        std::error_code statEc;
        const fs::file_time_type now = file.last_write_time(statEc);
        if (statEc) return;
        const std::string name = file.path().filename().string();

        seen.insert(name);
        auto it = entry.mtimes.find(name);
        if (it == entry.mtimes.end()) {        // new file
            entry.mtimes.emplace(name, now);
            changed = true;
        } else if (now != it->second) {        // modified file
            it->second = now;
            changed = true;
        }
    });

    // Sweep: any tracked file not seen this tick was deleted. Evict it (so the
    // map can't grow without bound) and count the removal as a change.
    for (auto it = entry.mtimes.begin(); it != entry.mtimes.end();) {
        if (seen.count(it->first)) {
            ++it;
        } else {
            it = entry.mtimes.erase(it);
            changed = true;
        }
    }

    if (changed && entry.onChange) {
        LOG_INFO("Change detected in '%s'", entry.path.c_str());
        entry.onChange();
    }
}

} // namespace Engine
