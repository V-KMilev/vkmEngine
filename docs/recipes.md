# Recipes

The common jobs of a game, each in the few lines a behavior writes for it. An engine call
here is a member of the behavior (`BehaviorBase`, [scripting.md](reference/scripting.md#behavior))
or a `UI::` builder ([ui.md](reference/ui.md#the-engines-controls)) unless it names its scope;
`aimAt`, `damage` and the like are the game's own. Offline,
`isSimulated()`, `isMine()` and `isAuthority()` always answer true, so a game
leaves them out until it is played over a wire.

## Moving and acting

**Move a character.** List the axes in `project.json`, then steer in the tick, where the
camera looks:

```json
"actions": { "Move/Forward": ["W", {"bind": "S", "scale": -1}], "Move/Right": ["D", {"bind": "A", "scale": -1}] }
```

```cpp
void Player::onFixedUpdate(float) {
    if (CharacterController* body = tryGet<CharacterController>())
        body->moveInput = alongView(axis("Move/Right"), axis("Move/Forward")) * speed;
}
```

**Jump on a press.** `if (pressed("Jump")) body->jumpRequested = true;` - in a tick
it reads the tick's command, so a tap between ticks lands once
([input.md](reference/input.md#reading-it-on-the-tick)).

**Turn something every frame.** The template's `Spinner`: change the
`Transform` in `onUpdate` by `dt`.

**Aim at the cursor.**

```cpp
if (auto ray = pointerRay())
    if (auto hit = cast(ray->origin, ray->direction, 100.0f)) aimAt(hit->point);
```

**Line of sight, or a shot.** `if (auto hit = cast(eye, forward, range)) damage(hit->entity);`
`cast` passes this entity over, and a `QueryFilter` narrows what it sees.

**Spawn a prefab at a point.** A `PrefabRef` field names it:
`instantiate(impact, Transform{hit->point});`. `instantiateShared` builds it at
every end, from the authority.

**Destroy later.** `destroyAfter(lifetime);` in `onStart`.

**Do something later.** `after(2.0f, [this] { openGate(); });` - dropped if the
behavior goes first.

**Cooldown.** A float and four lines:

```cpp
m_wait = std::max(0.0f, m_wait - dt);
if (m_wait > 0.0f || !pressed("Fire")) return;
fire();
m_wait = cooldown;
```

## Touching and finding

**React to a hit.**

```cpp
void Bullet::onCollisionEnter(const Collision& hit) {
    if (Health* health = findBehavior<Health>(hit.other)) health->damage(10.0f);
}
```

**Pick-up zone.** A trigger collider, and in the coin's behavior:

```cpp
void onTriggerEnter(EntityId other) override {
    if (Player* player = findBehavior<Player>(other)) { player->coins++; playSound(ding); destroy(); }
}
```

**An explosion pushes what is near.**

```cpp
std::vector<EntityId> nearby;
overlapSphere(at, radius, nearby);
for (EntityId e : nearby)
    if (Rigidbody* b = scene().tryGet<Rigidbody>(e)) b->linearVelocity += outFrom(at, e) * push / b->mass;
```

`overlapSphere` sees shapes, not centers, and a `QueryFilter` limits it to layers.

**Reach the game's director.** `m_game = findInScene<Director>();` in `onStart`,
kept while its entity lives.

**Find a named entity.** `m_door = findNamed("Door");` - the whole scene's lowest
slot. A part of this entity is `findPart("Lid")`.

## Sound and animation

**A one-shot sound.** `playSound(thud);` from this entity, or
`playSoundAt(thud, hit.point);`. `playSound(step, 1.0f, 0.1f)` varies each play's
pitch and volume by a tenth, so a sound heard often never repeats exactly.

**Play an animation clip.** `playAnimation(runClip);` cross-fades the nearest
`Animator` on this entity or below it, and does nothing in a replayed tick.

## Screens

**Score on screen.** A style's shadow reads over any scene, and a column stacks the lines:

```cpp
UI::Style hud;
hud.shadow = {0, 0, 0, 0.7f};

m_hud = UI::canvas(scene(), "HUD");
const EntityId lines = UI::column(scene(), m_hud, UIElement::at({0, 0}, {24, 24}, {400, 100}), 6);
m_score = UI::text(scene(), lines, UIElement::at({0, 0}, {0, 0}, {400, 40}), "Score 0", hud);
m_lives = UI::text(scene(), lines, UIElement::at({0, 0}, {0, 0}, {400, 40}), "Lives 3", hud);
// and when it changes:
scene().get<UIText>(m_score).text = "Score " + std::to_string(score);
```

**A button.**

```cpp
EntityId play = UI::button(scene(), m_menu, UIElement::at({0.5f, 0.5f}, {0, 0}, {240, 52}), "PLAY");
onClick(play, [this] { startGame(); });
```

**A slider or a toggle.**

```cpp
EntityId volume = UI::slider(scene(), m_page, UIElement::at({0.5f, 0}, {0, 80}, {240, 12}), 0.0f, 1.0f, 0.8f);
onChange(volume, [this](float value) { setVolume(value); });
```

A `UI::toggle` reports 1 or 0 through `onChange` the same way, and a `UI::choice`
its index. A `UI::Style` gives every control the game's font, text size, colors and
rounding.

**Pause menu.** A canvas that pauses the game holds the world and frees the cursor while it
shows, and its action opens and closes it, so the menu needs no hook:

```cpp
m_menu = UI::canvas(scene(), "Pause", 10);
UICanvas& menu = scene().get<UICanvas>(m_menu);

menu.visible      = false;
menu.pausesGame   = true;
menu.toggleAction = "Menu";
onClick(resume, [this] { scene().get<UICanvas>(m_menu).visible = false; });
```

An end screen is the same canvas without the action, shown when the round ends; its "again"
button's `restartScene()` takes the canvas with the old world, so the new one runs.

**A graphics settings page.** A quality row picks a tier, and a switch changes one
setting under it; an effect the project has off stays off, so the page shows what
the frame draws after each change:

```cpp
onChange(m_quality, [this](float tier) {
    graphics().setQuality(static_cast<GraphicsPreset>(std::lround(tier)));
    showRows();  // the rows read graphics().settings(), the tier row graphics().quality()
});
onChange(m_bloom, [this](float on) {
    RenderSettings settings = graphics().settings();
    settings.bloom          = on > 0.5f;
    graphics().set(settings);
    showRows();
});
```

**A label over a 3D thing.**

```cpp
const UICanvas& canvas = scene().get<UICanvas>(m_canvas);
if (auto at = screen().toCanvas(worldPosition() + Math::WORLD_UP * 2.0f, canvas))
    scene().get<UIElement>(m_label).position = *at;
```

**Next level, or restart.** `loadScene(nextLevel);` with a `std::string` field, or
`restartScene();`. Both wait for the end of the hook pass.

## State and the wire

**Send and hear a game event.** `events().emit(DoorOpened{entity()});` and, in
another behavior's `onStart`, `subscribe([this](const DoorOpened& e) { ... });`.

**Save progress.**

```cpp
const auto file = Roots::playerFile("progress.json");
nlohmann::json save = readOwnFile(file);
save["best"] = std::max(save.value("best", 0), score);
writeOwnFile(file, save);
```

**Show or rebind a key.** `input().keyName("Jump") + " jumps"` for a hint;
`if (auto key = input().struck()) input().rebind("Jump", {*key});` on a controls
screen. The runtime keeps the player's choice in their own file.

**Replicated health.** A component with a reflect block travels field by field
with no codec of its own:

```cpp
NetSchema::get().replicate<Health>("Health");     // in vkmSetupNetwork
if (isAuthority()) get<Health>().hp -= damage;    // where it is decided
```

**An effect the server counts.** The server raises a replicated count, as `Health`
above, and every end plays the effect when its `CountWatch` (`net/replication/count_watch.h`)
sees the count rise - never for the count a joining client first sees:

```cpp
CountWatch m_heard;                                       // a member of the behavior
if (isAuthority()) ++getOrAdd<Tally>().count;             // where it happens
if (const Tally* tally = tryGet<Tally>(); tally && m_heard.rose(tally->count))
    playSound(click);                                     // in onUpdate, at every end
```
