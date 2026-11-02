# Input

Gameplay reads named actions - `"Jump"`, `"Move/Forward"` - never keys. The key is named once,
where the game lists its actions in `project.json`, so a controls screen can rebind it and one
action can take several keys and mouse buttons.

A behavior asks its own `pressed`, `held`, `released` and `axis`, which read the right input in
every hook: inside the fixed tick, this entity's tick command; anywhere else, this frame's.

## Listing actions

In `project.json`, under `actions`: each action's name and the controls that feed it, each by
the name `bindingName` gives it. Project Settings' Input card edits the list.

```json
"actions": {
    "Fire":       ["Left mouse"],
    "Jump":       ["Space", "W"],
    "Move/Right": ["D", {"bind": "A", "scale": -1}]
}
```

`axis()` sums every held binding's `scale`, clamped to -1..1, so opposing scales make an axis
that reads zero with both held; `held`, `pressed` and `released` read the same sum
(`InputMap::held` gives the threshold). Keyboard and mouse are the only sources. A binding no
key or button is called is skipped with a warning, and **an action read but never listed warns
once**, by name, and reads as never pressed.

Every host applies the list before its world starts (`bootProjectWorld`, which calls
`InputMap::setActions`), and the editor again when Project Settings is saved, so every end of a
session holds the same actions in the same command slots from its first packet
([networking.md](networking.md#the-handshake)).

### Rebinding

A controls screen changes what feeds an action with `InputMap::rebind`, listens for the
player's choice with `InputMap::struck` - the key or button that went down this frame, bound or
not - and names it with `bindingName`; [recipes.md](../recipes.md#state-and-the-wire) has the
two lines. The runtime keeps the player's bindings in `bindings.json` in `Roots::playerRoot`
(`InputMap::keepBindingsIn`), only those that differ from the defaults, so an untouched key
follows a later default.

## Keys and buttons

A key is a `Platform::Key` and a mouse button a `Platform::MouseButton`
(vkmPlatform's `input/key.h`): the engine's own names, never the window library's codes. A key is
named by where it sits, the one a US layout prints there. A file that names one says
`using namespace Vkm::Platform;` beside `Vkm::Engine`:

```cpp
InputBinding::key(Key::W);               // "W" in project.json
InputBinding::mouse(MouseButton::Left);  // "Left mouse"
if (input().typed(Key::Backspace)) erase();
```

A binding holds one or the other (`InputControl`), so a key is never read as a button;
`InputBinding::is` asks which it is. Each has one name, which `bindingName` gives and
`project.json` and a player's bindings file hold: "Space", "W", "Left Shift", "Keypad +",
"F12", "Left mouse", "Mouse 4". The table is `input_binding.h`'s. A name no key or button has
reads as none.

The window turns GLFW's codes into these in one place (`keyFromGlfw` and
`mouseButtonFromGlfw`, in vkmPlatform's `window_manager.cpp`), and drops a code no key has.

## Reading it on the frame clock

In `onUpdate` or `onRealtimeUpdate`, `held`, `pressed`, `released` and `axis` answer for this
frame, and `input()` adds the pointer in window pixels, its movement and the wheel
(`InputMap::pointer`, `InputMap::pointerDelta`, `InputMap::wheel`). Typed text, for a text
field: [ui.md](ui.md#text-input).

The map is sampled once a frame before any system runs, so every reader agrees, and a tap
shorter than a frame still reads as pressed for one.

**A device someone else holds reads as untouched**: in the editor, the keys while a panel's
field has the keyboard, and the mouse's buttons, wheel and movement while a panel has the
pointer ([editor.md](editor.md#who-has-the-pointer-and-the-keyboard)); in every host, to every
action but `UI/Click`, a click that begins over the game's own UI ([ui.md](ui.md#interaction)).

**Mouse-look locks the cursor**: `window()->setCursorMode(CursorMode::Disabled)`
(`vkm_platform/window/window_manager.h`). `pointerDelta()` keeps reporting movement, and in the
editor Stop hands the cursor back.

## Reading it on the tick

In `onFixedUpdate` the same calls read this entity's tick command:

```cpp
void Player::onFixedUpdate(float dt) {
    const float move = axis("Move/Right");
    if (pressed("Jump")) jump();
}
```

**A tick must not read the frame.** Input arrives per frame and simulation runs per tick, so a
frame's answer **drops a tap** that fell between two ticks and **repeats a press** on every tick
of a slow frame. A command holds the axes as they stand plus every edge since the previous
tick, and it is what gets replayed for client-side prediction
([networking.md](networking.md)). `InputMap::pressed(command, action)` and its siblings ask a
command outside a behavior. Whose command `command()` is on each end:
[networking.md](networking.md#the-five-questions).

### Camera-relative movement

The camera turns on the frame clock, so a tick cannot ask it. Each frame, before the ticks, the
engine hands the input the active camera's view as the last frame showed it, and every command
carries it (`InputCommand::view`), so a tick steers by the view its input was taken with:

```cpp
body->moveInput = alongView(axis("Move/Right"), axis("Move/Forward")) * speed;
```

`alongView` lays the view level - looking straight down, the top of the screen is ahead - and
keeps a stick pushed corner-wise as fast as one pushed straight. An aim reads the view whole:
`Math::computeForward(command().view)`.

### Game state in the command

`input().setPayload(bytes, size)` (`InputMap::setPayload`) holds up to `MAX_COMMAND_PAYLOAD`
bytes that every command carries until changed (`InputCommand::payload`); more is refused, not
truncated. Something that must arrive once is a message instead (`NetSession::sendMessage`,
[networking.md](networking.md#messages)).

### 32 actions carry to a tick

A command is fixed-size, so it copies and goes on the wire without allocating:
`MAX_INPUT_ACTIONS` is 32, the engine's `UI/Click` first, then the game's in name order. Actions
past that read on the frame clock but are absent from every command, with a warning.

## What the engine binds for itself

| Action | Who reads it |
|---|---|
| `UI/Click` (`InputActions::UI_CLICK`) | `UISystem`, for pressing buttons. In the map from construction and after every `reset()`; a game's `UI/Click` in `actions` replaces it. |
| `Camera/*` (`CameraActions`) | the editor's view, in its own `InputMap` - never the game's. |

Every play session in the editor starts and ends with the edges and values cleared
(`InputMap::reset`); the actions stay.

## How it works inside

`InputMap::update` latches the edges that `InputMap::beginTick` turns into the command at the
top of each fixed step. `InputHandle`, the raw device state, holds keys and buttons by the
engine's names and knows nothing of GLFW, so a windowless host or a test feeds it through the
calls the window makes (`InputHandle::onKeyEvent`, `InputHandle::setButton`).

## Key files

- `src/engine/core/input_map.h` - actions, bindings, both query sets, `InputActions`
- `modules/vkmPlatform/src/vkm_platform/input/key.h` - `Platform::Key`, `Platform::MouseButton`: the keys and buttons
- `src/engine/platform/input/input_binding.h` - `InputBinding`, `InputAction`, `bindingName`: the controls and their names
- `src/engine/io/project/actions_json.h` - the actions as `project.json` and a bindings file hold them
- `src/engine/platform/input/input_command.h` - `InputCommand`, `MAX_INPUT_ACTIONS`, `MAX_COMMAND_PAYLOAD`
- `modules/vkmPlatform/src/vkm_platform/input/input_handle.h` - raw keyboard and mouse state; vkmPlatform's
  `docs/how-it-works.md` says how the window fills it
- `src/editor/view/camera_controller_system.h` - `CameraActions`, the editor's view
