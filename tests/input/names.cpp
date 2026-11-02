// Keys and buttons by name: what a controls screen shows, and what project.json and a player's file hold.

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "io/project/project.h"
#include "platform/input/input_binding.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using namespace Vkm::Platform;

namespace {

std::vector<InputBinding> everyKey() {
    std::vector<InputBinding> keys;
    for (size_t at = 0; at < static_cast<size_t>(Key::Count); ++at) {
        keys.push_back(InputBinding::key(static_cast<Key>(at)));
    }
    return keys;
}

std::vector<InputBinding> everyButton() {
    std::vector<InputBinding> buttons;
    for (size_t at = 0; at < static_cast<size_t>(MouseButton::Count); ++at) {
        buttons.push_back(InputBinding::mouse(static_cast<MouseButton>(at)));
    }
    return buttons;
}

/// Whether @p a and @p b bind the same control at the same scale.
bool same(const std::vector<InputBinding>& a, const std::vector<InputBinding>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].control != b[i].control || a[i].scale != b[i].scale) return false;
    }
    return true;
}

} // namespace

TEST("every key and mouse button has a name of its own, and its name reads back as it") {
    std::vector<InputBinding> controls = everyKey();
    for (const InputBinding& button : everyButton()) controls.push_back(button);

    std::set<std::string> names;
    for (const InputBinding& control : controls) {
        const std::string name = bindingName(control);
        CHECK(!name.empty());
        CHECK(name.rfind("Key ", 0) == std::string::npos);
        CHECK(names.insert(name).second);
        const std::optional<InputBinding> read = bindingNamed(name);
        REQUIRE(read.has_value());
        CHECK(read->control == control.control);
        CHECK(read->scale == 1.0f);
    }
    CHECK(names.size() == controls.size());

    // The names a project already holds stay, and the keys that had none have their own.
    CHECK(bindingName(InputBinding::key(Key::W)) == "W");
    CHECK(bindingName(InputBinding::key(Key::Digit0)) == "0");
    CHECK(bindingName(InputBinding::key(Key::Apostrophe)) == "'");
    CHECK(bindingName(InputBinding::key(Key::LeftShift)) == "Left Shift");
    CHECK(bindingName(InputBinding::key(Key::RightControl)) == "Right Ctrl");
    CHECK(bindingName(InputBinding::key(Key::Keypad7)) == "Keypad 7");
    CHECK(bindingName(InputBinding::key(Key::KeypadEnter)) == "Keypad Enter");
    CHECK(bindingName(InputBinding::key(Key::F12)) == "F12");
    CHECK(bindingName(InputBinding::mouse(MouseButton::Left)) == "Left mouse");
    CHECK(bindingName(InputBinding::mouse(MouseButton::Button4)) == "Mouse 4");
    CHECK(bindingName(InputBinding::key(Key::World1)) == "World 1");
    CHECK(bindingName(InputBinding::key(Key::PrintScreen)) == "Print Screen");
    CHECK(bindingName(InputBinding::key(Key::KeypadAdd)) == "Keypad +");
    CHECK(bindingName(InputBinding::key(Key::RightSuper)) == "Right Super");
    CHECK(bindingName(InputBinding::key(Key::Menu)) == "Menu");

    // Names are exact, and a number is no name.
    CHECK(!bindingNamed("w").has_value());
    CHECK(!bindingNamed("").has_value());
    CHECK(!bindingNamed("Key 87").has_value());
    CHECK(!bindingNamed("Mouse 9").has_value());
}

TEST("project.json keeps every key and button by name, and each scale, through a save and a load") {
    const std::filesystem::path root = openProject();
    Project project;
    project.actions = {
        {"Every/Button", everyButton()},
        {"Every/Key", everyKey()},
        {"Move/Right", {InputBinding::key(Key::D), InputBinding::key(Key::A, -1.0f)}},
        {
            "Zoom",
            {InputBinding::mouse(MouseButton::Middle, 0.5f), InputBinding::key(Key::KeypadSubtract, -0.5f)},
        },
    };
    REQUIRE(saveProject(root, project));

    const nlohmann::json file = nlohmann::json::parse(readText(root / "project.json"));
    REQUIRE(file["actions"]["Every/Key"].size() == static_cast<size_t>(Key::Count));
    CHECK(file["actions"]["Every/Key"][0] == "Space");
    CHECK(file["actions"]["Every/Key"][static_cast<size_t>(Key::Menu)] == "Menu");
    CHECK(file["actions"]["Every/Button"][1] == "Right mouse");
    CHECK(file["actions"]["Move/Right"] == nlohmann::json::parse(R"(["D", {"bind": "A", "scale": -1.0}])"));
    CHECK(file["actions"]["Zoom"][1] == nlohmann::json::parse(R"({"bind": "Keypad -", "scale": -0.5})"));

    Project again;
    REQUIRE(loadProject(root, again));
    REQUIRE(again.actions.size() == project.actions.size());
    for (size_t i = 0; i < project.actions.size(); ++i) {
        CHECK(again.actions[i].name == project.actions[i].name);
        CHECK(same(again.actions[i].bindings, project.actions[i].bindings));
    }
    CHECK(countInLog("skipped") == 0);
}
