#include "support.h"

#include <regex>

namespace {

namespace fs = std::filesystem;

// Where the documentation lives, relative to the repository root. Handed in at
// configure time because a test binary knows nothing about where it was built
// from - and a packaged SDK has no docs/ at all, which this reports rather than
// fails on.
#ifndef VKM_DOCS_DIR
#define VKM_DOCS_DIR ""
#endif

// Every .md under docs/, plus the top-level README.
std::vector<fs::path> documentFiles() {
    std::vector<fs::path> found;
    const fs::path root(VKM_DOCS_DIR);
    if (root.empty()) return found;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().extension() == ".md") found.push_back(it->path());
    }
    const fs::path readme = root.parent_path() / "README.md";
    if (fs::exists(readme, ec)) found.push_back(readme);

    std::sort(found.begin(), found.end());
    return found;
}

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A citation into a source file by line number: `some/file.h:120` or
// `file.cpp:12-34`. The first edit above line 120 makes it a lie, and nothing
// in the build ever reads it - so it is the one kind of reference that rots
// silently, and the manual carried ninety-eight of them.
void testTheManualCitesNoLineNumbers() {
    std::printf("What the manual points at:\n");

    const std::vector<fs::path> files = documentFiles();
    if (files.empty()) {
        std::printf("  (no docs/ in this tree - nothing to check)\n");
        return;
    }

    const std::regex citation(R"([A-Za-z0-9_./-]+\.(?:h|cpp|md|glsl|shader|cmake|json|txt):[0-9]+)");

    std::vector<std::string> offenders;
    for (const fs::path& file : files) {
        const std::string text = readAll(file);
        for (std::sregex_iterator it(text.begin(), text.end(), citation), end; it != end; ++it) {
            offenders.push_back(file.filename().string() + ": " + it->str());
        }
    }

    for (const std::string& offender : offenders) {
        std::printf("      %s\n", offender.c_str());
    }
    check("no document points at a source line number", offenders.empty());
}

} // namespace

void runDocsTests() {
    testTheManualCitesNoLineNumbers();
}
