# Input

Gameplay reads named actions - `"Jump"`, `"Move/Forward"` - never keys. The key
is named once, where the action is defined, by GLFW's code (`GLFW_KEY_*`; the
engine has no key constants of its own), so a controls screen can rebind it and
one action can take several keys and mouse buttons.

There are two ways to read input:

> **`onUpdate` reads the map. `onFixedUpdate` reads the command.**

## Defining actions

Once, in `onStart`. An action is a name and the controls that feed it:

```cpp
void Player::onStart() {
    InputMap& map = input();

    map.define("Jump", {
        {InputSource::Key, GLFW_KEY_SPACE, 1.0f},
        {InputSource::Key, GLFW_KEY_W,     1.0f},
    });

    // An axis: opposing scales, so holding both reads zero.
    map.define("Move/Right", {
        {InputSource::Key, GLFW_KEY_D,  1.0f},
        {InputSource::Key, GLFW_KEY_A, -1.0f},
    });
}
```

`axis()` sums every held binding's `scale`, clamped to -1..1; the action is
held, pressed or released when that reaches 0.5 either way. A mouse button binds
with `InputSource::MouseButton`; keyboard and mouse are the only sources. **An
undefined action reads as inactive**, not as an error.

### Rebinding

`define` states the defaults. A controls screen changes what feeds an action
with `rebind` - the whole list, empty to unbind it - and reads with
`bindings(action)` and `actions()`. While waiting for the player's choice it
asks `struck()` - the key or button that went down this frame, bound or not, as
an `InputBinding` - and shows one with `bindingName(binding)` ("Left Shift",
"Right mouse").

`saveBindings()` returns the player's rebindings as JSON - only those differing
from the defaults, so an untouched key follows a later default - for a settings
file under `ProjectPaths::playerRoot(game)`. `loadBindings(json)` restores them,
before or after the actions are defined; they survive `reset()`.
`resetBindings()` returns every action to its defaults.

## Reading it on the frame clock

In `onUpdate` or `onRealtimeUpdate`:

| Call | Answers |
|---|---|
| `input().held("Jump")` | is it down now |
| `input().pressed("Jump")` | did it go down this frame |
| `input().released("Jump")` | did it come up this frame |
| `input().axis("Move/Right")` | its value, -1..1 |
| `input().pointer()` | cursor position, window pixels, origin top-left |
| `input().pointerDelta()` | movement since the last frame |
| `input().wheel()` | notches this frame, positive away from you; zero over a blocking UI element |
| `input().text()` | characters typed this frame, UTF-32, layout and Shift applied |
| `input().typed(GLFW_KEY_BACKSPACE)` | did the key go down or auto-repeat this frame |

Text fields built from the last two: [ui.md](ui.md#text-input).

The map is sampled once a frame before any system runs, so every reader agrees;
a tap shorter than a frame reads as pressed one frame, released the next.

**A device someone else holds reads as untouched.** In the editor: keys (and
`text()`, `typed()`) while a panel's text field has the keyboard; mouse buttons,
wheel and `pointerDelta()` while a panel has the pointer. In every host, a
`UI/Click` press that begins over a blocking UI element belongs to the UI, and
`pointerOverUI()` says whether a blocker is under the pointer
([ui.md](ui.md#interaction)).

**Mouse-look locks the cursor**: `window()->setCursorMode(CursorMode::Disabled)`
(`platform/window/window_manager.h`). `pointerDelta()` keeps reporting movement,
the game's UI stops hit-testing, and in the editor Stop hands the cursor back.

## Reading it on the tick

In `onFixedUpdate`, ask the same questions of the tick's command:

```cpp
void Player::onFixedUpdate(float dt) {
    const InputCommand& tick = command();
    const InputMap&     map  = input();

    const float move = map.axis(tick, "Move/Right");
    if (map.pressed(tick, "Jump")) jump();
}
```

**A fixed update must not use the frame queries.** Input arrives per frame and
simulation runs per tick, so the frame queries **drop a tap** that fell between
two ticks and **repeat a press** on every tick of a slow frame. A command holds
the axes as they stand plus every edge since the previous tick, and it is what
gets replayed for client-side prediction ([networking.md](networking.md)).

Whose command `command()` is on each end:
[networking.md](networking.md#the-four-questions).

While the world stands still (`Clock::isSimulationStill`), a frame that runs no
tick drops its latched edges, so the next tick does not see a press made
meanwhile.

### Camera-relative movement

The camera turns on the frame clock, so a tick cannot ask it. Hand the view to
the input once per frame, after moving the camera:

```cpp
void Player::onUpdate(float dt) {
    orbitCamera(dt);
    if (const Transform* view = scene().tryGet<Transform>(findActiveCamera(scene()))) {
        input().setView(view->rotation);
    }
}
```

and read it off the command: `Math::computeForward(command().view)`.

### Game state in the command

`setPayload(bytes, size)` holds up to `MAX_COMMAND_PAYLOAD` bytes that every
command carries until changed (`InputCommand::payload`); more is refused, not
truncated. Something that must arrive once is a message instead
(`NetSession::sendMessage`, [networking.md](networking.md#messages)).

### 32 actions carry to a tick

A command is fixed-size so it copies and goes on the wire without allocating;
`MAX_INPUT_ACTIONS` is 32, assigned in definition order. Actions past that read
on the frame clock but are absent from every command, with a warning.

## What the engine binds for itself

| Action | Who reads it |
|---|---|
| `UI/Click` | `UISystem`, for pressing buttons. In the map from construction and after every `reset()`; your `define` of it replaces it. |
| `Camera/Forward`, `Camera/Right`, `Camera/Up`, `Camera/Boost`, `Camera/Look` | the editor's fly camera, in its own `InputMap` - never the game's. |

The constants: `InputActions::UI_CLICK` in `platform/input/input_map.h`, the
camera's `CameraActions` in `src/editor/input/camera_controller_system.h`.

In the editor every play session starts and ends on just the engine's actions
(`InputMap::reset`), so define yours in `onStart`.

## How it works inside

`WindowManager` polls the devices into an `InputHandle`; `Engine::run` calls
`InputMap::update` once per frame before any system, which latches edges into a
pending mask; `InputMap::beginTick` moves that mask into the command at the top
of each fixed step and clears it. `InputHandle` is raw device state that
gameplay never reads; it knows nothing of GLFW, so a windowless host or a test
feeds it through the calls the window makes (`onKeyEvent`, `setButton`, `moveTo`).

## Key files

- `src/engine/platform/input/input_map.{h,cpp}` - actions, bindings, both query sets, `InputActions`
- `src/engine/platform/input/input_command.h` - `InputCommand`, `MAX_INPUT_ACTIONS`, `MAX_COMMAND_PAYLOAD`
- `src/engine/platform/input/input_handle.h` - raw keyboard/mouse state
