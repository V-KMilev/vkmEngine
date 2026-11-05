// The renderer's programs on this machine's driver: each compiles, and one edited on disk is taken up; and
// the constants put before each of them.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <regex>
#include <string>

#include "gl_shader_constants.h"
#include "system/render/editor_render_hooks.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

TEST("every program the renderer starts with compiles on this driver, and a frame draws with no error said") {
    Screen screen;
    yard(screen.scene(), screen.resources());
    const Picture frame = screen.shot();

    const RenderReadiness ready = screen.readiness();
    CHECK(!ready.compiling());
    CHECK(ready.ready());
    CHECK(lit(frame) > 0.9f);
    CHECK(countInLog("ERROR") == 0);
    if (countInLog("ERROR") != 0) std::printf("%s", logText().c_str());
}

TEST("a shader edited on disk is taken up and changes the frame, and an edit that does not compile keeps the last") {
    // The shaders are read relative to the working directory: a copy of them, so the edit is this test's.
    const std::filesystem::path copy = scratch() / "tree";
    std::filesystem::create_directories(copy);
    std::filesystem::copy(
        engineDir() / "shaders",
        copy / "shaders",
        std::filesystem::copy_options::recursive
    );
    std::filesystem::current_path(copy);
    const std::filesystem::path shaders   = copy / "shaders" / "opengl";
    const std::filesystem::path composite = shaders / "post" / "composite" / "fragment.shader";
    const std::string original = readText(composite);
    const std::string last = "FragColor = vec4(u_dither == 1 ? displayDither(display, gl_FragCoord.xy) : display, 1.0);";
    REQUIRE(original.find(last) != std::string::npos);

    Screen screen;
    const Yard seen = yard(screen.scene(), screen.resources());
    // Polled every frame, as the editor's watcher polls; the first poll is where it starts from.
    const auto write = [&](const std::string& output) {
        std::string edited = original;
        edited.replace(edited.find(last), last.size(), output);
        writeText(composite, edited);
    };
    uint32_t swapped = 0, broken = 0;
    std::vector<Picture> frames;
    screen.play(PAST_THE_IDENT + 6, [&](int frame) {
        const uint32_t reloaded = screen.backend().editorHooks()->reloadChangedShaders();
        const int after = frame - PAST_THE_IDENT;
        if (after == 1) swapped = reloaded;
        if (after == 4) broken = reloaded;
        if (after == 0 || after == 3 || after == 6) frames.push_back(screen.picture());
        if (after == 0) write("FragColor = vec4(display.bgr, 1.0);");
        if (after == 3) write("FragColor = vec4(display.bgr, 1.0) +;");
    }, 16);

    REQUIRE(frames.size() == 3u);
    const Rect red = Rect::around(pixelOf(screen.scene(), seen.eye, {-1.6f, 0.5f, 0.5f}), 4);
    CHECK(swapped >= 1u);
    CHECK(frames[0].mean(red).r > 2.0f * frames[0].mean(red).b);
    CHECK(frames[1].mean(red).b > 2.0f * frames[1].mean(red).r);
    // Every other program is built again; the one that fails keeps what it had, and says so.
    CHECK(broken + 1 == swapped);
    CHECK(differing(frames[1], frames[2]) < 0.001f);
    CHECK(countInLog("1 kept their previous program") == 1);
}

TEST("every float the shaders' constants state is written to the nine digits a float needs to read back as itself") {
    // A whole number is exact however few its digits; any other value needs nine significant ones.
    const std::string prelude = shaderConstants();
    const std::regex  literal(R"((?:const (?:float|vec\d)\s+\w+\s*=|vec\d\(|,)\s*(-?[0-9]+\.[0-9]+(?:e[-+]?[0-9]+)?))");
    int read = 0;
    int cut  = 0;
    for (std::sregex_iterator it(prelude.begin(), prelude.end(), literal), end; it != end; ++it) {
        const std::string number = (*it)[1];
        const double      value  = std::stod(number);
        ++read;
        if (value == std::floor(value)) continue;
        std::string digits;
        for (const char c : number.substr(0, number.find('e'))) {
            if (c >= '0' && c <= '9' && !(digits.empty() && c == '0')) digits += c;
        }
        if (digits.size() < 9) {
            std::printf("  cut short: %s\n", number.c_str());
            ++cut;
        }
    }
    CHECK(read > 20);
    CHECK(cut == 0);
}
