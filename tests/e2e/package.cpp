// `vkm package` run as a user runs it, on a project whose art was imported: the game carries what it
// reads, and none of what it was made from.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "e2e/game_project.h"
#include "e2e/hosts.h"
#include "fixture/fixture.h"
#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

const std::vector<unsigned char> ONE_PIXEL = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
    0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xDD, 0x8D, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
    0x44, 0xAE, 0x42, 0x60, 0x82,
};  ///< A PNG of one red pixel: source art a texture is imported from.

int filesIn(const std::filesystem::path& folder, const std::string& extension) {
    int count = 0;
    std::error_code ec;
    for (const auto& file : std::filesystem::recursive_directory_iterator(folder, ec)) {
        count += file.path().extension() == extension;
    }
    return count;
}

} // namespace

TEST("vkm package ships the game, its cooked assets and its own files, and leaves the source art behind") {
#if !defined(VKM_TEST_PYTHON)
    skip("no Python was found to run vkm with");
#else
    // vkm drives the engine tree's build/, which is this one only there.
    const std::filesystem::path engine = engineDir();
    std::error_code ec;
    if (!std::filesystem::equivalent(engine / "build" / "bin", VKM_TEST_HOST_DIR, ec)) {
        skip("vkm drives " + (engine / "build").string() + ", and this is another build");
    }

    const std::filesystem::path root = openProject("art");
    writeProjectFile("scenes/main.json", R"("version": "2.0.0")");
    writeText(root / "assets" / "art" / "stone.png", std::string(ONE_PIXEL.begin(), ONE_PIXEL.end()));
    writeText(root / "assets" / "notes" / "credits.txt", "Stone by nobody.\n");
    REQUIRE(AssetLibrary::writeRecipe(
        AssetType::Texture,
        "assets/art/stone.png",
        {{"kind", "file"}, {"path", "assets/art/stone.png"}, {"usage", "Color"}}
    ));
    writeMeshRecipe("game:crate", "cube");
    authorScene("scenes/main.json", {{"Crate", "game:crate", {0.0f, 0.5f, 0.0f}, 1.0f}});
    installModule("game_v1");

    const Ran packaged = runProgram(VKM_TEST_PYTHON, {(engine / "tools" / "vkm.py").string(), "package",
                                                      root.string(), "--development", "-j", "4"});
    REQUIRE(packaged.ended);
    CHECK(packaged.exit == 0);
    if (packaged.exit != 0) std::printf("%s", packaged.output.c_str());

    // dist/<name>-<version>-<platform>, and its symbols beside it.
    std::filesystem::path game;
    for (const auto& made : std::filesystem::directory_iterator(root / "dist", ec)) {
        const std::string name = made.path().filename().string();
        if (name.rfind(
            "fixture-2.0.0-",
            0
        ) == 0 && name.find("-symbols") == std::string::npos) game = made.path();
    }
    REQUIRE(!game.empty());
    const std::string executable = std::string("bin/fixture") + VKM_TEST_HOST_SUFFIX;
    const std::vector<std::string> parts = {
        executable,
        "project.json",
        "scenes/main.json",
        "cooked/_manifest.json",
        "shaders",
        "assets/notes/credits.txt",
        "licenses/vkmEngine.txt",
        "licenses/glfw.txt",
        "licenses/mikktspace.txt",
    };
    for (const std::string& part : parts) {
        CHECK(std::filesystem::exists(game / part));
        if (!std::filesystem::exists(game / part)) std::printf("  the package has no %s\n", part.c_str());
    }
    CHECK(std::filesystem::exists(game / "bin" / Roots::gameplayModule().filename()));
    CHECK(filesIn(game / "cooked", ".vkmc") == 2);
    for (const char* part : {"assets/art", "library/textures", "library/meshes", "logs"}) {
        CHECK(!std::filesystem::exists(game / part));
        if (std::filesystem::exists(game / part)) std::printf("  the package carries %s\n", part);
    }
    int copies = 0;
    for (const auto& file : std::filesystem::directory_iterator(game / "bin", ec)) {
        copies += file.path().filename().string().find(".loaded.") != std::string::npos;
    }
    CHECK(copies == 0);
#endif
}
