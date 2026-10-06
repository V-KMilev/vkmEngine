# Input

Gameplay asks for what it wants - `"Jump"`, `"Move/Forward"` - and never names a
key where it reads one. That one indirection is what makes a controls screen
possible and lets an action carry a key and a mouse-button binding at once.

The key is named in one place, where the action is defined, and it is named by
GLFW's code: the engine has no key constants of its own, so the file that
defines the bindings includes GLFW for `GLFW_KEY_*` - every example does. The
calls that read input need no windowing header.

There are **two ways to read input**, and picking the wrong one is the most
common input bug in any engine. The rule is one sentence:

> **`onUpdate` reads the map. `onFixedUpdate` reads the command.**

Everything below is that sentence with its reasons.

## Defining actions

Do it once, in `onStart`. An action is a name and the physical controls that
feed it:

```cpp
void Player::onStart() {
    InputMap& map = input();

    // One action, several bindings: both keys do the same thing.
    map.define("Jump", {
        {InputSource::Key, GLFW_KEY_SPACE, 1.0f},
        {InputSource::Key, GLFW_KEY_W,     1.0f},
    });

    // One action as an *axis*: the scales oppose, so holding both reads zero.
    map.define("Move/Right", {
        {InputSource::Key, GLFW_KEY_D,  1.0f},
        {InputSource::Key, GLFW_KEY_A, -1.0f},
    });
}
```

`scale` is the whole of what makes an axis: `axis("Move/Right")` sums every
held binding and clamps to -1..1, so two keys become one signed value and a
future analogue stick binds to the same action with no call site changing.

`addBinding(action, binding)` adds one without replacing the rest - that is the
call a rebinding screen makes. `bindings(action)` reads them back, `actions()`
lists every defined name in name order, and `clearBindings(action)` empties one
while leaving the action defined.

Names are the stable identity: they are what gameplay asks for and what a saved
binding file would refer to. Nothing in the engine needs to know your action
exists.

## Reading it on the frame clock

Inside `onUpdate` or `onRealtimeUpdate` - anything that runs once a frame:

| Call | Answers |
|---|---|
| `input().held("Jump")` | is it down now |
| `input().pressed("Jump")` | did it go down *this frame* |
| `input().released("Jump")` | did it come up this frame |
| `input().axis("Move/Right")` | its value, -1..1 |

Plus the three the pointer has, which no binding can express - a position is not
an action, and a wheel notch has no held state to name:

| Call | Answers |
|---|---|
| `input().pointer()` | cursor position, window pixels, origin top-left |
| `input().pointerDelta()` | how far it moved since the last frame |
| `input().wheel()` | notches turned this frame, positive away from you; zero over a blocking element of the game's UI, which takes them |

A mouse *button* is an action like any other, and binds with
`InputSource::MouseButton`.

And the two a text field reads, which are not actions either - a character is
what the keys *meant*, with the layout, Shift and dead keys applied, and an
editing key is not rebindable:

| Call | Answers |
|---|---|
| `input().text()` | the characters typed this frame, in order, as UTF-32 |
| `input().typed(GLFW_KEY_BACKSPACE)` | did the key go down *or auto-repeat* this frame |

