// A recoverable error, as a host surfaces it: once in its error list and once in the log.

#include <string>

#include "debug/error/engine_error_log.h"

#include "harness/check.h"
#include "harness/scratch.h"

using namespace Vkm::Test;
using namespace Vkm::Engine;

TEST("reportError reaches the host's error list once and the log once, a repeat counted, the oldest let go") {
    EngineErrorLog errors;
    setErrorSink(&errors);

    reportError("Scene", "level.json", "a mesh is missing");
    REQUIRE(errors.entries().size() == 1u);
    CHECK(errors.entries()[0].category == "Scene");
    CHECK(errors.entries()[0].source == "level.json");
    CHECK(errors.entries()[0].message == "a mesh is missing");
    CHECK(countInLog("a mesh is missing") == 1);

    // The same again at once is one entry seen twice; the log has both.
    reportError("Scene", "level.json", "a mesh is missing");
    CHECK(errors.entries().size() == 1u);
    CHECK(errors.entries()[0].repeatCount == 2u);
    CHECK(errors.totalPushed() == 1u);
    CHECK(countInLog("a mesh is missing") == 2);

    for (size_t i = 0; i < EngineErrorLog::CAPACITY + 5; ++i) reportError(
        "Load",
        "file " + std::to_string(i),
        "unreadable"
    );
    CHECK(errors.entries().size() == EngineErrorLog::CAPACITY);
    CHECK(errors.entries().front().source == "file 5");
    CHECK(errors.entries().back().source == "file " + std::to_string(EngineErrorLog::CAPACITY + 4));
    CHECK(errors.totalPushed() == 1u + EngineErrorLog::CAPACITY + 5);

    // With no list installed it is still logged.
    setErrorSink(nullptr);
    reportError("Audio", "device", "went away");
    CHECK(errors.entries().back().source != "device");
    CHECK(countInLog("went away") == 1);
}
