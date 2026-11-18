// The Console follows the log file the logger writes: this session's lines, as they are written,
// a line said again at once folded into one with its count.

#define VKM_LOG_CATEGORY "EDITOR"

#include <filesystem>
#include <fstream>

#include "vkm_log/logger.h"

#include "panels/console/log_tail.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;

namespace {

std::filesystem::path logFile() { return scratch() / "test.log"; }

void append(const std::filesystem::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary | std::ios::app) << text;
}

} // namespace

TEST("the Console shows the lines written since the session began, as they are written") {
    LOG_WARNING("written before the editor opened");
    LogTail tail;
    tail.open(logFile(), std::filesystem::file_size(logFile()));
    CHECK(!tail.poll());
    CHECK(tail.lines().empty());

    LOG_WARNING_C("PHYSICS", "a body has no mass");
    CHECK(tail.poll());
    REQUIRE(tail.lines().size() == 1);
    CHECK(tail.lines()[0].text == "a body has no mass");
    CHECK(tail.lines()[0].category == "PHYSICS");
    CHECK(tail.lines()[0].level == Vkm::Log::Level::Warning);

    LOG_ERROR("a scene would not load");
    CHECK(tail.poll());
    REQUIRE(tail.lines().size() == 2);
    CHECK(tail.lines()[1].category == "EDITOR");
    CHECK(tail.lines()[1].level == Vkm::Log::Level::Error);
    CHECK(!tail.poll());
}

TEST("a line said again at once is one line with its count, and the counts are by severity") {
    LogTail tail;
    tail.open(logFile(), std::filesystem::file_size(logFile()));
    for (int i = 0; i < 3; ++i) LOG_WARNING("the device was lost");
    LOG_ERROR("the device was lost");
    LOG_WARNING("the device was lost");
    tail.poll();

    REQUIRE(tail.lines().size() == 3);
    CHECK(tail.lines()[0].repeats == 3);
    CHECK(tail.lines()[1].repeats == 1);
    CHECK(tail.counts()[static_cast<size_t>(Vkm::Log::Level::Warning)] == 2);
    CHECK(tail.counts()[static_cast<size_t>(Vkm::Log::Level::Error)] == 1);
}

TEST("a line half written waits for its end, and a log started again is read from its start") {
    const std::filesystem::path file = scratch() / "editor.log";
    writeText(file, "[2026-11-11 10:00:00.000] [VKM-ENGINE] [RENDER] [INFO] ready\n");
    LogTail tail;
    tail.open(file, 0);
    tail.poll();
    REQUIRE(tail.lines().size() == 1);
    CHECK(tail.lines()[0].text == "ready");
    CHECK(tail.lines()[0].time == "2026-11-11 10:00:00.000");

    append(file, "[2026-11-11 10:00:01.000] [VKM-ENGINE] [RENDER] [WARNING] slow fr");
    CHECK(!tail.poll());
    append(file, "ame\n");
    CHECK(tail.poll());
    REQUIRE(tail.lines().size() == 2);
    CHECK(tail.lines()[1].text == "slow frame");

    // A line of another shape is kept whole.
    writeText(file, "plain words\n");
    tail.poll();
    REQUIRE(tail.lines().size() == 3);
    CHECK(tail.lines()[2].text == "plain words");
    CHECK(tail.lines()[2].level == Vkm::Log::Level::Info);
}
