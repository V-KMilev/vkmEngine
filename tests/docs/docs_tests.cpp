#include "support.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace {

namespace fs = std::filesystem;

// Where the documentation lives, relative to the repository root, handed in at
// configure time. A packaged SDK has no docs/, which this reports rather than fails on.
#ifndef VKM_DOCS_DIR
#define VKM_DOCS_DIR ""
#endif

// The owner's own pages under docs/: .gitignore keeps them out of the repository, so
// they are not the manual.
bool isPrivateDocument(const fs::path& path) {
    return path.filename() == "roadmap.md"
        || path.generic_string().find("/notes/") != std::string::npos;
}

// Every .md under docs/ that is part of the manual, plus the top-level README.
std::vector<fs::path> documentFiles() {
    std::vector<fs::path> found;
    const fs::path root(VKM_DOCS_DIR);
    if (root.empty()) return found;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".md") continue;
        if (isPrivateDocument(it->path())) continue;
        found.push_back(it->path());
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

// Blank every comment and fill every literal - string, raw string, character - with
// `literal`, keeping the line structure.
std::string codeMasked(std::string text, char literal) {
    const auto fill = [&](size_t from, size_t to, char with) {
        for (size_t k = from; k <= to && k < text.size(); ++k) {
            if (text[k] != '\n') text[k] = with;
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (text.compare(i, 2, "//") == 0) {
            size_t end = text.find('\n', i);
            end = (end == std::string::npos) ? text.size() - 1 : end - 1;
            fill(i, end, ' ');
            i = end;
        } else if (text.compare(i, 2, "/*") == 0) {
            size_t end = text.find("*/", i + 2);
            end = (end == std::string::npos) ? text.size() - 1 : end + 1;
            fill(i, end, ' ');
            i = end;
        } else if (c == '"' && i > 0 && text[i - 1] == 'R') {
            // A raw string runs to )delimiter" whatever it holds, quotes and newlines included.
            const size_t open = text.find('(', i);
            const std::string close = ")" + text.substr(i + 1, open - i - 1) + "\"";
            size_t end = open == std::string::npos ? std::string::npos : text.find(close, open);
            end = (end == std::string::npos) ? text.size() - 1 : end + close.size() - 1;
            fill(i, end, literal);
            i = end;
        } else if (c == '"' || c == '\'') {
            // A quote inside a number separates digits (1'000, 0xFF'FF); after a word
            // prefix (U'A', L'x') it opens a literal.
            if (c == '\'' && i + 1 < text.size() && std::isalnum(static_cast<unsigned char>(text[i + 1]))) {
                const auto word = [](char ch) {
                    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
                };
                size_t token = i;
                while (token > 0 && word(text[token - 1])) --token;
                if (token < i && std::isdigit(static_cast<unsigned char>(text[token]))) continue;
            }
            size_t end = i + 1;
            while (end < text.size() && text[end] != c && text[end] != '\n') {
                if (text[end] == '\\') ++end;
                ++end;
            }
            fill(i, end, literal);
            i = end;
        }
    }
    return text;
}

// Blank every literal and comment, keeping the line structure, so checks reading what
// source says never see a name inside a comment or string.
std::string codeOnly(std::string text) {
    return codeMasked(std::move(text), ' ');
}

// Blank every fenced block and inline code span of a Markdown page, keeping the line
// structure, so what is left is prose. Link syntax inside code - a lambda's `[]()` - is
// not a link.
std::string proseOnly(std::string text) {
    bool fenced = false;
    size_t lineStart = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] != '\n') continue;
        const bool fence = text.compare(lineStart, 3, "```") == 0;
        if (fence) fenced = !fenced;
        if (fence || fenced) {
            for (size_t k = lineStart; k < i; ++k) text[k] = ' ';
        } else {
            for (size_t k = lineStart; k < i; ++k) {
                if (text[k] != '`') continue;
                const size_t close = text.find('`', k + 1);
                if (close == std::string::npos || close > i) break;
                for (size_t m = k; m <= close; ++m) text[m] = ' ';
                k = close;
            }
        }
        lineStart = i + 1;
    }
    return text;
}

// A citation by line number - `some/file.h:120`, `file.cpp:12-34` - becomes a lie at
// the first edit above it, and nothing in the build reads it: it rots silently.
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

// The files of @p files with a byte past ASCII, each named with its first such line.
std::vector<std::string> nonAsciiLines(const std::vector<fs::path>& files) {
    std::vector<std::string> offenders;
    for (const fs::path& file : files) {
        const std::string text = readAll(file);
        int line = 1;
        for (const char c : text) {
            if (c == '\n') {
                ++line;
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x80) continue;
            offenders.push_back(file.filename().string() + ":" + std::to_string(line));
            break;   // one line per file is enough to find it
        }
    }
    return offenders;
}

// The manual held to the source's charset. code-style.md makes ASCII an absolute and
// the manual writes " - " for a dash: one convention spelled two ways is drift
// (review.md's "one concept, several mechanisms"), and a fancy dash renders as a box
// where the font lacks it and cannot be grepped for by typing it.
void testTheManualIsAscii() {
    std::printf("What characters the manual is written in:\n");

    const std::vector<fs::path> files = documentFiles();
    if (files.empty()) {
        std::printf("  (no docs/ in this tree - nothing to check)\n");
        return;
    }

    const std::vector<std::string> offenders = nonAsciiLines(files);
    for (const std::string& one : offenders) {
        std::printf("      %s is not ASCII\n", one.c_str());
    }
    check("every page is ASCII", offenders.empty());
}

// First-party CMake files and Python tools, held to the code's rules (code-style.md
// 6.2); a project's build/ and dist/ are generated.
std::vector<fs::path> buildAndToolFiles(const fs::path& engineRoot) {
    std::vector<fs::path> found;
    std::error_code ec;
    for (const char* const one : {"CMakeLists.txt", "modules/CMakeLists.txt"}) {
        if (fs::exists(engineRoot / one, ec)) found.push_back(engineRoot / one);
    }
    for (const char* const root : {"cmake", "app", "src", "tests", "examples", "templates", "tools"}) {
        const fs::path dir = engineRoot / root;
        if (!fs::exists(dir, ec)) continue;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            const std::string where = it->path().generic_string();
            const bool generated = where.find("/build/") != std::string::npos
                || where.find("/dist/") != std::string::npos;
            if (generated) continue;
            const std::string name = it->path().filename().string();
            const std::string ext  = it->path().extension().string();
            const bool cmake = name == "CMakeLists.txt" || ext == ".cmake" || ext == ".in";
            bool python = ext == ".py";
            if (!python && root == std::string("tools") && ext.empty()) {
                std::ifstream in(it->path());
                std::string first;
                std::getline(in, first);
                python = first.rfind("#!/usr/bin/env python", 0) == 0;
            }
            if (cmake || python) found.push_back(it->path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

// A non-ASCII character reads differently in an editor on another code page, and in a
// literal it is whatever bytes the compiler took the file to be. An escape says which.
void testTheSourceIsAscii() {
    std::printf("What characters the code is written in:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    std::vector<fs::path> files;
    for (const char* root : {"src", "app", "tests", "templates", "shaders", "cmake"}) {
        const fs::path dir = engineRoot / root;
        if (!fs::exists(dir, ec)) continue;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            const std::string ext  = it->path().extension().string();
            const std::string name = it->path().filename().string();
            if (ext == ".h" || ext == ".cpp" || ext == ".shader" || ext == ".glsl" || ext == ".cmake"
                || name == "CMakeLists.txt") {
                files.push_back(it->path());
            }
        }
    }
    const std::vector<fs::path> builds = buildAndToolFiles(engineRoot);
    files.insert(files.end(), builds.begin(), builds.end());
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    if (files.empty()) {
        std::printf("  (no sources in this tree - nothing to check)\n");
        return;
    }

    const std::vector<std::string> offenders = nonAsciiLines(files);
    std::printf("      %zu source file(s)\n", files.size());
    for (const std::string& one : offenders) {
        std::printf("      %s is not ASCII\n", one.c_str());
    }
    check("every source file is ASCII", offenders.empty());
}

// Where a documented path may be rooted: the manual writes a header as an `#include`
// does, so `ecs/scene.h` and `src/engine/ecs/scene.h` are both correct.
const std::vector<std::string>& includeRoots() {
    static const std::vector<std::string> roots = {
        "", "src/engine/", "src/backend/opengl/", "src/editor/", "src/tools/",
        "src/", "app/", "shaders/",
    };
    return roots;
}

bool resolvesSomewhere(const fs::path& engineRoot, const std::string& rel) {
    std::error_code ec;
    for (const std::string& root : includeRoots()) {
        if (fs::exists(engineRoot / (root + rel), ec)) return true;
    }
    return false;
}

// Every source file name in the tree. A page grouping files under their folder's
// heading (as physics.md does) writes bare names; the claim checked is that the file
// exists, not where, so ambiguity is fine.
const std::set<std::string>& sourceFileNames(const fs::path& engineRoot) {
    static std::set<std::string> names;
    static bool built = false;
    if (built) return names;
    built = true;

    std::error_code ec;
    for (const char* const root : {"src", "app", "examples", "tests", "tools", "shaders", "templates"}) {
        const fs::path dir = engineRoot / root;
        if (!fs::exists(dir, ec)) continue;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file()) names.insert(it->path().filename().string());
        }
    }
    return names;
}

// A path with a placeholder - `library/<type>/<uid>.json`, `examples/<project>/src/` -
// names a shape, not a file.
bool isPlaceholder(const std::string& text) {
    return text.find('<') != std::string::npos || text.find('*') != std::string::npos;
}

// Every `[label](target)` in the manual's prose, resolved against the linking page.
// These rot when a page moves, leaving the reader nothing; an empty target is the same
// failure written down at once.
void testTheManualLinksResolve() {
    std::printf("Where the manual links:\n");

    const std::vector<fs::path> files = documentFiles();
    if (files.empty()) {
        std::printf("  (no docs/ in this tree - nothing to check)\n");
        return;
    }

    const std::regex link(R"(\]\(([^)\s]*)\))");

    std::vector<std::string> broken;
    for (const fs::path& file : files) {
        const std::string text = proseOnly(readAll(file));
        for (std::sregex_iterator it(text.begin(), text.end(), link), end; it != end; ++it) {
            std::string target = (*it)[1].str();
            if (target.empty()) {   // `[label]()` renders as a link to nowhere
                broken.push_back(file.filename().string() + " -> ()");
                continue;
            }
            if (target.rfind("http", 0) == 0 || target.rfind("mailto", 0) == 0) continue;
            const size_t anchor = target.find('#');
            if (anchor != std::string::npos) target = target.substr(0, anchor);
            if (target.empty()) continue;   // a pure #anchor into the same page

            std::error_code ec;
            if (!fs::exists(file.parent_path() / target, ec)) {
                broken.push_back(file.filename().string() + " -> " + target);
            }
        }
    }

    for (const std::string& one : broken) {
        std::printf("      %s\n", one.c_str());
    }
    check("every link in the manual resolves", broken.empty());
}

// A page's heading anchors as GitHub makes them: lowercased, all but letters, digits,
// spaces, hyphens and underscores dropped, spaces to hyphens, repeats given -1, -2.
// Headings inside a code fence are code.
std::set<std::string> headingAnchors(const std::string& text) {
    std::set<std::string> anchors;
    std::map<std::string, int> seen;
    std::istringstream lines(text);
    std::string line;
    bool fenced = false;
    while (std::getline(lines, line)) {
        if (line.rfind("```", 0) == 0) {
            fenced = !fenced;
            continue;
        }
        if (fenced || line.empty() || line[0] != '#') continue;
        const size_t start = line.find_first_not_of('#');
        if (start == std::string::npos || line[start] != ' ') continue;

        std::string slug;
        for (const char c : line.substr(start + 1)) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u) || c == '-' || c == '_') slug += static_cast<char>(std::tolower(u));
            else if (c == ' ') slug += '-';
        }
        const int repeat = seen[slug]++;
        anchors.insert(repeat == 0 ? slug : slug + "-" + std::to_string(repeat));
    }
    return anchors;
}

// Every `[label](page.md#anchor)` names a heading the page has. A changed heading breaks
// its anchors silently: the page opens at the top, not the promised section.
void testTheManualAnchorsResolve() {
    std::printf("Where the manual's anchors point:\n");

    const std::vector<fs::path> files = documentFiles();
    if (files.empty()) {
        std::printf("  (no docs/ in this tree - nothing to check)\n");
        return;
    }

    const std::regex link(R"(\]\(([^)\s#]*)#([^)\s]+)\))");

    std::map<fs::path, std::set<std::string>> anchorsOf;
    std::vector<std::string> broken;
    for (const fs::path& file : files) {
        const std::string text = readAll(file);
        for (std::sregex_iterator it(text.begin(), text.end(), link), end; it != end; ++it) {
            const std::string target = (*it)[1].str();
            const std::string anchor = (*it)[2].str();
            if (target.rfind("http", 0) == 0) continue;
            if (!target.empty() && fs::path(target).extension() != ".md") continue;

            std::error_code ec;
            const fs::path page = target.empty()
                ? file : fs::weakly_canonical(file.parent_path() / target, ec);
            if (!fs::exists(page, ec)) continue;   // the link check reports it
            auto found = anchorsOf.find(page);
            if (found == anchorsOf.end()) {
                found = anchorsOf.emplace(page, headingAnchors(readAll(page))).first;
            }
            if (found->second.count(anchor) == 0) {
                broken.push_back(
                    file.filename().string() + " -> " + (target.empty() ? std::string{} : target)
                        + "#" + anchor
                );
            }
        }
    }

    for (const std::string& one : broken) {
        std::printf("      %s\n", one.c_str());
    }
    check("every anchor in the manual names a heading", broken.empty());
}

// Every backticked token that looks like a repository path. A renamed or moved file
// leaves a reference that reads as authoritative and points at nothing.
void testTheManualNamesRealFiles() {
    std::printf("What files the manual names:\n");

    const std::vector<fs::path> files = documentFiles();
    if (files.empty()) {
        std::printf("  (no docs/ in this tree - nothing to check)\n");
        return;
    }
    const fs::path engineRoot = fs::path(VKM_ENGINE_DIR);

    // Backticked, containing a slash, ending in an extension this repo uses.
    const std::regex quoted(R"(`([^`]+)`)");
    const std::regex pathish(R"(^[A-Za-z0-9_./<>*-]+\.(h|cpp|md|glsl|shader|py|dox)$)");

    std::vector<std::string> missing;
    for (const fs::path& file : files) {
        const std::string text = readAll(file);
        for (std::sregex_iterator it(text.begin(), text.end(), quoted), end; it != end; ++it) {
            const std::string token = (*it)[1].str();
            if (token.find('/') == std::string::npos) continue;
            if (!std::regex_match(token, pathish)) continue;
            if (isPlaceholder(token)) continue;

            // `narrowphase.h/.cpp` is the manual's shorthand for the pair; the header is
            // what it must be right about.
            std::string path = token;
            const std::string pair = ".h/.cpp";
            if (path.size() > pair.size()
                && path.compare(path.size() - pair.size(), pair.size(), pair) == 0) {
                path = path.substr(0, path.size() - pair.size()) + ".h";
            }
            if (resolvesSomewhere(engineRoot, path)) continue;
            if (fs::exists(file.parent_path() / path)) continue;
            if (path.find('/') == std::string::npos
                && sourceFileNames(engineRoot).count(path) != 0) continue;
            missing.push_back(file.filename().string() + ": " + token);
        }
    }

    for (const std::string& one : missing) {
        std::printf("      %s\n", one.c_str());
    }
    check("every source file the manual names exists", missing.empty());
}

