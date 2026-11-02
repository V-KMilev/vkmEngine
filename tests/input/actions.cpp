// Actions a game names, read off the keys a player presses: per frame, per tick, and as the player
// rebinds them.

#include <cstring>
#include <filesystem>

#include "core/host_chrome.h"
#include "core/input_map.h"
#include "vkm_platform/input/input_handle.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using namespace Vkm::Platform;

namespace {

/**
 * @brief A player's keyboard and mouse, read the way a frame reads them.
 */
struct Keyboard {
    InputMap    map;
    InputHandle device;
    HostChrome  chrome;

    Keyboard() {
        map.setActions({
            {"Move/Forward", {InputBinding::key(Key::W), InputBinding::key(Key::S, -1.0f)}},
            {"Move/Right", {InputBinding::key(Key::D), InputBinding::key(Key::A, -1.0f)}},
            {"Fire", {InputBinding::mouse(MouseButton::Left), InputBinding::key(Key::F)}},
            {"Boost", {InputBinding::key(Key::E, 0.75f), InputBinding::key(Key::R, 0.75f)}},
        });
    }

    void key(Key pressed, bool down) { device.onKeyEvent(pressed, down); }
    void frame() { map.update(device, chrome); }
};

} // namespace

TEST("an action reads held, pressed and released from its keys and buttons, each edge on one frame") {
    Keyboard input;
    input.key(Key::W, true);
    input.frame();
    CHECK(input.map.held("Move/Forward"));
    CHECK(input.map.pressed("Move/Forward"));
    input.frame();
    CHECK(input.map.held("Move/Forward"));
    CHECK(!input.map.pressed("Move/Forward"));
    input.key(Key::W, false);
    input.frame();
    CHECK(!input.map.held("Move/Forward"));
    CHECK(input.map.released("Move/Forward"));
    input.frame();
    CHECK(!input.map.released("Move/Forward"));

    // A click pressed and let go between two frames is still a press, and its release the next.
    input.device.setButton(MouseButton::Left, true);
    input.device.setButton(MouseButton::Left, false);
    input.frame();
    CHECK(input.map.pressed("Fire"));
    input.frame();
    CHECK(input.map.released("Fire"));
    CHECK(!input.map.held("Unlisted"));
    CHECK(input.map.axis("Unlisted") == 0.0f);
}

TEST("opposing keys cancel, and bindings sum to at most one") {
    Keyboard input;
    input.key(Key::D, true);
    input.key(Key::A, true);
    input.key(Key::E, true);
    input.frame();
    CHECK(input.map.axis("Move/Right") == 0.0f);
    CHECK(!input.map.held("Move/Right"));
    CHECK(input.map.axis("Boost") == 0.75f);
    input.key(Key::R, true);
    input.key(Key::A, false);
    input.frame();
    CHECK(input.map.axis("Boost") == 1.0f);
    CHECK(input.map.axis("Move/Right") == 1.0f);
    input.key(Key::D, false);
    input.key(Key::A, true);
    input.frame();
    CHECK(input.map.axis("Move/Right") == -1.0f);
}

TEST("a rebound action takes its new key, names it, and the choice is kept in the player's file") {
    const std::filesystem::path file = scratch() / "controls.json";
    {
        Keyboard input;
        input.map.keepBindingsIn(file);
        input.map.rebind("Move/Forward", {InputBinding::key(Key::Up)});
        input.key(Key::Up, true);
        input.frame();
        CHECK(input.map.held("Move/Forward"));
        CHECK(input.map.keyName("Move/Forward") == bindingName(InputBinding::key(Key::Up)));
        input.key(Key::W, true);
        input.key(Key::Up, false);
        input.frame();
        CHECK(!input.map.held("Move/Forward"));
        input.map.rebind("Nothing", {InputBinding::key(Key::X)});
        CHECK(countInLog("Nothing") == 1);
    }
    CHECK(std::filesystem::exists(file));
    {
        Keyboard again;
        again.map.keepBindingsIn(file);
        again.key(Key::Up, true);
        again.frame();
        CHECK(again.map.held("Move/Forward"));
        // Untouched actions keep their defaults, and resetting forgets the choice.
        CHECK(again.map.bindings("Move/Right").size() == 2);
        again.map.resetBindings();
        again.frame();
        CHECK(!again.map.held("Move/Forward"));
    }
    Keyboard third;
    third.map.keepBindingsIn(file);
    third.key(Key::W, true);
    third.frame();
    CHECK(third.map.held("Move/Forward"));
}