`typed()` is what Backspace, Delete, the arrows, Home, End and Enter act on: once
per press and again at the system's repeat rate while held, which `pressed()`
deliberately is not. How a project builds a field from them is
[ui.md](ui.md#text-input).

The map is sampled once, before any system runs, so every reader in the frame
agrees about the edges. That is also why `pressed()` is correct for everybody
without each caller keeping its own "was it down last frame" flag.

In the editor, a device its panels hold reads as untouched: keys while a text
field has the keyboard, mouse buttons, the wheel and `pointerDelta()` while a
panel has the pointer. The map applies it, from `HostChrome`, so no reader asks.
A key held when a panel takes the keyboard reads as released, the way letting go
would, and `text()` and `typed()` read empty while a panel has the keyboard.

The game's own UI holds a press the same way, in every host. A press of a button
`UI/Click` is bound to that begins with the pointer over a blocking UI element
reads as down for `UI/Click` alone, until it is let go: a click on a menu never
fires the gun bound to the same button, nor reaches a tick's command. Whose a
press is gets decided once, when it begins - a press that begins in the world
stays gameplay's when it is dragged over a panel. `pointerOverUI()` says whether
a blocker is under the pointer, as of the last UI layout
([ui.md](ui.md#interaction)).

## Reading it on the tick

Inside `onFixedUpdate`, ask the same four questions of the tick's command:

```cpp
void Player::onFixedUpdate(float dt) {
    const InputCommand& tick = command();
    const InputMap&     map  = input();

    const float move = map.axis(tick, "Move/Right");
    if (map.pressed(tick, "Jump")) jump();
}
```

**A fixed update must not use the frame queries.** Input arrives on the render
clock and simulation runs on the tick clock, so a fixed update that asks the
device directly:

- **drops a tap** that began and ended between two ticks, and
- **repeats a press** on every tick of a slow frame - one keypress, three jumps.

A command avoids both because it is built per tick from what actually happened
since the previous one: the axes as they stand, plus the edges latched across
every frame in between. It is also the only form of input that can be
**replayed**, which is what client-side prediction is made of - see
[networking.md](networking.md).

`command()` answers correctly in all three roles, which is the point of it:
offline and on the owning client it is the local player's input, and on a server
it is what *that entity's* player sent, run on the tick they sent it for. On a
server or a client an entity no player drives reads as nothing held; offline,
every entity reads the local player's command.

### Camera-relative movement

A tick cannot ask where the camera points, because the camera turns on the frame
clock and by the time the tick runs the answer has moved - so the same command
replayed against a camera that has since turned walks somewhere else. Hand the
view to the input instead, once per frame, after you move the camera:

```cpp
void Player::onUpdate(float dt) {
    orbitCamera(dt);
    if (const Transform* view = scene().tryGet<Transform>(findActiveCamera(scene()))) {
        input().setView(view->rotation);
    }
}
```

and read it back off the command, where it is a fact about that tick:

```cpp
const glm::vec3 forward = Math::computeForward(command().view);
```

## What the engine binds for itself

Two sets. The first is in your map from its construction and again after every
`reset()`, and yours to override - your `define` of the same name replaces it;
the second is the editor's, in a map you never see.

| Action | Who reads it |
|---|---|
| `UI/Click` | `UISystem`, for pressing buttons. Bound rather than read raw so a game can rebind it, and so the edge detection is the map's rather than the UI's. |
| `Camera/Forward`, `Camera/Right`, `Camera/Up`, `Camera/Boost`, `Camera/Look` | the editor's fly camera only, in an `InputMap` of its own - never the game's, so a game's map holds none of them and a runtime has no such map at all. |

In the editor, each play session starts and ends on exactly the engine's own
(`InputMap::reset`, called from `editor/session/scene_io_controller.cpp`): what a session's
behaviors defined goes with it, so define your actions in `onStart`, which every
session runs. While the world stands still - the clock paused in Edit mode or
by a game's own pause, or a time scale of zero - a frame that runs no tick
drops the edges it latched (`Clock::isSimulationStill`), so the first tick
after it does not see a press made meanwhile.

The names are constants - `InputActions::UI_CLICK` in
`platform/input/input_map.h`, the camera's `CameraActions` beside the
controller in `src/editor/input/camera_controller_system.h` - so a typo in the
engine's own actions is a compile error rather than an action that silently
never fires. Yours can be bare
string literals, or your own constants if you would rather have the same
protection.

## The two limits

- **32 actions carry to a tick.** A command is a fixed-size value so it can be
  copied, kept and put on a wire without an allocation, and `MAX_INPUT_ACTIONS`
  is 32. Actions past that are still readable on the frame clock; they are
  absent from every command, and the map logs a warning naming each action left
  without one. Slots are handed out in definition order.
- **An undefined action reads as inactive**, rather than erroring. A behavior
  may ask for something the current binding set does not provide, and answering
  "not held" is more useful than a crash.

---

## How it works inside

`InputMap` holds one entry per action: its bindings, this frame's value,
whether it is active this frame and was the previous one, and its command slot.
`WindowManager` polls the devices, `Engine::run` calls `InputMap::update` once
per frame before any system, and `beginTick` builds the command at the top of
each fixed step.

**A tap shorter than a frame still counts.** One poll can deliver a key's press
and its release together, and the level alone would then read as a key nobody
touched. So a key or button going down is also recorded as *struck*, and
`update()` reads a struck binding as down for that one sample before clearing
the strikes: the tap is held for one frame, pressed on it and released on the
next, and reaches a tick like any other press.

**Edges are latched at frame rate, drained at tick rate.** `update()` compares
whether an action is active this frame against last frame and sets a bit in a
pending mask;
`beginTick()` moves that mask into the command and clears it. That is what makes
a press that happened on a frame no tick followed still reach a tick, and what
stops the second tick of a slow frame seeing a press the first already consumed.

An action is "active" when its summed, clamped value reaches 0.5 either way,
which is what lets an analogue source resolve to a button the same way a key
does.

Lookups take `std::string_view` against a `std::map` with a transparent
comparator, so `pressed("Jump")` with a literal builds no string and allocates
nothing - a fixed update asks several of these per entity per tick.

`InputHandle` (`platform/input/input_handle.h`) is the raw device state behind
all of it - which keys and buttons are down or struck, which keys were typed
(struck or repeated) and the characters they made, where the cursor is, how far
the wheel turned. Nothing in gameplay should reach it: it is per-device and
unsampled, and its strikes belong to the one reader that consumes them.

It knows nothing about where that state came from, which is what keeps the
windowing library out of the input path. `WindowManager` fills one from GLFW on
a host that has a window - it owns the key, character, mouse-button, scroll and cursor
callbacks - and a host that has none writes the same
fields directly. That is how the engine's own tests drive a keypress from device to
action to tick without a window, and it is the seam a recorded input replay
would use.

**Key files**

- `src/engine/platform/input/input_map.{h,cpp}` - actions, bindings, both query sets, and
  `InputActions`, the engine's own
- `src/engine/platform/input/input_command.h` - `InputCommand`, `MAX_INPUT_ACTIONS`
- `src/engine/platform/input/input_handle.h` - raw keyboard/mouse state