// The pass list is hand-ordered and load-bearing; design.md sends readers to
// rendering.md's table for what each pass owes the next. The table copies the
// registration list, so it can drift, and a stale one puts a new pass before its inputs.
void testTheManualsPassTableIsTheRegistrationOrder() {
    std::printf("What order the manual says the passes run in:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path backend = engineRoot / "src/backend/opengl/gl_backend.cpp";
    const fs::path page    = engineRoot / "docs/reference/rendering.md";

    std::error_code ec;
    if (!fs::exists(backend, ec) || !fs::exists(page, ec)) {
        std::printf("  (no source or no docs/ in this tree - nothing to check)\n");
        return;
    }

    std::vector<std::string> registered;
    {
        const std::string text = readAll(backend);
        const std::regex row(R"RX(m_passes\.push_back\(\{"([A-Za-z]+)")RX");
        for (std::sregex_iterator it(text.begin(), text.end(), row), end; it != end; ++it) {
            registered.push_back((*it)[1].str());
        }
    }

    // Rows are numbered - "| <n> | <Name>", the name optionally bold - and nothing else
    // in the page is shaped like that.
    std::vector<std::string> documented;
    {
        const std::string text = readAll(page);
        const std::regex row(R"RX(\n\|\s*[0-9]+\s*\|\s*\**([A-Za-z]+))RX");
        for (std::sregex_iterator it(text.begin(), text.end(), row), end; it != end; ++it) {
            documented.push_back((*it)[1].str());
        }
    }

    std::printf("      %zu registered, %zu documented\n", registered.size(), documented.size());
    for (size_t i = 0; i < registered.size() && i < documented.size(); ++i) {
        if (registered[i] != documented[i]) {
            std::printf(
                "      row %zu: registers %s, documents %s\n",
                i + 1,
                registered[i].c_str(),
                documented[i].c_str()
            );
        }
    }
    check(
        "the manual's pass table is the registration order",
        !registered.empty() && registered == documented
    );
}

// The component names an X-macro list's rows carry, in order. A list runs from its
// #define to the first blank line, whichever one is asked for.
std::vector<std::string> macroRows(const fs::path& file, const std::string& macro, const std::regex& row) {
    std::vector<std::string> names;
    const std::string text = readAll(file);
    const size_t begin = text.find("#define " + macro);
    const size_t end   = begin == std::string::npos ? begin : text.find("\n\n", begin);
    if (begin == std::string::npos || end == std::string::npos) return names;

    const std::string block = text.substr(begin, end - begin);
    for (std::sregex_iterator it(block.begin(), block.end(), row), stop; it != stop; ++it) {
        names.push_back((*it)[1].str());
    }
    return names;
}

// Every component the scene format carries, against the ECS page's table. The list in
// component_serializer.h is where a component joins the format; one missing from the
// table can be saved and not looked up.
void testTheManualDocumentsEveryComponent() {
    std::printf("What components the manual lists:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path header = engineRoot / "src/engine/io/scene/component_serializer.h";
    const fs::path page   = engineRoot / "docs/reference/ecs.md";

    std::error_code ec;
    if (!fs::exists(header, ec) || !fs::exists(page, ec)) {
        std::printf("  (no source or no docs/ in this tree - nothing to check)\n");
        return;
    }

    const std::vector<std::string> names =
        macroRows(header, "VKM_SCENE_COMPONENTS", std::regex(R"RX(\n\s+[PRE]\((\w+),)RX"));
    const std::string doc = readAll(page);

    std::vector<std::string> undocumented;
    const size_t total = names.size();
    for (const std::string& name : names) {
        if (doc.find("`" + name + "`") == std::string::npos) undocumented.push_back(name);
    }

    std::printf("      %zu in the scene format\n", total);
    for (const std::string& one : undocumented) {
        std::printf("      %s\n", one.c_str());
    }
    check("every component in the scene format is in the ECS page", total > 0 && undocumented.empty());
}

// Every `.h` / `.cpp` under src/ and app/, read as text: the claims below live in comments.
std::vector<fs::path> sourceFiles(const fs::path& engineRoot) {
    std::vector<fs::path> found;
    std::error_code ec;
    for (const char* const root : {"src", "app"}) {
        const fs::path dir = engineRoot / root;
        if (!fs::exists(dir, ec)) continue;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            const std::string ext = it->path().extension().string();
            if (ext == ".h" || ext == ".cpp") found.push_back(it->path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

// Every `.h` / `.cpp` under tests/, held to the same linkage rule: suites link into one
// binary, so a helper outside an anonymous namespace can collide with another suite's.
std::vector<fs::path> suiteFiles(const fs::path& engineRoot) {
    std::vector<fs::path> found;
    std::error_code ec;
    const fs::path dir = engineRoot / "tests";
    if (!fs::exists(dir, ec)) return found;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string ext = it->path().extension().string();
        if (ext == ".h" || ext == ".cpp") found.push_back(it->path());
    }
    std::sort(found.begin(), found.end());
    return found;
}

// Every `.h` / `.cpp` of examples/ and templates/: the code a game developer copies
// first, held to the engine's rules. A project's build/ holds CMake's probe sources,
// which are not ours.
std::vector<fs::path> projectSourceFiles(const fs::path& engineRoot) {
    std::vector<fs::path> found;
    std::error_code ec;
    for (const char* const root : {"examples", "templates"}) {
        const fs::path dir = engineRoot / root;
        if (!fs::exists(dir, ec)) continue;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            if (it->path().generic_string().find("/build/") != std::string::npos) continue;
            const std::string ext = it->path().extension().string();
            if (ext == ".h" || ext == ".cpp") found.push_back(it->path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

// The engine's sources and the projects', which the style checks below read alike.
std::vector<fs::path> engineAndProjectFiles(const fs::path& engineRoot) {
    std::vector<fs::path> files = sourceFiles(engineRoot);
    const std::vector<fs::path> projects = projectSourceFiles(engineRoot);
    files.insert(files.end(), projects.begin(), projects.end());
    return files;
}

// Fold every newline and its comment leader into a space, so a wrapped claim reads as
// one sentence.
std::string flattenComments(const std::string& text) {
    static const std::regex fold(R"RX(\n\s*(?://+|\*)?[ \t]*)RX");
    return std::regex_replace(text, fold, " ");
}

// Does `name` appear anywhere under `target` - one shader, or a directory of them?
bool namedUnder(const fs::path& target, const std::string& name) {
    std::error_code ec;
    if (!fs::exists(target, ec)) return false;
    if (fs::is_regular_file(target, ec)) return readAll(target).find(name) != std::string::npos;
    for (fs::recursive_directory_iterator it(target, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && readAll(it->path()).find(name) != std::string::npos) return true;
    }
    return false;
}

// The lines between `<keyword> <name> {` and the `};` closing it at column zero. Enough
// for flat settings structs; a nested type would end the slice early.
std::string blockBody(const std::string& text, const std::string& opener) {
    const size_t begin = text.find(opener);
    if (begin == std::string::npos) return {};
    const size_t brace = text.find('{', begin);
    if (brace == std::string::npos) return {};
    const size_t end = text.find("\n}", brace);
    if (end == std::string::npos) return {};
    return text.substr(brace, end - brace);
}

// Data members of a flat struct: four-space indent, type, name, initializer. Doc-block
// lines start with an asterisk and comments with a slash, so neither reads as a member.
std::vector<std::string> structFields(const fs::path& file, const std::string& name) {
    std::vector<std::string> fields;
    const std::string body = blockBody(readAll(file), "struct " + name);
    const std::regex member(R"RX(^    ([A-Za-z_][A-Za-z0-9_:]*)[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*=)RX");

    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch m;
        if (std::regex_search(line, m, member)) fields.push_back(m[2].str());
    }
    return fields;
}

// The keys a visitX(...) function names, in `f("key", r.key)` form.
std::vector<std::string> visitedKeys(const fs::path& file, const std::string& function) {
    std::vector<std::string> keys;
    const std::string body = blockBody(readAll(file), function + "(");
    const std::regex key(R"RX(f\("(\w+)")RX");
    for (std::sregex_iterator it(body.begin(), body.end(), key), stop; it != stop; ++it) {
        keys.push_back((*it)[1].str());
    }
    return keys;
}

// Every field of RenderView, against the contract table documenting it. rendering.md
// calls the struct "the entire engine-to-backend interface" and hand-copies its fields,
// so the table can keep a row for a field long gone.
//
// The struct is the authority. A row may name several fields (the viewport rect is
// one row for four), so each member must appear somewhere in the table; rows and
// members need not line up one for one.
void testTheRenderViewContractTableIsTheStruct() {
    std::printf("What the manual says RenderView carries:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path header = engineRoot / "src/engine/system/render/render_view.h";
    const fs::path page   = engineRoot / "docs/reference/rendering.md";

    std::error_code ec;
    if (!fs::exists(header, ec) || !fs::exists(page, ec)) {
        std::printf("  (no source or no docs/ in this tree - nothing to check)\n");
        return;
    }

    // Up to the struct's first access specifier: past it are build methods the table
    // does not document.
    std::string body = blockBody(readAll(header), "struct RenderView");
    const size_t methods = body.find("\n    public:");
    if (methods != std::string::npos) body.erase(methods);

    // A member is an indented declaration ending in `;`, initialized or not, borrowed
    // pointers included. Doc-block and comment lines start with an asterisk or a slash,
    // so neither matches.
    const std::regex member(
        R"RX(^    ([A-Za-z_][A-Za-z0-9_:<>, *&]*?)\s+([A-Za-z_]\w*)\s*(?:=[^;]*|\{[^;]*\})?;)RX"
    );

    // The contract table alone: the prose below names fields too, and a stale row beside
    // updated prose is the drift this catches.
    const std::string whole = readAll(page);
    const size_t tableAt = whole.find("| Field | Type | Notes |");
    if (tableAt == std::string::npos) {
        check("the RenderView contract table was found", false);
        return;
    }
    const size_t tableEnd = whole.find("\n\n", tableAt);
    const std::string doc = whole.substr(tableAt, tableEnd - tableAt);

    std::vector<std::string> undocumented;
    size_t total = 0;

    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch m;
        if (!std::regex_search(line, m, member)) continue;
        const std::string name = m[2].str();
        ++total;
        if (doc.find("`" + name + "`") == std::string::npos) undocumented.push_back(name);
    }

    std::printf("      %zu field(s) on the contract\n", total);
    for (const std::string& one : undocumented) {
        std::printf("      %s is on RenderView and in no row of the table\n", one.c_str());
    }
    check("every RenderView field is documented", total > 10 && undocumented.empty());
}

// Every identifier the tree declares or uses in code (comments and literals blanked),
// across the engine and the examples, which samples are written against. Not
// templates/: its `Game` is the namespace the manual gives a reader's own project. By
// hand, not regex: over megabytes of source a std::regex is an order of magnitude
// slower. Built once and shared.
const std::set<std::string>& identifiersInCode(const fs::path& engineRoot) {
    static std::set<std::string> names;
    static bool built = false;
    if (built) return names;
    built = true;

    for (const fs::path& file : engineAndProjectFiles(engineRoot)) {
        if (file.generic_string().find("/templates/") != std::string::npos) continue;
        const std::string text = codeOnly(readAll(file));
        for (size_t i = 0; i < text.size();) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (!std::isalpha(c) && c != '_') {
                ++i;
                continue;
            }
            size_t j = i + 1;
            while (j < text.size()) {
                const unsigned char d = static_cast<unsigned char>(text[j]);
                if (!std::isalnum(d) && d != '_') break;
                ++j;
            }
            names.insert(text.substr(i, j - i));
            i = j;
        }
    }
    return names;
}

// What each class, struct, union, enum and namespace holds: every identifier inside
// its braces wherever opened, plus every `Foo::bar` code writes (out-of-line
// definitions, X-macro enumerators). Over-generous on purpose - nested types count for
// their parent - since it exists to catch a member written under a scope that never
// had it. By hand, for the reason identifiersInCode gives.
const std::map<std::string, std::set<std::string>>& membersOfScopes(const fs::path& engineRoot) {
    static std::map<std::string, std::set<std::string>> scopes;
    static bool built = false;
    if (built) return scopes;
    built = true;

    const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    const auto identifiers = [&](
        const std::string& text,
        size_t from,
        size_t to,
        std::set<std::string>& found
    ) {
        for (size_t i = from; i < to;) {
            if (!word(text[i]) || std::isdigit(static_cast<unsigned char>(text[i]))) {
                ++i;
                continue;
            }
            size_t j = i + 1;
            while (j < to && word(text[j])) ++j;
            found.insert(text.substr(i, j - i));
            i = j;
        }
    };

    std::vector<fs::path> files = engineAndProjectFiles(engineRoot);
    const std::vector<fs::path> suites = suiteFiles(engineRoot);
    files.insert(files.end(), suites.begin(), suites.end());
    for (const fs::path& file : files) {
        if (file.generic_string().find("/templates/") != std::string::npos) continue;
        const std::string text = codeOnly(readAll(file));

        for (const char* keyword : {"class ", "struct ", "union ", "enum ", "namespace "}) {
            const size_t length = std::strlen(keyword);
            for (size_t at = text.find(keyword); at != std::string::npos; at = text.find(keyword, at + 1)) {
                if (at > 0 && word(text[at - 1])) continue;
                // The head runs to the body's brace; a `;` or `(` first means a
                // declaration, a parameter or a template's `class T`.
                const size_t brace = text.find_first_of("{;()", at + length);
                if (brace == std::string::npos || text[brace] != '{') continue;
                std::string name;
                for (size_t i = at + length; i < brace;) {
                    if (!word(text[i])) {
                        // `::` joins a nested name; a lone `:` starts a base list.
                        if (text.compare(i, 2, "::") == 0) {
                            i += 2;
                            continue;
                        }
                        if (text[i] == ':') break;
                        ++i;
                        continue;
                    }
                    size_t j = i;
                    while (j < brace && word(text[j])) ++j;
                    const std::string token = text.substr(i, j - i);
                    if (token != "class" && token != "final" && token != "alignas"
                        && token.find("_API") == std::string::npos) {
                        name = token;
                    }
                    i = j;
                }
                if (name.empty()) continue;
                int depth = 1;
                size_t close = brace + 1;
                for (; close < text.size() && depth > 0; ++close) {
                    if (text[close] == '{') ++depth;
                    else if (text[close] == '}') --depth;
                }
                identifiers(text, brace + 1, close, scopes[name]);
            }
        }

        for (size_t at = text.find("::"); at != std::string::npos; at = text.find("::", at + 2)) {
            size_t begin = at;
            while (begin > 0 && word(text[begin - 1])) --begin;
            size_t end = at + 2;
            if (end < text.size() && text[end] == '~') ++end;
            const size_t memberBegin = end;
            while (end < text.size() && word(text[end])) ++end;
            if (begin == at || end == memberBegin) continue;
            scopes[text.substr(begin, at - begin)].insert(text.substr(memberBegin, end - memberBegin));
        }
    }
    return scopes;
}

// Whether @p scope has @p member, as far as membersOfScopes can say: a scope never seen
// opened - an alias, a variable - answers by the member existing anywhere.
bool scopeHas(const fs::path& engineRoot, const std::string& scope, const std::string& member) {
    const auto& scopes = membersOfScopes(engineRoot);
    const auto found = scopes.find(scope);
    if (found == scopes.end()) return identifiersInCode(engineRoot).count(member) != 0;
    return found->second.count(member) != 0;
}

// Every `Scope::member` a doc's cpp sample writes must name something the source
// declares. Samples are what readers copy, so a missing name costs a compile and trust,
// and fenced blocks escape the audits over backticked prose.
//
// Literals and comments are stripped before collecting names, which makes it exact: a
// name like `Easing::easeInOutSine` may exist only as a string, where grep would vouch
// and the compiler would not. An undeclared scope is a project's own type -
// `Game::CubeSpinner` - and is skipped whole, with no exception list to maintain.
void testEveryNameInADocSampleExists() {
    std::printf("What the manual's code samples name:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const std::vector<fs::path> pages = documentFiles();
    if (pages.empty()) {
        std::printf("  (no docs/ in this tree - nothing to check)\n");
        return;
    }

    const std::set<std::string>& declared = identifiersInCode(engineRoot);

    const std::regex qualified(R"RX(\b([A-Z]\w*)::(\w+)\b)RX");

    std::vector<std::string> unknown;
    size_t checked = 0;

    for (const fs::path& page : pages) {
        const std::string text = readAll(page);
        for (size_t at = text.find("```cpp"); at != std::string::npos; at = text.find("```cpp", at + 1)) {
            const size_t begin = text.find('\n', at);
            if (begin == std::string::npos) break;
            const size_t end = text.find("```", begin);
            if (end == std::string::npos) break;

            const std::string block = codeOnly(text.substr(begin, end - begin));
            for (std::sregex_iterator it(block.begin(), block.end(), qualified), last; it != last; ++it) {
                const std::string scope  = (*it)[1].str();
                const std::string member = (*it)[2].str();
                if (declared.count(scope) == 0) continue;   // a project's own type
                ++checked;
                if (!scopeHas(engineRoot, scope, member)) {
                    unknown.push_back(page.filename().string() + ": " + scope + "::" + member);
                }
            }
        }
    }

    std::printf("      %zu qualified name(s) in the samples\n", checked);
    for (const std::string& one : unknown) {
        std::printf("      %s names nothing the source declares\n", one.c_str());
    }
    check("every name a code sample writes exists", checked > 20 && unknown.empty());
}

// The same question of the manual's prose: every backticked `Scope::member` -
// `EditorState::showMaterialEditor`, `Clock::getFixedAlpha()` - names a member the
// source has; a renamed member leaves the sentence reading as before. Cheap rather than
// exact: the member must be an identifier somewhere in code, not on that type. An
// undeclared scope - a project's type, a placeholder - is skipped, as in the samples.
void testEveryQualifiedNameInTheManualExists() {
    std::printf("What the manual's prose says a type has:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const std::vector<fs::path> pages = documentFiles();
    if (pages.empty()) {
        std::printf("  (no docs/ in this tree - nothing to check)\n");
        return;
    }
    const std::set<std::string>& declared = identifiersInCode(engineRoot);

    // Inside one code span, a scope then a member, optionally called.
    const std::regex span(R"RX(`([^`\n]+)`)RX");
    const std::regex qualified(R"RX(\b([A-Z]\w*)::([A-Za-z_]\w*)\b)RX");

    std::vector<std::string> unknown;
    size_t checked = 0;
    for (const fs::path& page : pages) {
        const std::string text = readAll(page);
        for (std::sregex_iterator it(text.begin(), text.end(), span), last; it != last; ++it) {
            const std::string code = (*it)[1].str();
            // A quoted string in a span - a profiler zone's name - is text, not a member.
            if (!code.empty() && code.front() == '"') continue;
            for (std::sregex_iterator q(code.begin(), code.end(), qualified), end; q != end; ++q) {
                const std::string scope  = (*q)[1].str();
                const std::string member = (*q)[2].str();
                if (declared.count(scope) == 0) continue;
                ++checked;
                if (!scopeHas(engineRoot, scope, member)) {
                    unknown.push_back(page.filename().string() + ": " + scope + "::" + member);
                }
            }
        }
    }

    std::printf("      %zu qualified name(s) in the prose\n", checked);
    for (const std::string& one : unknown) {
        std::printf("      %s names nothing the source declares\n", one.c_str());
    }
    check("every qualified name the prose writes exists", checked > 100 && unknown.empty());
}

// HierarchyOperations is the sanctioned way to touch the entity graph, and hierarchy.md's
// tables are how a reader finds what is in it ("where every walk over it belongs"). A
// function missing from the tables is invisible, and the next reader rewrites the walk
// by hand, unbounded - the failure the namespace exists to prevent.
void testTheHierarchyApiTablesAreTheHeader() {
    std::printf("What the manual says HierarchyOperations offers:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path header = engineRoot / "src/engine/ecs/hierarchy_operations.h";
    const fs::path page   = engineRoot / "docs/reference/hierarchy.md";

    std::error_code ec;
    if (!fs::exists(header, ec) || !fs::exists(page, ec)) {
        std::printf("  (no source or no docs/ in this tree - nothing to check)\n");
        return;
    }

    // The whole header, not a brace-matched block: every declaration in it is the
    // namespace's, and the first inline body's brace would end a block match early.
    std::string body = readAll(header);
    // detail:: is the bounds checking rather than the API, so it is cut out.
    const size_t detailAt = body.find("namespace detail {");
    const size_t detailEnd = body.find("} // namespace detail");
    if (detailAt != std::string::npos && detailEnd != std::string::npos && detailEnd > detailAt) {
        body.erase(detailAt, detailEnd - detailAt);
    }

    // A namespace-scope declaration: return type at column zero, a name, an open paren.
    // Templates put the return type on the line after `template<...>`, so both match.
    const std::regex declared(R"RX(^(?:inline\s+)?[A-Za-z_][A-Za-z0-9_:<>, ]*[ >&*]([A-Za-z_]\w*)\s*\()RX");

    // The tables alone: the prose names functions too, and a row missing while the
    // prose mentions it is the drift this catches.
    std::string tables;
    {
        std::istringstream rowsIn(readAll(page));
        std::string row;
        while (std::getline(rowsIn, row)) {
            if (row.rfind("| `", 0) == 0) tables += row + "\n";
        }
    }

    std::vector<std::string> undocumented;
    size_t total = 0;

    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch m;
        if (!std::regex_search(line, m, declared)) continue;
        const std::string name = m[1].str();
        if (name == "if" || name == "for" || name == "while" || name == "return") continue;
        ++total;
        if (tables.find("`" + name + "(") == std::string::npos
            && tables.find("`" + name + "<") == std::string::npos) {
            undocumented.push_back(name);
        }
    }

    std::printf("      %zu function(s) in the namespace\n", total);
    for (const std::string& one : undocumented) {
        std::printf("      %s is in HierarchyOperations and in no row of the page\n", one.c_str());
    }
    check("every HierarchyOperations function is in a table", total >= 8 && undocumented.empty());
}

// RenderSettings persists in two homes on purpose: the shipped look goes to
// project.json via visitShippedRenderFields, the editor's view state to the user's
// settings via visitRenderFields. Adding a member reaches neither, so a new setting
// silently fails to persist. Both halves are checked: every field is somewhere, and
// none in both, since a field in two files reads back from whichever loads last.
void testEveryRenderSettingIsPersistedExactlyOnce() {
    std::printf("Where a render setting is written down:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path model  = engineRoot / "src/engine/system/render/render_settings.h";
    const fs::path editor = engineRoot / "src/editor/editor_settings.cpp";

    std::error_code ec;
    if (!fs::exists(model, ec) || !fs::exists(editor, ec)) {
        std::printf("  (no source in this tree - nothing to check)\n");
        return;
    }

    const std::vector<std::string> fields  = structFields(model, "RenderSettings");
    const std::vector<std::string> shipped = visitedKeys(model, "visitShippedRenderFields");
    const std::vector<std::string> view    = visitedKeys(editor, "visitRenderFields");

    const std::set<std::string> inShipped(shipped.begin(), shipped.end());
    const std::set<std::string> inView(view.begin(), view.end());
    const std::set<std::string> declared(fields.begin(), fields.end());

    std::vector<std::string> homeless;
    std::vector<std::string> twice;
    for (const std::string& field : fields) {
        const bool s = inShipped.count(field) != 0;
        const bool v = inView.count(field) != 0;
        if (!s && !v) homeless.push_back(field);
        if (s && v)   twice.push_back(field);
    }

    // The other direction: a field renamed in the struct but not the visitor stops
    // compiling - unless the visitor is what was renamed, reading a key nothing holds.
    std::vector<std::string> unknown;
    for (const std::string& key : shipped) {
        if (declared.count(key) == 0) unknown.push_back(key);
    }
    for (const std::string& key : view) {
        if (declared.count(key) == 0) unknown.push_back(key);
    }

    std::printf(
        "      %zu settings: %zu shipped in project.json, %zu the editor's own\n",
        fields.size(),
        shipped.size(),
        view.size()
    );
    for (const std::string& one : homeless) {
        std::printf("      %s is written to neither file, so it never survives a restart\n", one.c_str());
    }
    for (const std::string& one : twice) {
        std::printf("      %s is written to both, so whichever loads last decides it\n", one.c_str());
    }
    for (const std::string& one : unknown) {
        std::printf("      a visitor names %s, which RenderSettings does not have\n", one.c_str());
    }

    check("the struct was read", !fields.empty());
    check("and both visitors were", !shipped.empty() && !view.empty());
    check("every render setting is persisted somewhere", homeless.empty());
    check("and none of them in two places",              twice.empty());
    check("and no visitor names a setting that is gone", unknown.empty());
}

// Every scene-format component, against the list an editor EntitySnapshot carries -
// what undoing a delete, redoing a create and Duplicate put back. One left off survives
// a save but not a delete and its undo: the entity returns without it.
//
// ScriptComponent is the exception, being move-only: the snapshot keeps it as
// serialized JSON through a special case in capture/apply, not a value copy.
void testUndoRestoresEveryComponent() {
    std::printf("What undoing a delete puts back:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path format   = engineRoot / "src/engine/io/scene/component_serializer.h";
    const fs::path snapshot = engineRoot / "src/editor/command/editor_commands.h";

    std::error_code ec;
    if (!fs::exists(format, ec) || !fs::exists(snapshot, ec)) {
        std::printf("  (no source in this tree - nothing to check)\n");
        return;
    }

    const std::vector<std::string> authored =
        macroRows(format, "VKM_SCENE_COMPONENTS", std::regex(R"RX(\n\s+[PRE]\((\w+),)RX"));
    const std::vector<std::string> restored =
        macroRows(snapshot, "VKM_EDITOR_COMPONENTS", std::regex(R"RX(\n\s+[XS]\((\w+),)RX"));
    const std::set<std::string> carried(restored.begin(), restored.end());

    std::vector<std::string> lost;
    for (const std::string& name : authored) {
        if (name == "ScriptComponent") continue;   // carried as JSON, not as a value
        if (carried.count(name) == 0) lost.push_back(name);
    }

    std::printf("      %zu authored, %zu restored\n", authored.size(), restored.size());
    for (const std::string& one : lost) {
        std::printf("      %s survives a save and not a delete and its undo\n", one.c_str());
    }
    check(
        "every component the scene format carries survives a delete and its undo",
        !authored.empty() && !restored.empty() && lost.empty()
    );
}

// Each GPU block is written in two languages, held together only by a comment on each
// side naming the other. Nothing in the build reads them, so a moved block leaves the
// other side pointing at where it was.
void testTheBackendPointsAtRealShaders() {
    std::printf("What the backend says about the shaders:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(engineRoot / "shaders", ec)) {
        std::printf("  (no shaders/ in this tree - nothing to check)\n");
        return;
    }

    const std::regex shaderPath(R"RX(shaders/[A-Za-z0-9_/.-]*[A-Za-z0-9_])RX");
    const std::regex claim(
        R"RX(must match (?:the )?([A-Za-z_][A-Za-z0-9_]*)[^.]{0,80}? in (shaders/[A-Za-z0-9_/.-]*[A-Za-z0-9_]))RX"
    );

    std::vector<std::string> wrong;
    size_t paths = 0, claims = 0;
    for (const fs::path& file : sourceFiles(engineRoot)) {
        const std::string text = readAll(file);
        for (std::sregex_iterator it(text.begin(), text.end(), shaderPath), end; it != end; ++it) {
            ++paths;
            if (fs::exists(engineRoot / it->str(), ec)) continue;
            wrong.push_back(file.filename().string() + ": no " + it->str());
        }

        const std::string flat = flattenComments(text);
        for (std::sregex_iterator it(flat.begin(), flat.end(), claim), end; it != end; ++it) {
            ++claims;
            const std::string name = (*it)[1].str();
            const std::string path = (*it)[2].str();
            if (namedUnder(engineRoot / path, name)) continue;
            wrong.push_back(file.filename().string() + ": " + path + " does not name " + name);
        }
    }

    std::printf("      %zu shader paths, %zu \"must match\" claims\n", paths, claims);
    for (const std::string& one : wrong) {
        std::printf("      %s\n", one.c_str());
    }
    check("every shader the backend names is where it says", paths > 0 && wrong.empty());
}

// Whether a shader stage includes shaders/fog.glsl through any chain, resolved as the
// loader does: beside the including file.
bool includesFog(const fs::path& stage, std::set<std::string>& visited) {
    std::error_code ec;
    if (!visited.insert(fs::weakly_canonical(stage, ec).string()).second) return false;

    const std::string text = readAll(stage);
    const std::regex include(R"RX(#include\s*"([^"]+)")RX");
    for (std::sregex_iterator it(text.begin(), text.end(), include), end; it != end; ++it) {
        // Normalised first: "../../fog.glsl" names its folder only once the dots resolve.
        const fs::path target = (stage.parent_path() / (*it)[1].str()).lexically_normal();
        if (target.filename() == "fog.glsl" && target.parent_path().filename() == "shaders") return true;
        if (includesFog(target, visited)) return true;
    }
    return false;
}

// No pass fogs the finished frame: every program drawing something seen through fog
// includes shaders/fog.glsl and fogs at its own depth, gated on u_hasFog. An unset
// uniform reads 0, so a pass that never hands its program the frame's fog draws
// unfogged, silently. Per file: a fogged program is handed the fog (GLPass::bindFog) or
// ruled out by name.
void testEveryFoggedProgramIsToldAboutTheFog() {
    std::printf("Which programs fog what they draw:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(engineRoot / "shaders", ec)) {
        std::printf("  (no shaders/ in this tree - nothing to check)\n");
        return;
    }

    // A program is the folder its stages sit in, named as the backend builds it.
    std::set<std::string> fogged;
    const fs::path shaders = engineRoot / "shaders";
    for (fs::recursive_directory_iterator it(shaders, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".shader") continue;
        std::set<std::string> visited;
        if (!includesFog(it->path(), visited)) continue;
        fogged.insert(fs::relative(it->path().parent_path(), engineRoot, ec).generic_string());
    }

    const std::regex built(R"RX("(shaders/[A-Za-z0-9_/]+)")RX");
    std::vector<std::string> untold;
    size_t builds = 0;
    for (const fs::path& file : sourceFiles(engineRoot)) {
        const std::string text = readAll(file);
        const bool tells = text.find("bindFog(") != std::string::npos
            || text.find("\"u_hasFog\"") != std::string::npos;
        for (std::sregex_iterator it(text.begin(), text.end(), built), end; it != end; ++it) {
            const std::string program = (*it)[1].str();
            if (fogged.count(program) == 0) continue;
            ++builds;
            if (!tells) untold.push_back(file.filename().string() + " builds " + program);
        }
    }

    std::printf("      %zu fogged program(s), built in %zu place(s)\n", fogged.size(), builds);
    for (const std::string& one : untold) {
        std::printf("      %s and never tells it about the fog\n", one.c_str());
    }
    check(
        "every program that fogs what it draws is told whether there is fog",
        !fogged.empty() && builds > 0 && untold.empty()
    );
}

// A sampler's texture unit is a number gl_bindings.h owns, named in a shader through
// the prelude's layout qualifier. A bare one reads unit 0 unless C++ sets it - a second,
// unchecked statement of the number that a pass binding elsewhere silently contradicts.
void testEverySamplerTakesItsUnitFromThePrelude() {
    std::printf("Where each shader sampler's unit is stated:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(engineRoot / "shaders", ec)) {
        std::printf("  (no shaders/ in this tree - nothing to check)\n");
        return;
    }

    const std::regex sampler(R"RX((layout\s*\(([^)]*)\)\s*)?uniform\s+[iu]?sampler\w*\s+(\w+))RX");
    std::set<std::string> names;
    std::vector<std::string> bare;
    const fs::path shaderRoot = engineRoot / "shaders";
    for (fs::recursive_directory_iterator it(shaderRoot, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string ext = it->path().extension().string();
        if (ext != ".shader" && ext != ".glsl") continue;
        const std::string text = codeOnly(readAll(it->path()));
        for (std::sregex_iterator m(text.begin(), text.end(), sampler), stop; m != stop; ++m) {
            names.insert((*m)[3].str());
            if ((*m)[2].str().find("binding") == std::string::npos) {
                bare.push_back(
                    fs::relative(it->path(), engineRoot, ec).generic_string() + " " + (*m)[3].str()
                );
            }
        }
    }

    const std::regex setter(R"RX(setUniform1i\("(\w+)")RX");
    std::vector<std::string> setFromCpp;
    for (const fs::path& file : sourceFiles(engineRoot)) {
        const std::string text = readAll(file);
        for (std::sregex_iterator m(text.begin(), text.end(), setter), stop; m != stop; ++m) {
            if (names.count((*m)[1].str())) {
                setFromCpp.push_back(file.filename().string() + " " + (*m)[1].str());
            }
        }
    }

    std::printf("      %zu sampler name(s)\n", names.size());
    for (const std::string& one : bare)       std::printf("      %s has no binding\n", one.c_str());
    for (const std::string& one : setFromCpp) std::printf("      %s sets a sampler's unit\n", one.c_str());
    check("every sampler names its unit in a layout qualifier", !names.empty() && bare.empty());
    check("  and no C++ sets one", setFromCpp.empty());
}

// The API site's pages, docs/api/*.dox, which no Markdown check reads. They name pages
// and source files as the manual does, and rot alike. A `@dir` block naming a directory
// outside Doxyfile.in's INPUT documents nothing and makes Doxygen warn every run.
void testTheApiPagesNameRealPaths() {
    std::printf("What the API site's pages name:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path api      = engineRoot / "docs/api";
    const fs::path doxyfile = engineRoot / "docs/Doxyfile.in";

    std::error_code ec;
    if (!fs::exists(api, ec) || !fs::exists(doxyfile, ec)) {
        std::printf("  (no docs/api in this tree - nothing to check)\n");
        return;
    }

    std::vector<std::string> inputs;
    {
        const std::string text = readAll(doxyfile);
        const std::regex input(R"RX("@CMAKE_SOURCE_DIR@/([^"]+)")RX");
        for (std::sregex_iterator it(text.begin(), text.end(), input), end; it != end; ++it) {
            inputs.push_back((*it)[1].str());
        }
    }

    const std::regex dir(R"RX(@dir\s+(\S+))RX");
    const std::regex page(R"RX(([A-Za-z0-9_][A-Za-z0-9_./-]*\.md)\b)RX");
    const std::regex source(R"RX(\b([A-Za-z0-9_][A-Za-z0-9_./-]*/[A-Za-z0-9_.-]+\.(?:h|cpp)))RX");

    std::vector<std::string> wrong;
    size_t dirs = 0, names = 0;
    for (fs::directory_iterator it(api, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".dox") continue;
        const std::string file = it->path().filename().string();
        const std::string text = readAll(it->path());

        for (std::sregex_iterator d(text.begin(), text.end(), dir), last; d != last; ++d) {
            ++dirs;
            const std::string path = (*d)[1].str();
            bool underInput = false;
            for (const std::string& root : inputs) {
                if ((path + "/").rfind(root + "/", 0) == 0) underInput = true;
            }
            if (!fs::is_directory(engineRoot / path, ec)) {
                wrong.push_back(file + ": @dir " + path + " is not a directory");
            } else if (!underInput) {
                wrong.push_back(file + ": @dir " + path + " is outside Doxyfile.in's INPUT");
            }
        }
        for (std::sregex_iterator n(text.begin(), text.end(), page), last; n != last; ++n) {
            ++names;
            if (fs::exists(engineRoot / "docs" / (*n)[1].str(), ec)) continue;
            wrong.push_back(file + ": no docs/" + (*n)[1].str());
        }
        for (std::sregex_iterator n(text.begin(), text.end(), source), last; n != last; ++n) {
            ++names;
            if (resolvesSomewhere(engineRoot, (*n)[1].str())) continue;
            wrong.push_back(file + ": no " + (*n)[1].str());
        }
    }

    std::printf("      %zu @dir block(s), %zu path(s) named\n", dirs, names);
    for (const std::string& one : wrong) std::printf("      %s\n", one.c_str());
    check("the INPUT list was read", !inputs.empty());
    check(
        "every page and file the API site names exists, and every @dir is under INPUT",
        dirs > 0 && wrong.empty()
    );
}

// Both index pages against the pages that exist: a reference page nothing links to is
// one nobody finds, and nothing makes an index mention a new page.
void testTheIndexesListEveryPage() {
    std::printf("What the indexes list:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path docs = engineRoot / "docs";
    std::error_code ec;
    if (!fs::exists(docs, ec)) {
        std::printf("  (no docs/ in this tree - nothing to check)\n");
        return;
    }

    // What each index links to, resolved as a click is. A name found anywhere in the
    // text is not a listing: io.md is inside audio.md.
    std::set<std::string> listed;
    const std::regex link(R"(\]\(([^)\s#]+)(?:#[^)\s]*)?\))");
    for (const fs::path& index : {engineRoot / "README.md", docs / "README.md"}) {
        const std::string text = proseOnly(readAll(index));
        for (std::sregex_iterator it(text.begin(), text.end(), link), end; it != end; ++it) {
            std::error_code canon;
            listed.insert(fs::weakly_canonical(index.parent_path() / (*it)[1].str(), canon).string());
        }
    }

    std::vector<std::string> unlisted;
    for (fs::recursive_directory_iterator it(docs, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".md") continue;
        // An index does not list itself.
        if (it->path().filename() == "README.md" || isPrivateDocument(it->path())) continue;

        std::error_code canon;
        if (listed.count(fs::weakly_canonical(it->path(), canon).string()) == 0) {
            unlisted.push_back(it->path().filename().string());
        }
    }

    for (const std::string& one : unlisted) {
        std::printf("      %s is linked from neither index\n", one.c_str());
    }
    check("every page is reachable from an index", unlisted.empty());
}

// A scene-format component without an Inspector card can be put in a file and not
// seen. Name is the one exception, named here so a second must be a decision: it is
// edited beside the entity's title, not in a card.
void testTheInspectorHasACardForEveryComponent() {
    std::printf("What the Inspector can edit:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path format = engineRoot / "src/engine/io/scene/component_serializer.h";
    const fs::path cards  = engineRoot / "src/editor/panels/inspector_panel.h";

    std::error_code ec;
    if (!fs::exists(format, ec) || !fs::exists(cards, ec)) {
        std::printf("  (no source in this tree - nothing to check)\n");
        return;
    }

    const std::vector<std::string> authored =
        macroRows(format, "VKM_SCENE_COMPONENTS", std::regex(R"RX(\n\s+[PRE]\((\w+),)RX"));
    const std::vector<std::string> carded =
        macroRows(cards, "VKM_INSPECTOR_CARDS", std::regex(R"RX(\n\s+X\((\w+),)RX"));
    const std::set<std::string> hasCard(carded.begin(), carded.end());

    std::vector<std::string> uneditable;
    for (const std::string& name : authored) {
        if (name == "Name") continue;   // the row beside the entity title
        if (hasCard.count(name) == 0) uneditable.push_back(name);
    }

    std::printf("      %zu in the scene format, %zu cards\n", authored.size(), carded.size());
    for (const std::string& one : uneditable) {
        std::printf("      %s can be authored in a file and not in the editor\n", one.c_str());
    }
    check(
        "every component the scene format carries has an Inspector card",
        !authored.empty() && !carded.empty() && uneditable.empty()
    );
}

// A suite is stated four times: the file, the declaration in suites.h, the row in
// main.cpp's SUITES table, and building.md's list. The middle two expand from
// VKM_TEST_SUITES; this checks the two that cannot - a file on disk and a sentence of prose.
void testEverySuiteIsAFileAndALine() {
    std::printf("What agrees that a suite exists:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path header = engineRoot / "tests/suites.h";
    const fs::path manual = engineRoot / "docs/reference/building.md";

    std::error_code ec;
    if (!fs::exists(header, ec)) {
        std::printf("  (no source in this tree - nothing to check)\n");
        return;
    }

    // The list is the authority: main.cpp's table and the declarations expand from it.
    std::vector<std::string> names;
    {
        const std::string text = readAll(header);
        const std::regex row(R"RX(X\(\s*"(\w+)"\s*,\s*run\w+\s*,)RX");
        for (std::sregex_iterator it(text.begin(), text.end(), row), stop; it != stop; ++it) {
            names.push_back((*it)[1].str());
        }
    }

    // tests/<area>/<suite>_tests.cpp, what the glob picks up; render/ is the GPU binary's.
    const auto isSuiteFile = [](const std::string& name) {
        return name.size() > 10 && name.rfind("_tests.cpp") == name.size() - 10;
    };
    std::vector<std::string> files;
    bool atRoot = false;
    for (fs::directory_iterator area(engineRoot / "tests", ec), end; !ec && area != end; area.increment(ec)) {
        if (!area->is_directory()) {
            atRoot = atRoot || isSuiteFile(area->path().filename().string());
            continue;
        }
        if (area->path().filename() == "render") continue;
        std::error_code inner;
        for (fs::directory_iterator it(area->path(), inner), stop;
            !inner && it != stop; it.increment(inner)) {
            const std::string name = it->path().filename().string();
            if (isSuiteFile(name)) files.push_back(name.substr(0, name.size() - 10));
        }
    }
    std::sort(files.begin(), files.end());
    std::vector<std::string> rows = names;
    std::sort(rows.begin(), rows.end());

    std::printf("      %zu listed, %zu files\n", names.size(), files.size());
    check("the suite list was read", !names.empty());
    check("each row is a tests/<area>/<row>_tests.cpp, and each such file a row", files == rows);
    check("  and no suite sits at the root of tests/", !atRoot);

    if (!fs::exists(manual, ec)) {
        std::printf("  (no docs/ in this tree - the manual's list is not checked)\n");
        return;
    }

    // Backticked in the manual so this can read them: exactly the list's suites.
    const std::string text = readAll(manual);
    const size_t begin = text.find("suites that run today are");
    std::set<std::string> listed;
    if (begin != std::string::npos) {
        const size_t end = text.find('.', text.find('`', begin));
        const std::string block = text.substr(begin, end - begin);
        const std::regex tick(R"RX(`(\w+)`)RX");
        for (std::sregex_iterator it(block.begin(), block.end(), tick), stop; it != stop; ++it) {
            listed.insert((*it)[1].str());
        }
    }
    const std::set<std::string> wanted(names.begin(), names.end());
    for (const std::string& one : wanted) {
        if (listed.count(one) == 0) std::printf("      the manual does not name %s\n", one.c_str());
    }
    check("  and the manual names every one of them, and only them", listed == wanted);
}

// architecture.md's directory map against the tree. docs/README.md sends new readers
// there first, so a row naming a moved header misleads them, and no sibling check
// reaches it: rows are directories, and testTheManualNamesRealFiles only matches
// tokens ending in a file extension.
//
// Checked: every row names an existing directory; every backticked name in a row that
// is a header in this tree lives at or under that row's path; and every directory
// holding sources is reached by some row.
void testTheManualsDirectoryMapIsTheTree() {
    std::printf("The engine's directory map, against the tree:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path page = engineRoot / "docs/reference/architecture.md";
    const fs::path tree = engineRoot / "src/engine";

    std::error_code ec;
    if (!fs::exists(page, ec) || !fs::exists(tree, ec)) {
        std::printf("  (no docs or no source in this tree - nothing to check)\n");
        return;
    }

    const std::string text = readAll(page);
    const size_t begin = text.find("Engine code, single include root `src/engine/`:");
    const size_t end   = text.find("OpenGL backend, `src/backend/opengl/`");
    if (begin == std::string::npos || end == std::string::npos) {
        check("the engine's directory table was found", false);
        return;
    }
    const std::string block = text.substr(begin, end - begin);

    // Where every header and source under src/ lives, by stem. All of src/, so a tool's
    // type listed under the engine fails ("a type inside another header" would pass it);
    // a directory outside it reads "../tools/cook/", which no row claims.
    const fs::path sources = engineRoot / "src";
    std::map<std::string, std::vector<std::string>> homeOf;
    std::set<std::string> directoriesWithSources;
    for (fs::recursive_directory_iterator it(sources, ec), stop; !ec && it != stop; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = it->path().extension().string();
        if (ext != ".h" && ext != ".cpp") continue;
        std::string dir = fs::relative(it->path().parent_path(), tree, ec).generic_string() + "/";
        if (dir == "./") dir.clear();
        if (dir.rfind("../", 0) != 0) directoriesWithSources.insert(dir);
        homeOf[it->path().stem().string()].push_back(dir);
    }

    // `WindowManager` is window_manager.h; `project_paths` is already a stem.
    const auto stemOf = [](const std::string& name) {
        std::string out;
        for (size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            const bool startsWord = i > 0 && std::isupper(static_cast<unsigned char>(c));
            const bool lowerBefore = startsWord && std::islower(static_cast<unsigned char>(name[i - 1]));
            const bool lowerAfter = i + 1 < name.size()
                && std::islower(static_cast<unsigned char>(name[i + 1]));
            if (startsWord && (lowerBefore || lowerAfter)) {
                out += '_';
            }
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    };

    std::vector<std::string> rows;
    std::vector<std::string> missingDirs;
    std::vector<std::string> misplaced;

    const std::regex rowRe(R"RX(^\|\s*`([A-Za-z0-9_/]+/)`\s*\|(.*)$)RX");
    std::istringstream lines(block);
    for (std::string line; std::getline(lines, line); ) {
        std::smatch m;
        if (!std::regex_search(line, m, rowRe)) continue;
        const std::string path     = m[1].str();
        const std::string contents = m[2].str();
        rows.push_back(path);

        if (!fs::is_directory(tree / path, ec)) {
            missingDirs.push_back(path);
            continue;
        }

        // The leading roster is the location claim; prose after it names things that
        // live elsewhere on purpose. Parenthesised asides go; the rest is cut at the
        // first prose break.
        std::string roster = contents;
        for (size_t open = roster.find('('); open != std::string::npos; open = roster.find('(')) {
            const size_t close = roster.find(')', open);
            if (close == std::string::npos) break;
            roster.erase(open, close - open + 1);
        }
        for (const char* const brk : {" - ", ". "}) {
            const size_t at = roster.find(brk);
            if (at != std::string::npos) roster.erase(at);
        }

        const std::regex tick(R"RX(`([A-Za-z_][A-Za-z0-9_]*)(?:\.h)?`)RX");
        for (std::sregex_iterator t(roster.begin(), roster.end(), tick), stop; t != stop; ++t) {
            const std::string stem = stemOf((*t)[1].str());
            const auto found = homeOf.find(stem);
            if (found == homeOf.end()) continue;   // a type inside another header, not a file

            bool under = false;
            for (const std::string& dir : found->second) {
                if (dir.rfind(path, 0) == 0) {
                    under = true;
                    break;
                }
            }
            if (!under) {
                misplaced.push_back(
                    path + " lists " + (*t)[1].str() + ", which is in " + found->second.front()
                );
            }
        }
    }

    // A directory the map does not reach at all, by a row or by an ancestor row.
    std::vector<std::string> unmapped;
    for (const std::string& dir : directoriesWithSources) {
        if (dir.empty()) continue;
        bool covered = false;
        for (const std::string& path : rows) {
            if (dir.rfind(path, 0) == 0) {
                covered = true;
                break;
            }
        }
        if (!covered) unmapped.push_back(dir);
    }

    for (const std::string& one : missingDirs) {
        std::printf("      %s has a row and is not a directory\n", one.c_str());
    }
    for (const std::string& one : misplaced) {
        std::printf("      %s\n", one.c_str());
    }
    for (const std::string& one : unmapped) {
        std::printf("      %s has no row and no ancestor row\n", one.c_str());
    }

    std::printf("      %zu rows, %zu directories with sources\n", rows.size(), directoriesWithSources.size());
    check("the directory table was read", rows.size() > 20);
    check("every row names a directory that exists", missingDirs.empty());
    check("  and every header a row names lives at or under it", misplaced.empty());
    check("  and every directory with sources is reached by some row", unmapped.empty());
}

// `vkm new` and the editor's New Project dialog both make projects from a template and
// must skip the same build output, and the SDK install must leave it out of the copies
// it ships. Python, C++ and CMake, so the list is stated three times, and a name only
// one states is copied into every project.
void testEveryWayOfMakingAProjectSkipsTheSameNames() {
    std::printf("What a new project does not inherit from the template:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path cli     = engineRoot / "tools/vkmcli/project.py";
    const fs::path editor  = engineRoot / "src/editor/chrome/new_project_dialog.cpp";
    const fs::path install = engineRoot / "cmake/install.cmake";

    std::error_code ec;
    if (!fs::exists(cli, ec) || !fs::exists(editor, ec) || !fs::exists(install, ec)) {
        std::printf("  (not in this tree - nothing to check)\n");
        return;
    }

    const std::regex quoted(R"RX("([A-Za-z_][A-Za-z0-9_.]*)")RX");

    const auto namesIn = [&](const fs::path& file, const std::string& marker, char opener, char closer) {
        std::set<std::string> names;
        const std::string text = readAll(file);
        const size_t begin = text.find(marker);
        if (begin == std::string::npos) return names;
        const size_t open  = text.find(opener, begin);
        const size_t close = text.find(closer, open);
        if (open == std::string::npos || close == std::string::npos) return names;

        const std::string block = text.substr(open, close - open);
        for (std::sregex_iterator it(block.begin(), block.end(), quoted), stop; it != stop; ++it) {
            names.insert((*it)[1].str());
        }
        return names;
    };

    const std::set<std::string> fromCli     = namesIn(cli,     "GENERATED = frozenset(", '{', '}');
    const std::set<std::string> fromEditor  = namesIn(editor,  "GENERATED[] =", '{', '}');
    const std::set<std::string> fromInstall = namesIn(install, "set(VKM_GENERATED", '(', ')');

    std::printf(
        "      %zu named by the CLI, %zu by the editor, %zu by the install\n",
        fromCli.size(),
        fromEditor.size(),
        fromInstall.size()
    );
    for (const std::string& one : fromCli) {
        if (fromEditor.count(one) == 0) {
            std::printf("      the editor would copy %s, which the CLI skips\n", one.c_str());
        }
    }
    for (const std::string& one : fromEditor) {
        if (fromCli.count(one) == 0) {
            std::printf("      the CLI would copy %s, which the editor skips\n", one.c_str());
        }
    }

    for (const std::string& one : fromInstall) {
        if (fromCli.count(one) == 0) {
            std::printf("      the install leaves out %s, which the CLI copies\n", one.c_str());
        }
    }
    for (const std::string& one : fromCli) {
        if (fromInstall.count(one) == 0) {
            std::printf("      the install ships %s, which the CLI skips\n", one.c_str());
        }
    }

    check("all three lists were read", !fromCli.empty() && !fromEditor.empty() && !fromInstall.empty());
    check("and a new project skips the same names either way", fromCli == fromEditor);
    check("  and the SDK ships its copies without them", fromCli == fromInstall);
}

// Dependencies are listed thrice: `.gitmodules` (what a clone fetches), the README's
// source layout and building.md's module table. A module swapped in `.gitmodules` alone
// leaves both documents naming one that is gone. A non-submodule directory under
// modules/ - told apart by having no `.git` - counts too, so it cannot hide.
void testTheModuleListsAreTheModules() {
    std::printf("What the engine says it is built on:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const fs::path gitmodules = engineRoot / ".gitmodules";
    const fs::path readme     = engineRoot / "README.md";
    const fs::path building   = engineRoot / "docs/reference/building.md";

    std::error_code ec;
    if (!fs::exists(gitmodules, ec) || !fs::exists(readme, ec) || !fs::exists(building, ec)) {
        std::printf("  (not in this tree - nothing to check)\n");
        return;
    }

    std::set<std::string> modules;
    const std::string listed = readAll(gitmodules);
    const std::regex submodulePath(R"RX(path\s*=\s*modules/([A-Za-z0-9_.-]+))RX");
    for (std::sregex_iterator it(listed.begin(), listed.end(), submodulePath), end; it != end; ++it) {
        modules.insert((*it)[1].str());
    }
    for (fs::directory_iterator it(engineRoot / "modules", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory() && !fs::exists(it->path() / ".git", ec)) {
            modules.insert(it->path().filename().string());
        }
    }

    // The README's block runs from its `modules/` line to the next unindented line. A
    // row names one or more slash-separated modules before the colon-ended word starting
    // its description; a row without one is names alone, described below.
    std::set<std::string> inReadme;
    {
        std::istringstream lines(readAll(readme));
        std::string line;
        bool inside = false;
        while (std::getline(lines, line)) {
            if (!inside) {
                inside = line == "modules/";
                continue;
            }
            if (line.empty() || line[0] != ' ') break;
            std::istringstream words(line);
            std::vector<std::string> names;
            std::string word;
            while (words >> word && word.back() != ':') {
                if (word != "/") names.push_back(word);
            }
            inReadme.insert(names.begin(), names.end());
        }
    }

    std::set<std::string> inBuilding;
    const std::string table = readAll(building);
    const std::regex row(R"RX(\|\s*`modules/([A-Za-z0-9_.-]+)`\s*\|)RX");
    for (std::sregex_iterator it(table.begin(), table.end(), row), end; it != end; ++it) {
        inBuilding.insert((*it)[1].str());
    }

    const auto report = [&](const std::set<std::string>& documented, const char* where) {
        for (const std::string& one : modules) {
            if (documented.count(one) == 0) std::printf("      %s does not list %s\n", where, one.c_str());
        }
        for (const std::string& one : documented) {
            if (modules.count(one) == 0) {
                std::printf("      %s lists %s, which is not a module\n", where, one.c_str());
            }
        }
    };
    std::printf("      %zu module(s)\n", modules.size());
    report(inReadme, "README.md");
    report(inBuilding, "building.md");

    check("the modules were read", !modules.empty());
    check("  and the README lists exactly them", inReadme == modules);
    check("  and so does building.md", inBuilding == modules);
}

// The suite check guards the outer level; this is one level down - a test function
// written and compiled but never called by its runner. Nothing fails or prints, and the
// file reads as covering what it does not.
void testEveryTestFunctionIsActuallyRun() {
    std::printf("Tests that are written and tests that are run:\n");

    const fs::path testsDir(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(testsDir / "tests", ec)) {
        std::printf("  (no tests/ in this tree - nothing to check)\n");
        return;
    }

    // `void testFoo(...) {` opens one; a line that *starts* with `testFoo(` calls one. A
    // forward declaration matches neither: no brace, and it starts with `void`.
    const std::regex defines(R"RX(^\s*(?:static\s+)?void\s+(test\w+)\s*\([^;]*\)\s*\{)RX");
    const std::regex calls(R"RX(^\s*(test\w+)\s*\()RX");

    size_t functions = 0;
    std::vector<std::string> unrun;

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(testsDir / "tests", ec), end;
        !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc)) continue;
        if (it->path().extension() == ".cpp") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    for (const fs::path& file : files) {
        std::set<std::string> defined;
        std::set<std::string> called;

        std::istringstream in(readAll(file));
        std::string line;
        while (std::getline(in, line)) {
            std::smatch m;
            if (std::regex_search(line, m, defines)) defined.insert(m[1].str());
            else if (std::regex_search(line, m, calls)) called.insert(m[1].str());
        }

        functions += defined.size();
        for (const std::string& one : defined) {
            if (called.count(one) == 0) {
                unrun.push_back(file.filename().string() + ": " + one);
            }
        }
    }

    std::printf("      %zu test functions across %zu files\n", functions, files.size());
    for (const std::string& one : unrun) {
        std::printf("      %s is compiled and never called\n", one.c_str());
    }
    check("the suites were read", functions > 100 && !files.empty());
    check("every test function its suite defines is also run", unrun.empty());
}

// A thread_local reachable from an engine header is instantiated in every binary using
// it, and one with a destructor registers it against the binary; glibc will not unmap a
// library while a thread holding such a registration lives. One gameplay call into such
// a header makes the module un-unloadable, so every script reload leaves megabytes of
// old copy mapped, silently. Modules compile engine headers but never engine sources,
// and the engine library is never unloaded, so this reads headers only.
void testNoEngineHeaderCarriesAThreadLocal() {
    std::printf("What a gameplay module can be given a TLS block by:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;

    // The roots a gameplay module includes from; not the backend's - a module links the
    // engine, not the renderer.
    const std::array<const char*, 2> roots = {"src/engine", "src/tools"};

    std::vector<std::string> offenders;
    size_t scanned = 0;
    for (const char* root : roots) {
        const fs::path dir = engineRoot / root;
        if (!fs::exists(dir, ec)) continue;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() != ".h") continue;
            ++scanned;

            const std::string text = readAll(it->path());
            // Skipping the comment that explains the rule, which names it.
            for (std::size_t at = text.find("thread_local"); at != std::string::npos;
                at = text.find("thread_local", at + 1)) {
                const std::size_t lineStart = text.rfind('\n', at) + 1;
                const std::string line = text.substr(lineStart, text.find('\n', at) - lineStart);
                if (line.find("//") < line.find("thread_local")) continue;
                if (line.find('*') < line.find("thread_local")) continue;
                offenders.push_back(it->path().filename().string() + ":" + line);
            }
        }
    }

    if (scanned == 0) {
        std::printf("  (no engine sources in this tree - nothing to check)\n");
        return;
    }
    std::printf("      %zu header(s) scanned\n", scanned);
    for (const std::string& one : offenders) {
        std::printf("      %s\n", one.c_str());
    }
    check("no engine header defines a thread_local", offenders.empty());
}

// Every path a source comment names, against the tree. Nothing in the build reads "see
// core/math/projection.h", so it outlives the header and points at nothing.
//
// Only paths this repository could own are checked: GL/glew.h or GLFW/glfw3.h names a
// dependency's header, and decal/fragment.shader a loader-style shader path; both
// resolve, through modules/ and shaders/.
void testSourceCommentsNameRealFiles() {
    std::printf("What the source says about itself:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(engineRoot / "src", ec)) {
        std::printf("  (no sources in this tree - nothing to check)\n");
        return;
    }

    // What a comment could legitimately name: our trees via the manual check's include
    // roots, shaders via their loader-relative root, dependencies via modules/.
    const auto resolves = [&](const std::string& path) {
        if (resolvesSomewhere(engineRoot, path)) return true;
        if (fs::exists(engineRoot / "shaders" / path, ec)) return true;
        static std::set<std::string> moduleFiles;
        static bool walked = false;
        if (!walked) {
            walked = true;
            // Followed, since a checkout may link its modules in. In UTF-8: ufbx's test
            // data has non-ASCII names, which string() throws on under Windows.
            const auto follow = fs::directory_options::follow_directory_symlink;
            for (fs::recursive_directory_iterator it(engineRoot / "modules", follow, ec), end;
                !ec && it != end; it.increment(ec)) {
                if (it->is_regular_file()) moduleFiles.insert(it->path().filename().u8string());
            }
        }
        const size_t slash = path.find_last_of('/');
        return moduleFiles.count(slash == std::string::npos ? path : path.substr(slash + 1)) != 0;
    };

    // A project's comments name files from its own root (`src/module.cpp`), so a path
    // is also tried from every folder above the naming file.
    const auto nearby = [&](const fs::path& file, const std::string& path) {
        for (fs::path dir = file.parent_path(); dir != engineRoot && dir.has_relative_path();
            dir = dir.parent_path()) {
            if (fs::exists(dir / path, ec)) return true;
        }
        return false;
    };

    const std::regex pathish(
        R"RX(\b([A-Za-z0-9_][A-Za-z0-9_./-]*/[A-Za-z0-9_.-]+\.(?:h|cpp|glsl|shader))\b)RX"
    );

    std::vector<std::string> missing;
    size_t named = 0;
    for (const fs::path& file : engineAndProjectFiles(engineRoot)) {
        std::ifstream in(file);
        std::string line;
        int number = 0;
        while (std::getline(in, line)) {
            ++number;
            const size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            const std::string trimmed = line.substr(first);
            const bool comment = trimmed.rfind("//", 0) == 0 || trimmed.rfind("*", 0) == 0
                || trimmed.rfind("/*", 0) == 0;
            if (!comment) continue;

            for (std::sregex_iterator it(line.begin(), line.end(), pathish), end; it != end; ++it) {
                const std::string token = (*it)[1].str();
                if (token.rfind("http", 0) == 0 || isPlaceholder(token)) continue;
                ++named;
                if (resolves(token) || nearby(file, token)) continue;
                missing.push_back(
                    file.filename().string() + ":" + std::to_string(number) + " names " + token
                );
            }
        }
    }

    std::printf("      %zu path(s) named in comments\n", named);
    for (const std::string& one : missing) {
        std::printf("      %s\n", one.c_str());
    }
    check("every file a source comment names exists", named > 0 && missing.empty());
}

// Keep only what is inside a comment - the reverse of codeOnly - with the line
// structure, so a match's line number is the file's.
std::string commentsOnly(const std::string& text) {
    // From codeMasked, so one lexer decides what a comment is: what it blanked and the
    // text had something in is a comment's; a literal it filled is not.
    const std::string code = codeMasked(text, 'x');
    std::string out(text.size(), ' ');
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') out[i] = '\n';
        else if (code[i] == ' ') out[i] = text[i];
    }
    return out;
}

// Every `.glsl` and `.shader` under shaders/.
std::vector<fs::path> shaderFiles(const fs::path& engineRoot) {
    std::vector<fs::path> found;
    std::error_code ec;
    const fs::path shaderRoot = engineRoot / "shaders";
    for (fs::recursive_directory_iterator it(shaderRoot, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string ext = it->path().extension().string();
        if (ext == ".glsl" || ext == ".shader") found.push_back(it->path());
    }
    std::sort(found.begin(), found.end());
    return found;
}

// A comment names the code it means rather than describing it (code-style.md 6.1,
// "Speak for other code"), and a name can be checked. So every `Scope::member` or
// camelCase `function()` a comment writes as code must be declared. An undeclared scope
// is a library's (`glm::mix`) and is skipped whole, as is a library-prefixed call.
void testSourceCommentsNameRealSymbols() {
    std::printf("What the source's comments name:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(engineRoot / "src", ec)) {
        std::printf("  (no sources in this tree - nothing to check)\n");
        return;
    }
    std::set<std::string> declared = identifiersInCode(engineRoot);
    std::vector<fs::path> declaring = shaderFiles(engineRoot);
    const std::vector<fs::path> suiteSources = suiteFiles(engineRoot);
    declaring.insert(declaring.end(), suiteSources.begin(), suiteSources.end());
    for (const fs::path& shader : declaring) {
        const std::string text = codeOnly(readAll(shader));
        static const std::regex word(R"RX(\b[A-Za-z_]\w*\b)RX");
        for (std::sregex_iterator it(text.begin(), text.end(), word), end; it != end; ++it) {
            declared.insert(it->str());
        }
    }

    const std::regex qualified(R"RX(\b([A-Z]\w*)::([A-Za-z_]\w*)\b)RX");
    const std::regex called(R"RX(\b([a-z][a-z0-9]*[A-Z]\w*)\(\))RX");
    // Scopes a comment may name that the tree does not declare: other code's. Any other
    // undeclared scope is a renamed or deleted engine one. `Game` is the template's
    // namespace, standing for a reader's own project.
    const std::set<std::string> EXTERNAL_SCOPES = {"ImGui", "ImGuiKey", "ImDrawList", "Tracy", "Game"};
    std::set<std::string> undeclaredScopes;
    const auto library = [](const std::string& name) {
        for (const char* prefix : {"gl", "stbi", "ufbx", "cgltf", "meshopt", "ma", "imgui"}) {
            if (name.rfind(prefix, 0) == 0 && name.size() > std::strlen(prefix)
                && std::isupper(static_cast<unsigned char>(name[std::strlen(prefix)]))) return true;
        }
        return false;
    };

    std::vector<fs::path> files = engineAndProjectFiles(engineRoot);
    const std::vector<fs::path> suites  = suiteFiles(engineRoot);
    const std::vector<fs::path> shaders = shaderFiles(engineRoot);
    files.insert(files.end(), suites.begin(), suites.end());
    files.insert(files.end(), shaders.begin(), shaders.end());

    std::vector<std::string> unknown;
    size_t checked = 0;
    for (const fs::path& file : files) {
        // This file's comments quote the mistakes its checks catch.
        if (file.filename() == "docs_tests.cpp") continue;
        const std::string comments = commentsOnly(readAll(file));
        const auto lineOf = [&](std::ptrdiff_t at) {
            return std::to_string(std::count(comments.begin(), comments.begin() + at, '\n') + 1);
        };
        // A doc-block sample - a statement, or a definition opening its body - writes
        // whatever a project would, its own types included.
        const auto inSample = [&](std::ptrdiff_t at) {
            const size_t begin = comments.rfind('\n', static_cast<size_t>(at)) + 1;
            const size_t end   = comments.find('\n', static_cast<size_t>(at));
            const std::string line = comments.substr(begin, end - begin);
            const size_t last = line.find_last_not_of(" \t");
            return line.find(';') != std::string::npos || (last != std::string::npos && line[last] == '{');
        };
        for (std::sregex_iterator q(comments.begin(), comments.end(), qualified), end; q != end; ++q) {
            const std::string scope  = (*q)[1].str();
            const std::string member = (*q)[2].str();
            if (inSample(q->position())) continue;
            if (declared.count(scope) == 0) {
                if (EXTERNAL_SCOPES.count(scope) == 0) undeclaredScopes.insert(scope + "::" + member);
                continue;
            }
            ++checked;
            if (!scopeHas(engineRoot, scope, member)) {
                unknown.push_back(
                    file.filename().string() + ":" + lineOf(q->position()) + " names " + scope + "::" + member
                );
            }
        }
        for (std::sregex_iterator c(comments.begin(), comments.end(), called), end; c != end; ++c) {
            const std::string name = (*c)[1].str();
            if (library(name)) continue;
            // A sample statement in a doc block calls whatever a project would.
            const size_t lineStart = comments.rfind('\n', c->position()) + 1;
            const size_t lineEnd   = comments.find('\n', c->position());
            if (comments.substr(lineStart, lineEnd - lineStart).find(';') != std::string::npos) continue;
            ++checked;
            if (declared.count(name) == 0) {
                unknown.push_back(
                    file.filename().string() + ":" + lineOf(c->position()) + " names " + name + "()"
                );
            }
        }
    }

    std::printf("      %zu name(s) written in comments\n", checked);
    for (const std::string& one : unknown) {
        std::printf("      %s, which the source does not declare\n", one.c_str());
    }
    for (const std::string& one : undeclaredScopes) {
        std::printf("      %s is written under a scope the tree does not declare\n", one.c_str());
    }
    check("every name a source comment writes exists", checked > 100 && unknown.empty());
    check("  under a scope that exists", undeclaredScopes.empty());
}

// A comment states what is true, never what was (code-style.md 6.1): the log keeps
// history, and no reader can check a sentence about the past against the code. These
// phrases have no present-tense reading; those that also describe a running program
// ("previously selected", "a world that was replaced", "no longer touching") are left
// to review.
void testNoCommentNarratesHistory() {
    std::printf("What the source's comments say about the past:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(engineRoot / "src", ec)) {
        std::printf("  (no sources in this tree - nothing to check)\n");
        return;
    }

    const std::regex past(
        R"RX(\b(formerly|used to be|until recently|was fixed|has been fixed|fixed a bug|a bug (where|in which)|the old (code|version|behaviou?r)|we (changed|replaced|removed|added)|(was|has been) renamed|renamed from|this (commit|fix)\b|before the fix|previously (crash|fail|leak|broke)\w*))RX",
        std::regex::icase
    );

    std::vector<fs::path> files = engineAndProjectFiles(engineRoot);
    const std::vector<fs::path> suites  = suiteFiles(engineRoot);
    const std::vector<fs::path> shaders = shaderFiles(engineRoot);
    files.insert(files.end(), suites.begin(), suites.end());
    files.insert(files.end(), shaders.begin(), shaders.end());

    std::vector<std::string> offenders;
    for (const fs::path& file : files) {
        if (file.filename() == "docs_tests.cpp") continue;
        const std::string comments = flattenComments(commentsOnly(readAll(file)));
        for (std::sregex_iterator it(comments.begin(), comments.end(), past), end; it != end; ++it) {
            offenders.push_back(file.filename().string() + ": \"" + it->str() + "\"");
        }
    }

    for (const std::string& one : offenders) {
        std::printf("      %s\n", one.c_str());
    }
    check("no source comment narrates history", offenders.empty());
}

// Every C++ file the style rules hold: the engine's, the projects' and the suites'.
std::vector<fs::path> styledFiles(const fs::path& engineRoot) {
    std::vector<fs::path> files = engineAndProjectFiles(engineRoot);
    const std::vector<fs::path> suites = suiteFiles(engineRoot);
    files.insert(files.end(), suites.begin(), suites.end());
    return files;
}

// code-style.md 5.5: a value is named, never labelled by a comment beside it, since
// nothing keeps the label in step. A comment whose innermost open bracket is a
// parenthesis is inside a list - arguments, parameters - except a control statement's
// condition. A lambda's body inside a call is a brace, and its comments are a body's.
void testNoCommentInsideAnArgumentList() {
    std::printf("What the source's argument lists carry:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const std::regex control(R"RX(\b(if|while|for|switch|return|catch)\s*$)RX");

    std::vector<std::string> offenders;
    for (const fs::path& file : styledFiles(engineRoot)) {
        const std::string raw  = readAll(file);
        const std::string code = codeMasked(raw, 'x');

        std::vector<std::pair<char, bool>> open;  // The bracket, and whether it opens a list.
        size_t line = 1;
        size_t lineStart = 0;
        bool directive = false;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\n') {
                ++line;
                lineStart = i + 1;
                directive = false;
                continue;
            }
            if (i == raw.find_first_not_of(" \t", lineStart) && raw[i] == '#') directive = true;
            if (directive) continue;

            const bool opens = raw.compare(i, 2, "//") == 0 || raw.compare(i, 2, "/*") == 0;
            const bool comment = code[i] == ' ' && opens;
            if (comment) {
                if (!open.empty() && open.back().first == '(' && open.back().second) {
                    const std::string where = fs::relative(file, engineRoot).generic_string();
                    offenders.push_back(where + ":" + std::to_string(line));
                }
                const bool toLineEnd = raw[i + 1] == '/';
                const size_t close = toLineEnd ? raw.find('\n', i) : raw.find("*/", i);
                const size_t end = close == std::string::npos ? raw.size() : close + (toLineEnd ? 0 : 1);
                for (; i < end && i < raw.size(); ++i) {
                    if (raw[i] == '\n') {
                        ++line;
                        lineStart = i + 1;
                    }
                }
                --i;
                continue;
            }

            const char c = code[i];
            if (c == '(') {
                const std::string head = code.substr(lineStart, i - lineStart);
                open.push_back({c, !std::regex_search(head, control)});
            } else if (c == '[' || c == '{') {
                open.push_back({c, false});
            } else if ((c == ')' || c == ']' || c == '}') && !open.empty()) {
                open.pop_back();
            }
        }
    }

    for (const std::string& one : offenders) {
        std::printf("      %s has a comment inside an argument list\n", one.c_str());
    }
    check("no argument list carries a comment", offenders.empty());
}

// Whether the `{` at @p at opens a braced list rather than a block: it follows `=`,
// `(`, `,` or `return`, or a list's own `{`, or sits against a type's name (`Vec3{`,
// `std::vector<int>{`). After `)`, `]`, `;` or a keyword it opens a body.
bool opensBracedList(const std::string& code, size_t at, bool insideList) {
    size_t prev = at;
    while (prev > 0 && (code[prev - 1] == ' ' || code[prev - 1] == '\t' || code[prev - 1] == '\n')) --prev;
    if (prev == 0) return false;
    const char before = code[prev - 1];
    if (before == '{') return insideList;
    if (before == '=' || before == '(' || before == ',') return true;
    if (prev >= 6 && code.compare(prev - 6, 6, "return") == 0) return true;
    const bool touching = prev == at;
    return touching && (std::isalnum(static_cast<unsigned char>(before)) || before == '_' || before == '>');
}

// The items a line of a list holds past one: commas outside any bracket, template
// arguments included, less a trailing one.
int extraItems(const std::string& piece) {
    int depth = 0;
    int angles = 0;
    int separators = 0;
    for (size_t k = 0; k < piece.size(); ++k) {
        const char ch = piece[k];
        const char before = k > 0 ? piece[k - 1] : ' ';
        if (ch == '<' && (std::isalnum(static_cast<unsigned char>(before)) || before == '_')) ++angles;
        else if (ch == '>' && angles > 0 && before != '-') --angles;
        else if (ch == '(' || ch == '[' || ch == '{') ++depth;
        else if (ch == ')' || ch == ']' || ch == '}') --depth;
        else if (ch == ',' && depth == 0 && angles == 0) ++separators;
    }
    if (!piece.empty() && piece.back() == ',') --separators;
    return separators;
}

// code-style.md 5.3: a parameter, argument or braced list sits on one line when it fits,
// else breaks whole - opener ends its line, one item per line, closer starts one. A
// table keeps its rows (every line the same item count). Control conditions, returned
// expressions, and calls whose opener line ends in a lambda's brace are left to review.
void testABrokenListBreaksWhole() {
    std::printf("How the source breaks a list that does not fit:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const std::regex control(R"RX(\b(if|while|for|switch|return|catch)\s*\($)RX");
    const std::regex lambdaOpener(R"RX(\]\s*(\([^()]*(\([^()]*\)[^()]*)*\))?\s*(mutable\s*)?(->[^{]*)?\{$)RX");
    const std::regex bracedArgument(R"RX([\w:<>]*\{)RX");
    const std::regex macroTable(R"RX([A-Z][A-Z0-9_]*\(.*\),?)RX");

    std::vector<std::string> offenders;
    std::vector<std::string> packed;
    size_t broken = 0;
    std::vector<std::string> fitting;
    for (const fs::path& file : styledFiles(engineRoot)) {
        const std::string raw = readAll(file);
        const std::string code = codeMasked(raw, 'x');

        std::vector<size_t> lineStart{0};
        for (size_t i = 0; i < code.size(); ++i) {
            if (code[i] == '\n') lineStart.push_back(i + 1);
        }
        const auto lineOf = [&](size_t at) {
            const auto after = std::upper_bound(lineStart.begin(), lineStart.end(), at);
            return static_cast<size_t>(after - lineStart.begin()) - 1;
        };
        const auto lineText = [&](size_t line) {
            const size_t begin = lineStart[line];
            const size_t end = line + 1 < lineStart.size() ? lineStart[line + 1] - 1 : code.size();
            return code.substr(begin, end - begin);
        };
        const auto hasComment = [&](size_t line) {
            const size_t begin = lineStart[line];
            const size_t end = line + 1 < lineStart.size() ? lineStart[line + 1] - 1 : code.size();
            for (size_t k = begin; k + 1 < end; ++k) {
                const bool opens = raw.compare(k, 2, "//") == 0 || raw.compare(k, 2, "/*") == 0;
                if (opens && code[k] == ' ') return true;
            }
            return false;
        };
        const auto trimmed = [](std::string text) {
            const size_t first = text.find_first_not_of(" \t");
            if (first == std::string::npos) return std::string();
            return text.substr(first, text.find_last_not_of(" \t") - first + 1);
        };

        struct Opener {
            size_t at;
            bool   list;
        };
        std::vector<Opener> open;
        bool macro = false;
        for (size_t line = 0; line < lineStart.size(); ++line) {
            const std::string text = lineText(line);
            const std::string bare = trimmed(text);
            if (macro || (!bare.empty() && bare[0] == '#')) {
                macro = !bare.empty() && bare.back() == '\\';
                continue;
            }
            for (size_t col = 0; col < text.size(); ++col) {
                const char c = text[col];
                const size_t at = lineStart[line] + col;
                if (c == '(') {
                    const std::string head = text.substr(0, col + 1);
                    open.push_back({at, !std::regex_search(head, control)});
                } else if (c == '{') {
                    const bool insideList = !open.empty() && open.back().list && code[open.back().at] == '{';
                    open.push_back({at, opensBracedList(code, at, insideList)});
                } else if (c == '[') {
                    open.push_back({at, false});
                }
                if ((c != ')' && c != '}' && c != ']') || open.empty()) continue;
                const Opener opener = open.back();
                open.pop_back();
                if (!opener.list) continue;
                const size_t openLine = lineOf(opener.at);
                if (openLine == line) continue;

                const size_t openColumn = opener.at - lineStart[openLine];
                ++broken;
                const std::string afterOpen = trimmed(lineText(openLine).substr(openColumn + 1));
                const std::string beforeClose = trimmed(text.substr(0, col));
                // A call whose last argument is a lambda, or whose one argument is a
                // braced list (`std::max({`, `push_back(Vertex{`), opens it on its own
                // line; the braced list is checked as a list of its own.
                const bool lambdaLast = std::regex_search(afterOpen, lambdaOpener)
                    || (std::regex_match(afterOpen, bracedArgument) && beforeClose == "}");
                const std::string where =
                    fs::relative(file, engineRoot).generic_string() + ":" + std::to_string(openLine + 1);
                if ((!afterOpen.empty() && !lambdaLast) || (afterOpen.empty() && !beforeClose.empty())) {
                    offenders.push_back(where);
                    continue;
                }
                if (lambdaLast || openLine + 1 >= line) continue;

                // Item-starting lines: those at the list's own depth, not inside a
                // multi-line item.
                std::vector<size_t> itemLines;
                {
                    int depth = 0;
                    for (size_t item = openLine + 1; item < line; ++item) {
                        if (depth == 0) itemLines.push_back(item);
                        for (const char ch : lineText(item)) {
                            if (ch == '(' || ch == '[' || ch == '{') ++depth;
                            else if (ch == ')' || ch == ']' || ch == '}') --depth;
                        }
                    }
                }
                // An X-macro's expansion is a table's rows, written once elsewhere.
                const bool expansion = itemLines.size() == 1
                    && std::regex_match(trimmed(lineText(itemLines[0])), macroTable);
                if (expansion) continue;

                // A table: rows of several items, the last allowed to be anything - a
                // remainder, a terminator.
                std::vector<int> counts;
                for (const size_t item : itemLines) counts.push_back(extraItems(trimmed(lineText(item))));
                bool table = counts.size() > 1;
                for (size_t r = 0; r + 1 < counts.size() && table; ++r) table = counts[r] > 0;
                const bool several = std::any_of(counts.begin(), counts.end(), [](int n) { return n > 0; });
                if (several && !table) {
                    packed.push_back(where);
                    continue;
                }

                // Broken whole though it fits. Item lines must be whole and comment-free
                // to be joined, and a table - records in braced rows included - keeps
                // its rows.
                const bool records = itemLines.size() > 1 && std::all_of(
                    itemLines.begin(),
                    itemLines.end(),
                    [&](size_t item) { return trimmed(lineText(item)).rfind('{', 0) == 0; }
                );
                if (table || records) continue;
                std::string joined = lineText(openLine);
                joined.erase(joined.find_last_not_of(" \t") + 1);
                bool joinable = true;
                for (size_t item = openLine + 1; item < line && joinable; ++item) {
                    const std::string piece = trimmed(lineText(item));
                    const auto opens = std::count_if(piece.begin(), piece.end(), [](char ch) {
                        return ch == '(' || ch == '[' || ch == '{';
                    });
                    const auto closes = std::count_if(piece.begin(), piece.end(), [](char ch) {
                        return ch == ')' || ch == ']' || ch == '}';
                    });
                    joinable = !piece.empty() && opens == closes && !hasComment(item);
                    joined += (item == openLine + 1 ? "" : " ") + piece;
                }
                joined += trimmed(text.substr(col));
                if (joinable && joined.size() <= 110) fitting.push_back(where);
            }
        }
    }

    std::printf("      %zu list(s) span lines\n", broken);
    for (const std::string& one : offenders) {
        std::printf("      %s breaks a list part-way\n", one.c_str());
    }
    for (const std::string& one : packed) {
        std::printf("      %s puts several items to a line of a list that is not a table\n", one.c_str());
    }
    for (const std::string& one : fitting) {
        std::printf("      %s breaks a list that fits on one line\n", one.c_str());
    }
    check("every list that spans lines breaks whole", broken > 0 && offenders.empty());
    check("  one item to a line", packed.empty());
    check("  and only when it does not fit on one line", fitting.empty());
}

// code-style.md section 5: a line fits in 110 columns. Macro-table rows and raw-string
// regexes keep their shape (5.3): a row is a continued macro line invoking one
// (`X(Mesh, mesh) \`), and a raw string the line opening one, not any line with `R"`.
void testEveryLineFits() {
    std::printf("How wide the source runs:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    const std::regex rawOpener(R"RX((^|[^\w])(u8|u|U|L)?R"[^ ()\\\t"]{0,16}\()RX");
    const std::regex row(R"RX(^\s*(#define\s+)?[A-Za-z_]\w*\s*\()RX");

    std::vector<std::string> offenders;
    // Shaders, build files and Python tools too: same editor, and 6.2 holds them alike.
    std::vector<fs::path> files = styledFiles(engineRoot);
    const std::vector<fs::path> shaders = shaderFiles(engineRoot);
    files.insert(files.end(), shaders.begin(), shaders.end());
    const std::vector<fs::path> builds = buildAndToolFiles(engineRoot);
    files.insert(files.end(), builds.begin(), builds.end());
    for (const fs::path& file : files) {
        std::ifstream in(file);
        std::string line;
        bool continued = false;
        int number = 0;
        while (std::getline(in, line)) {
            ++number;
            const bool continues = !line.empty() && line.back() == '\\';
            const bool tableRow = (continues || continued) && std::regex_search(line, row);
            continued = continues;
            if (line.size() > 110 && !tableRow && !std::regex_search(line, rawOpener)) {
                const std::string where = fs::relative(file, engineRoot).generic_string();
                offenders.push_back(where + ":" + std::to_string(number));
            }
            // Tabs are not a width the rule can count, nor an indent the tree uses.
            if (line.find('\t') != std::string::npos) {
                const std::string where = fs::relative(file, engineRoot).generic_string();
                offenders.push_back(where + ":" + std::to_string(number) + " (a tab)");
            }
        }
    }

    for (const std::string& one : offenders) {
        std::printf("      %s runs past 110 columns, or holds a tab\n", one.c_str());
    }
    check("every line fits in 110 columns", offenders.empty());
}

// code-style.md 7.1: every class spells out its five special members, so copy and move
// behaviour is written where a reader looks. A gameplay Behavior is exempt: clone() copies it.
void testEveryClassWritesItsFive() {
    std::printf("Which classes say how they copy and move:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    // `class alignas(16) X` and `class VKM_API X` are classes too.
    const std::regex header(
        R"RX(\b(enum\s+)?class\s+(?:alignas\s*\([^)]*\)\s+|[A-Z][A-Z0-9_]*_API\s+)?((?:\w+::)*(\w+))(?:\s+final)?\s*(:(?!:)[^{;]*)?\{)RX"
    );
    const std::regex gameplay(R"RX(\b(Behavior\b|ReflectedBehavior\s*<))RX");

    std::vector<std::string> offenders;
    size_t classes = 0;
    for (const fs::path& file : styledFiles(engineRoot)) {
        const std::string code = codeOnly(readAll(file));
        for (std::sregex_iterator it(code.begin(), code.end(), header), end; it != end; ++it) {
            if ((*it)[1].matched) continue;
            const std::string name  = (*it)[3].str();
            const std::string bases = (*it)[4].str();
            if (std::regex_search(bases, gameplay)) continue;

            size_t at = static_cast<size_t>(it->position() + it->length());
            int depth = 1;
            const size_t begin = at;
            for (; at < code.size() && depth > 0; ++at) {
                if (code[at] == '{') ++depth;
                else if (code[at] == '}') --depth;
            }
            const std::string body = code.substr(begin, at - begin);
            ++classes;

            // All five, each by shape: one alone says how a copy behaves and leaves a
            // move to the compiler.
            const std::string self = "\\b" + name + R"RX(\s*)RX";
            const std::pair<const char*, std::regex> members[] = {
                {"destructor", std::regex("~" + name + R"RX(\s*\()RX")},
                {"copy constructor", std::regex(self + R"RX(\(\s*const\s+)RX" + name + R"RX(\s*&[^&])RX")},
                {"copy assignment", std::regex(R"RX(operator=\s*\(\s*const\s+)RX" + name + R"RX(\s*&[^&])RX")},
                {"move constructor", std::regex(self + R"RX(\(\s*)RX" + name + R"RX(\s*&&)RX")},
                {"move assignment", std::regex(R"RX(operator=\s*\(\s*)RX" + name + R"RX(\s*&&)RX")},
            };
            std::string missing;
            for (const auto& [what, shape] : members) {
                if (std::regex_search(body, shape)) continue;
                missing += missing.empty() ? what : std::string(", ") + what;
            }
            if (missing.empty()) continue;
            const auto declaredAt = code.begin() + it->position();
            const size_t line = static_cast<size_t>(std::count(code.begin(), declaredAt, '\n')) + 1;
            offenders.push_back(
                fs::relative(file, engineRoot).generic_string() + ":" + std::to_string(line) + " " + name
                    + " (no " + missing + ")"
            );
        }
    }

    std::printf("      %zu class(es)\n", classes);
    for (const std::string& one : offenders) {
        std::printf("      %s\n", one.c_str());
    }
    check("every class writes its Rule of 5", classes > 0 && offenders.empty());
}

// code-style.md 4.1: a struct is data; a class has behaviour and hides state. A struct
// with a private or protected section is a class, owing m_ names and the Rule of 5.
void testAStructHidesNothing() {
    std::printf("What the source's structs hide:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::vector<std::string> offenders;
    size_t structs = 0;
    for (const fs::path& file : styledFiles(engineRoot)) {
        const std::string code = codeOnly(readAll(file));
        for (size_t at = code.find("struct "); at != std::string::npos; at = code.find("struct ", at + 1)) {
            const char before = at > 0 ? code[at - 1] : ' ';
            if (std::isalnum(static_cast<unsigned char>(before)) || before == '_') continue;
            const size_t brace = code.find_first_of("{;()", at);
            if (brace == std::string::npos || code[brace] != '{') continue;
            ++structs;
            int depth = 1;
            size_t close = brace + 1;
            bool hides = false;
            for (; close < code.size() && depth > 0; ++close) {
                if (code[close] == '{') ++depth;
                else if (code[close] == '}') --depth;
                else if (depth == 1 && code.compare(close, 8, "private:") == 0) hides = true;
                else if (depth == 1 && code.compare(close, 10, "protected:") == 0) hides = true;
            }
            if (!hides) continue;
            const auto declaredAt = code.begin() + static_cast<long>(at);
            const size_t line = static_cast<size_t>(std::count(code.begin(), declaredAt, '\n')) + 1;
            offenders.push_back(fs::relative(file, engineRoot).generic_string() + ":" + std::to_string(line));
        }
    }

    std::printf("      %zu struct(s)\n", structs);
    for (const std::string& one : offenders) {
        std::printf("      %s is a struct with a private section, which makes it a class\n", one.c_str());
    }
    check("no struct hides state", structs > 0 && offenders.empty());
}

// Both halves of code-style.md's logging rule, which nothing else checks.
//
// A `.cpp` logging through the category-less macros takes its category from the
// `#define VKM_LOG_CATEGORY` at its top; without one its lines go out under no
// category, where no filter reaches them and nobody notices. Conversely, a file
// declaring a category and logging nothing carries an inert define. The
// explicit-category macros (`LOG_*_C`) name their own and are exempt from the first
// half - why header inline code uses them.
void testEveryLoggingFileDeclaresItsCategory() {
    std::printf("What the log lines are filed under:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(engineRoot / "src", ec)) {
        std::printf("  (no sources in this tree - nothing to check)\n");
        return;
    }

    const std::regex bare(R"RX(\bLOG_(?:TRACE|VERBOSE|INFO|WARNING|ERROR)\()RX");
    const std::regex tagged(R"RX(\bLOG_[A-Z]+_C\()RX");
    const std::regex declares(R"RX(#define\s+VKM_LOG_CATEGORY\b)RX");

    std::vector<std::string> offenders;
    size_t declaring = 0;
    for (const fs::path& file : engineAndProjectFiles(engineRoot)) {
        if (file.extension() != ".cpp") continue;

        const std::string text = readAll(file);
        const bool declared = std::regex_search(text, declares);
        const bool logsBare = std::regex_search(text, bare);
        const bool logsAny  = logsBare || std::regex_search(text, tagged);

        if (declared) ++declaring;
        if (logsBare && !declared) {
            offenders.push_back(file.filename().string() + " logs under no category");
        }
        if (declared && !logsAny) {
            offenders.push_back(file.filename().string() + " declares a category it never uses");
        }
    }

    std::printf("      %zu implementation file(s) declare a category\n", declaring);
    for (const std::string& one : offenders) {
        std::printf("      %s\n", one.c_str());
    }
    check("every file that logs declares the category it logs under", declaring > 0 && offenders.empty());
}

// A doc block's @param is the part the compiler never reads, so a renamed parameter
// leaves it documenting one that is not there. The same walk catches a block
// documenting nothing - stranded above a second block when its function moved.
//
// A block's declaration runs to the first `;` or to a `{` outside parentheses, so a
// default argument (`= {}`), a callable parameter (`std::function<void()>`) and a
// defaulted `ImVec2(0, 0)` stay inside it. A block followed by another documents
// nothing, and every name in it is reported.
void testEveryParamNamesAParameter() {
    std::printf("What the doc blocks claim their parameters are:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(engineRoot / "src", ec)) {
        std::printf("  (no sources in this tree - nothing to check)\n");
        return;
    }

    const auto declarationAfter = [](const std::string& text, size_t from) {
        int depth = 0;
        size_t at = from;
        for (; at < text.size(); ++at) {
            if (text.compare(at, 3, "/**") == 0) break;
            const char c = text[at];
            if (c == '(') ++depth;
            else if (c == ')') --depth;
            else if (c == ';') break;
            else if (c == '{' && depth <= 0) break;
        }
        return text.substr(from, at - from);
    };

    const std::regex block(R"RX(/\*\*[\s\S]*?\*/)RX");
    const std::regex documented(R"RX(@(?:param(?:\[[a-z,]+\])?|tparam)\s+(\w+))RX");

    std::vector<std::string> offenders;
    size_t documentedNames = 0;
    for (const fs::path& file : engineAndProjectFiles(engineRoot)) {
        const std::string text = readAll(file);
        for (std::sregex_iterator it(text.begin(), text.end(), block), end; it != end; ++it) {
            const std::string comment = it->str();
            const std::string decl = declarationAfter(text, it->position() + it->length());
            const size_t line = static_cast<size_t>(
                std::count(text.begin(), text.begin() + it->position(), '\n')
            ) + 1;

            for (std::sregex_iterator name(comment.begin(), comment.end(), documented), last;
                name != last; ++name) {
                const std::string spelled = (*name)[1].str();
                ++documentedNames;
                if (std::regex_search(decl, std::regex(R"(\b)" + spelled + R"(\b)"))) continue;
                offenders.push_back(
                    file.filename().string() + ":" + std::to_string(line) + " documents '" + spelled + "'"
                );
            }
        }
    }

    std::printf("      %zu parameter(s) documented\n", documentedNames);
    for (const std::string& one : offenders) {
        std::printf("      %s\n", one.c_str());
    }
    check("every documented parameter is a parameter", documentedNames > 0 && offenders.empty());
}

// Every namespace-scope function a `.cpp` defines, against the headers declaring one.
// code-style.md makes file-local helpers in an anonymous namespace an absolute; one
// outside still compiles and works, but has external linkage - it can collide at link
// time with a same-named helper elsewhere, and no reader can tell it is private. A
// helper placed past the file's first anonymous namespace is how that happens unmeant.
//
// tests/ is held to it too: suites share a binary, a test added at a file's end goes
// after the closing brace as readily as before, and a collision is a link error that
// does not say which two suites.
//
// A member definition is skipped by the `::` before its name; `main` is the one free
// function deliberately declared nowhere.
void testEveryFreeFunctionIsDeclaredOrFileLocal() {
    std::printf("What a .cpp offers the linker:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);
    std::error_code ec;
    if (!fs::exists(engineRoot / "src", ec)) {
        std::printf("  (no sources in this tree - nothing to check)\n");
        return;
    }

    std::vector<fs::path> files = engineAndProjectFiles(engineRoot);
    {
        const std::vector<fs::path> suite = suiteFiles(engineRoot);
        files.insert(files.end(), suite.begin(), suite.end());
    }

    // Every name any header uses as a callable, gathered once: a fresh regex over 1.5 MB
    // of headers, three hundred times, would make the suite an order of magnitude slower.
    std::set<std::string> declared;
    {
        const std::regex callable(R"RX(\b([A-Za-z_]\w*)\s*\()RX");
        for (const fs::path& file : files) {
            if (file.extension() != ".h") continue;
            const std::string text = readAll(file);
            for (std::sregex_iterator it(text.begin(), text.end(), callable), end; it != end; ++it) {
                declared.insert((*it)[1].str());
            }
        }
        // A suite's entry point is declared by its VKM_TEST_SUITES row, so its name never
        // appears in suites.h followed by a paren, and the pattern above cannot see it.
        const fs::path suitesHeader = engineRoot / "tests/suites.h";
        const std::regex suiteRow(R"RX(,\s*(run\w+)\s*,)RX");
        const std::vector<std::string> suites = macroRows(suitesHeader, "VKM_TEST_SUITES", suiteRow);
        for (const std::string& entry : suites) {
            declared.insert(entry);
        }
    }

    // Anonymous namespaces are what the rule asks for, so their contents go first:
    // brace-matched since they nest, blanked to one newline each so line numbers hold.
    const auto withoutAnonymousNamespaces = [](const std::string& text) {
        static const std::regex opener(R"RX(namespace\s*\{)RX");
        std::string out;
        size_t at = 0;
        std::smatch m;
        while (std::regex_search(text.cbegin() + static_cast<long>(at), text.cend(), m, opener)) {
            const size_t start = at + static_cast<size_t>(m.position());
            out.append(text, at, start - at);

            size_t scan = at + static_cast<size_t>(m.position() + m.length());
            int depth = 1;
            while (scan < text.size() && depth > 0) {
                if (text[scan] == '{') ++depth;
                else if (text[scan] == '}') --depth;
                ++scan;
            }
            const auto blockBegin = text.begin() + static_cast<long>(start);
            const auto blockEnd   = text.begin() + static_cast<long>(scan);
            out.append(static_cast<size_t>(std::count(blockBegin, blockEnd, '\n')), '\n');
            at = scan;
        }
        out.append(text, at, std::string::npos);
        return out;
    };

    // The prefix is everything before the name; a trailing `Class::` marks a member.
    // Without the `\b` the lazy prefix stops inside an identifier - `static_assert(`
    // becomes a prefix "s" and a "tatic_assert".
    const std::regex definition(R"RX(^([A-Za-z_][^(\n]*?)\b([A-Za-z_]\w*)\s*\()RX");
    const std::set<std::string> notFunctions = {
        "if", "for", "while", "switch", "return", "catch", "sizeof",
        "static_assert", "else", "do", "main",
    };

    std::vector<std::string> exposed;
    size_t examined = 0;
    for (const fs::path& file : files) {
        if (file.extension() != ".cpp") continue;

        // Code only: a brace in a string literal - a test's JSON - would end an
        // anonymous namespace early.
        std::istringstream in(withoutAnonymousNamespaces(codeOnly(readAll(file))));
        std::string line;
        int number = 0;
        while (std::getline(in, line)) {
            ++number;
            if (line.empty() || std::isspace(static_cast<unsigned char>(line[0]))) continue;
            if (!line.empty() && line.back() == ';') continue;

            std::smatch m;
            if (!std::regex_search(line, m, definition)) continue;

            const std::string prefix = m[1].str();
            const std::string name   = m[2].str();
            if (prefix.size() >= 2 && prefix.compare(prefix.size() - 2, 2, "::") == 0) continue;
            if (notFunctions.count(name) != 0) continue;

            ++examined;
            // File-local is the anonymous namespace's to say, never `static`'s.
            if (prefix.rfind("static ", 0) == 0) {
                exposed.push_back(
                    file.filename().string() + ":" + std::to_string(number)
                        + " defines " + name + " static, where the anonymous namespace is the rule"
                );
                continue;
            }
            if (declared.count(name) != 0) continue;
            exposed.push_back(
                file.filename().string() + ":" + std::to_string(number)
                    + " defines " + name + ", which no header declares"
            );
        }
    }

    std::printf("      %zu free function definition(s) examined\n", examined);
    for (const std::string& one : exposed) {
        std::printf("      %s\n", one.c_str());
    }
    check("every function a .cpp defines is either declared or file-local", examined > 0 && exposed.empty());
}

// A project.json or scene is written and read by the engine, so its bytes are the
// engine's. A copy in any other shape - other indent, no trailing newline, hand-reflowed
// - is rewritten line for line by the first save. Pinned: checked-in files already look
// like the next save, so a diff over one is only what somebody changed.
void testCheckedInDataFilesAreInTheFormatTheEngineWrites() {
    std::printf("What shape the checked-in data files are in:\n");

    const fs::path root = fs::path(VKM_ENGINE_DIR);
    std::vector<std::string> offenders;
    size_t checked = 0;

    for (const char* dir : {"examples", "templates"}) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root / dir, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path& path = it->path();
            if (!it->is_regular_file() || path.extension() != ".json") continue;

            // The two a person edits by hand. The rest of a project is generated, and a
            // stale one says only that its generator has not run since.
            const std::string within = "/" + fs::relative(path, root / dir).generic_string();
            if (within.find("/dist/") != std::string::npos) continue;
            const bool ours = path.filename() == "project.json"
                || within.find("/scenes/") != std::string::npos;
            if (!ours) continue;

            std::ifstream in(path, std::ios::binary);
            const std::string onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::string name = fs::relative(path, root).generic_string();

            const nlohmann::json doc = nlohmann::json::parse(onDisk, nullptr, false);
            if (doc.is_discarded()) {
                offenders.push_back(name + " (not JSON)");
                continue;
            }

            ++checked;
            // What io/json_file.h writes: two-space indent and a trailing newline, as
            // text files.
            if (onDisk != doc.dump(2) + "\n") offenders.push_back(name);
        }
    }

    std::printf("      %zu data file(s)\n", checked);
    for (const std::string& one : offenders) std::printf("      %s\n", one.c_str());
    check(
        "every checked-in data file is written the way the engine writes one",
        checked > 0 && offenders.empty()
    );
}

// `near`, `far` and `pascal` are empty macros in windef.h, so on Windows a local of
// those names vanishes and the next line is an inexplicable syntax error.
// platform/windows_api.h undefines all three, but a guard is only as good as the
// include order around it. So the rule is the name itself: nothing the build compiles
// uses them, which is why this reads tests/ and the projects too.
void testNoIdentifierIsAWindowsMacro() {
    std::printf("Names windef.h has already taken:\n");

    const fs::path engineRoot(VKM_ENGINE_DIR);

    std::vector<fs::path> files = engineAndProjectFiles(engineRoot);
    {
        const std::vector<fs::path> suite = suiteFiles(engineRoot);
        files.insert(files.end(), suite.begin(), suite.end());
    }

    // A declaration or a use as an object: `Type near;`, `near.field`, `near =`.
    // windows_api.h names all three in its #undef lines - the fix, not the problem.
    const std::regex named(R"((?:^|[\s(,*&])(near|far|pascal)\s*(?:[;,).=(]|\.[A-Za-z_]))");

    std::vector<std::string> offenders;
    for (const fs::path& file : files) {
        const std::string name = file.filename().string();
        if (name == "windows_api.h") continue;

        const std::string text = codeOnly(readAll(file));
        std::istringstream lines(text);
        std::string line;
        uint32_t number = 0;
        while (std::getline(lines, line)) {
            ++number;
            if (!std::regex_search(line, named)) continue;
            offenders.push_back(name + ":" + std::to_string(number) + ": " + line.substr(0, 60));
        }
    }

    for (const std::string& offender : offenders) std::printf("      %s\n", offender.c_str());
    std::printf("      %zu file(s) read\n", files.size());
    check("nothing in the tree is named near, far or pascal", offenders.empty());
}

} // namespace

void runDocsTests() {
    testTheManualCitesNoLineNumbers();
    testTheManualIsAscii();
    testTheSourceIsAscii();
    testTheManualLinksResolve();
    testTheManualAnchorsResolve();
    testTheManualNamesRealFiles();
    testTheManualsPassTableIsTheRegistrationOrder();
    testTheManualDocumentsEveryComponent();
    testTheRenderViewContractTableIsTheStruct();
    testTheHierarchyApiTablesAreTheHeader();
    testEveryNameInADocSampleExists();
    testEveryQualifiedNameInTheManualExists();
    testTheIndexesListEveryPage();
    testTheApiPagesNameRealPaths();
    testTheBackendPointsAtRealShaders();
    testEveryFoggedProgramIsToldAboutTheFog();
    testEverySamplerTakesItsUnitFromThePrelude();
    testUndoRestoresEveryComponent();
    testTheInspectorHasACardForEveryComponent();
    testEveryRenderSettingIsPersistedExactlyOnce();
    testEverySuiteIsAFileAndALine();
    testTheManualsDirectoryMapIsTheTree();
    testEveryWayOfMakingAProjectSkipsTheSameNames();
    testTheModuleListsAreTheModules();
    testEveryTestFunctionIsActuallyRun();
    testNoEngineHeaderCarriesAThreadLocal();
    testSourceCommentsNameRealFiles();
    testSourceCommentsNameRealSymbols();
    testNoCommentNarratesHistory();
    testNoCommentInsideAnArgumentList();
    testABrokenListBreaksWhole();
    testEveryClassWritesItsFive();
    testAStructHidesNothing();
    testEveryLineFits();
    testEveryFreeFunctionIsDeclaredOrFileLocal();
    testCheckedInDataFilesAreInTheFormatTheEngineWrites();
    testEveryParamNamesAParameter();
    testEveryLoggingFileDeclaresItsCategory();
    testNoIdentifierIsAWindowsMacro();
}