TEST("the next key struck is told, for a controls screen to bind") {
    Keyboard input;
    input.frame();
    CHECK(!input.map.struck());
    input.key(Key::Q, true);
    input.frame();
    REQUIRE(input.map.struck().has_value());
    CHECK(input.map.struck()->is(Key::Q));
    input.frame();
    CHECK(!input.map.struck());
}

TEST("a tick's command carries every press and release since the last tick, once") {
    Keyboard input;
    // Struck and let go within frames between two ticks.
    input.map.beginTick(1);
    input.key(Key::F, true);
    input.frame();
    input.key(Key::F, false);
    input.frame();
    input.map.beginTick(2);
    const int fire = input.map.indexOf("Fire");
    REQUIRE(fire >= 0);
    const InputCommand first = input.map.command();
    CHECK(first.tick == 2);
    CHECK((first.pressed & (1u << fire)) != 0);
    CHECK((first.released & (1u << fire)) != 0);
    input.frame();
    input.map.beginTick(3);
    CHECK((input.map.command().pressed & (1u << fire)) == 0);
    CHECK(input.map.command().sequence > first.sequence);
    // Held across ticks, the axis rides every command.
    input.key(Key::W, true);
    input.frame();
    input.map.beginTick(4);
    input.frame();
    input.map.beginTick(5);
    CHECK(input.map.command().axis[input.map.indexOf("Move/Forward")] == 1.0f);
}

TEST("a command carries up to 16 bytes of the game's own on every tick, and refuses more") {
    Keyboard input;
    const uint8_t item[3] = {9, 8, 7};
    CHECK(input.map.setPayload(item, sizeof item));
    input.map.beginTick(1);
    input.map.beginTick(2);
    CHECK(input.map.command().payloadSize == 3);
    CHECK(input.map.command().payload[2] == 7);
    uint8_t tooMany[17] = {};
    CHECK(!input.map.setPayload(tooMany, sizeof tooMany));
    input.map.beginTick(3);
    CHECK(input.map.command().payloadSize == 3);
    CHECK(input.map.setPayload(tooMany, 16));
    CHECK(input.map.setPayload(nullptr, 0));
    input.map.beginTick(4);
    CHECK(input.map.command().payloadSize == 0);
}

TEST("two ends listing the same actions in any order agree on every slot and fingerprint") {
    InputMap one, two;
    one.setActions({{"Jump", {}}, {"Crouch", {}}, {"Wave", {}}});
    two.setActions({{"Wave", {}}, {"Jump", {}}, {"Crouch", {}}});
    CHECK(one.actionFingerprint() == two.actionFingerprint());
    for (const char* name : {"Jump", "Crouch", "Wave"}) CHECK(one.indexOf(name) == two.indexOf(name));

    // A list set again, or reset, is as a fresh map's.
    InputMap fresh;
    fresh.setActions({{"Jump", {}}, {"Crouch", {}}, {"Wave", {}}});
    two.setActions({{"Other", {}}});
    two.setActions({{"Jump", {}}, {"Crouch", {}}, {"Wave", {}}});
    CHECK(two.actionFingerprint() == fresh.actionFingerprint());
    CHECK(two.indexOf("Wave") == fresh.indexOf("Wave"));
    one.reset();
    CHECK(one.actionFingerprint() == fresh.actionFingerprint());
    CHECK(one.actionCount() == fresh.actionCount());

    InputMap other;
    other.setActions({{"Jump", {}}, {"Crouch", {}}});
    CHECK(other.actionFingerprint() != fresh.actionFingerprint());
}

TEST("typed text and a key's repeats reach the frame, and a host that takes the keyboard takes them") {
    Keyboard input;
    input.device.onText(U'h');
    input.device.onText(U'é');
    input.key(Key::Backspace, true);
    input.key(Key::Backspace, true);  // the platform's repeat
    input.frame();
    CHECK(input.map.text() == std::u32string_view(U"hé"));
    CHECK(input.map.typed(Key::Backspace));
    input.frame();
    CHECK(input.map.text().empty());

    input.chrome.captureKeyboard = true;
    input.device.onText(U'x');
    input.key(Key::W, true);
    input.frame();
    CHECK(input.map.text().empty());
    CHECK(!input.map.held("Move/Forward"));
}
