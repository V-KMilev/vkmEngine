// A game's own files - a player's settings, a save - kept by their field names in the player's own
// folder, written whole or not at all.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "base/reflect/enum_names.h"
#include "io/json/own_file.h"
#include "io/json/reflect_json.h"
#include "io/project/roots.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using nlohmann::json;

namespace SaveProbe {

enum class Difficulty { Easy, Hard, Count };

struct Options {
    bool  subtitles = false;
    float volume    = 1.0f;
};

struct Progress {
    int                      level = 1;
    std::string              hero  = "nobody";
    glm::vec3                spawn = {0.0f, 0.0f, 0.0f};
    Difficulty               mode  = Difficulty::Easy;
    std::vector<std::string> keys;
    Options                  options;
    std::vector<Options>     earlier;
};

} // namespace SaveProbe

VKM_ENUM_NAMES(::SaveProbe::Difficulty, "Easy", "Hard")

VKM_REFLECT_BEGIN(::SaveProbe::Options)
    VKM_F(subtitles)
    VKM_F(volume)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::SaveProbe::Progress)
    VKM_F(level)
    VKM_F(hero)
    VKM_F(spawn)
    VKM_F(mode)
    VKM_F(keys)
    VKM_F(options)
    VKM_F(earlier)
VKM_REFLECT_END()

using SaveProbe::Progress;

TEST("a game's own file is kept by its field names in the player's folder and read back the same") {
    const std::filesystem::path root = openProject();
    writeText(root / "project.json", R"({"name": "Tower Climb"})");

    // The player's folder is the game's own, under the user's settings, not the project.
    const std::filesystem::path file = Roots::playerFile("progress.json");
    CHECK(file.parent_path() == Roots::playerRoot("Tower Climb"));
    CHECK(file.parent_path().filename() == "Tower Climb");
    CHECK(file.string().rfind((scratch() / "home").string(), 0) == 0);
    CHECK(Roots::playerRoot("../../Tower Climb/") == Roots::playerRoot("Tower Climb"));
    CHECK(Roots::playerRoot("") == root);

    Progress saved;
    saved.level   = 7;
    saved.hero    = "Ada";
    saved.spawn   = {1.5f, 0.0f, -2.0f};
    saved.mode    = SaveProbe::Difficulty::Hard;
    saved.keys    = {"red", "blue"};
    saved.options = {true, 0.25f};
    saved.earlier = {{false, 0.5f}, {true, 0.75f}};
    REQUIRE(writeOwnFile(file, ReflectJson::toJson(saved)));

    Progress read;
    ReflectJson::fromJson(readOwnFile(file), read);
    CHECK(read.level == 7);
    CHECK(read.hero == "Ada");
    CHECK(read.spawn == saved.spawn);
    CHECK(read.mode == SaveProbe::Difficulty::Hard);
    CHECK(read.keys == saved.keys);
    CHECK(read.options.subtitles);
    CHECK(read.options.volume == 0.25f);
    REQUIRE(read.earlier.size() == 2u);
    CHECK(read.earlier[1].volume == 0.75f);
    CHECK(json::parse(readText(file))["mode"] == "Hard");
}

TEST("a game that writes nothing leaves no player folder, and its first file written makes one") {
    const std::filesystem::path root = openProject();
    writeText(root / "project.json", R"({"name": "Tower Climb"})");

    const std::filesystem::path folder = Roots::playerRoot("Tower Climb");
    CHECK(!std::filesystem::exists(folder));
    const std::filesystem::path file = Roots::playerFile("progress.json");
    CHECK(readOwnFile(file).empty());
    CHECK(!std::filesystem::exists(folder));

    REQUIRE(writeOwnFile(file, json{{"level", 2}}));
    CHECK(std::filesystem::is_directory(folder));
    CHECK(readOwnFile(file)["level"] == 2);
}

TEST("an own file from an older build, or with a field gone wrong, keeps what it cannot read") {
    const std::filesystem::path file = scratch() / "save.json";
    // Written before `keys` and `options` existed, one field of the wrong type, one enum no build names.
    writeText(file, R"({"level": "seven", "hero": "Ada", "mode": "Nightmare", "earlier": [{"volume": "loud"}]})");
    Progress read;
    read.earlier = {{true, 0.1f}};
    ReflectJson::fromJson(readOwnFile(file), read);
    CHECK(read.level == 1);
    CHECK(read.hero == "Ada");
    CHECK(read.mode == SaveProbe::Difficulty::Easy);
    CHECK(read.options.volume == 1.0f);
    // A list wrong anywhere keeps the list it had, not half of the file's.
    REQUIRE(read.earlier.size() == 1u);
    CHECK(read.earlier[0].volume == 0.1f);
    CHECK(countInLog("'level' holds a value of the wrong type") == 1);
    CHECK(countInLog("'earlier' holds a value of the wrong type") == 1);
    CHECK(countInLog("Nightmare") == 1);

    // None yet is a first run, said nothing of; one that will not parse is a fresh start, said once.
    CHECK(readOwnFile(scratch() / "none.json") == json::object());
    writeText(file, "[1, 2");
    CHECK(readOwnFile(file) == json::object());
    writeText(file, "[1, 2]");
    CHECK(readOwnFile(file) == json::object());
    CHECK(countInLog("starting afresh") == 2);
}

TEST("an own file is written whole or not at all") {
    const std::filesystem::path folder = scratch() / "saves";
    const std::filesystem::path file   = folder / "slot1.json";
    REQUIRE(writeOwnFile(file, {{"level", 3}}));
    // Written again it is replaced whole, the new file taking the old one's place.
    REQUIRE(writeOwnFile(file, {{"level", 4}}));
    const std::string before = readText(file);
    CHECK(json::parse(before)["level"] == 4);

#if !defined(_WIN32)
    // A folder that takes no new file: the write fails, the file is as it was, nothing beside it. Windows
    // has no such folder for its owner: a folder's read-only mark does not stop a file being made in it.
    std::filesystem::permissions(
        folder,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec
    );
    const bool wrote = writeOwnFile(file, {{"level", 5}});
    std::filesystem::permissions(folder, std::filesystem::perms::owner_all);
    CHECK(!wrote);
    CHECK(readText(file) == before);
#endif

    // A file that cannot be replaced - a folder stands in its place - is not half written either.
    const std::filesystem::path blocked = folder / "slot2.json";
    std::filesystem::create_directories(blocked);
    CHECK(!writeOwnFile(blocked, {{"level", 6}}));
    CHECK(std::filesystem::is_directory(blocked));
    int entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) entries += entry.path() != blocked;
    CHECK(entries == 1);
}
