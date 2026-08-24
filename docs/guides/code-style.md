# Code Style Guide

The mechanical rules: how vkmEngine code is laid out, named, formatted, and
documented. Every rule here is observed in the current codebase. If an example
here disagrees with the code, treat the code as the source of truth and flag
the drift.

The goal is **predictability**. Someone opening a random file should be able to
guess the shape of the next file without looking. The sibling guides cover the
parts above the mechanics: [engine.md](engine.md) for what the engine is and has
decided, [design.md](design.md) for where a change belongs,
[implementation.md](implementation.md) for what makes it good, and
[review.md](review.md) for judging what is already there.

---

## Absolutes

Mechanics do not admit judgment. Every one of these is a rule, not a default.

- `#pragma once`, never include guards.
- **ASCII only** in source and comments.
- **4 spaces**, never tabs.
- In a `.cpp`, the **own header is the first include**, alone. The only thing
  allowed before it is `#define VKM_LOG_CATEGORY`.
- **No `static` free functions in a `.cpp`.** Use an anonymous namespace.
- **Every virtual override carries `override`**, destructors included.
- **Struct members are bare; class members take `m_`.**
- **Data members come last, in a `private:` section of their own** - never mixed
  with nested types, methods or constants.
- **A template another file could instantiate lives entirely in a header.** No
  `.tpp`. File-local helper templates - [section 8](#8-templates).
- **A namespace you put definitions in closes with `} // namespace Name`.** A
  block that only forward-declares closes with a bare `}` -
  [section 2](#2-header-file-structure-h).
- **Backend code is `Vkm::Engine`,** not `Vkm::GL`. The two GL primitives that
  borrow vkmGL's namespace state their reason in their own headers -
  [section 4](#4-naming).
- **No decorative separator comments** of any kind.
- **No task, version or commit reference in a comment.** That is the commit's job.
- **A comment states a constraint to a reader.** It never argues the decision to
  a reviewer and never carries section headings, at any length. How long it may
  be is a separate question - [6.1](#61-what-bounds-a-comment).

---

## 1. Include roots and include order

Code lives under five include roots. Each has its own style:

| Root                  | Include style                    | Example                                  |
|-----------------------|----------------------------------|------------------------------------------|
| `src/engine/`         | module-qualified                 | `#include "system/render/render_view.h"` |
| `src/backend/opengl/` | flat (every file is `gl_`-prefixed) | `#include "gl_backend.h"`             |
| `src/tools/`          | module-qualified                 | `#include "loader/texture_loaders.h"`    |
| `src/editor/`         | module-qualified, from **two** roots | `#include "panels/inspector_panel.h"` and `#include "ecs/scene.h"` |
| `app/`                | repo-root-qualified              | `#include "app/engine_app.h"`            |

The editor has a root of its own (`src/editor/CMakeLists.txt:6`) *and* the
engine's, so an editor `.cpp` includes `panels/`, `framework/` and `ui/` from the
first and `core/`, `ecs/`, `system/` from the second - see
`panels/inspector_panel.cpp:1-24`. The three hosts add the repo root instead
(`app/editor/CMakeLists.txt:21` and its siblings), which is why `app/` is spelled
into the path.

Always include the **module path**, never the bare filename:

```cpp
// good
#include "ecs/scene.h"
#include "resource/asset/mesh_asset.h"
#include "system/render/render_view.h"

// bad
#include "scene.h"
#include "mesh_asset.h"
#include "render_view.h"
```

**Engine code never reaches into `backend/` directly.** The backend is reached
only through the abstract interfaces in `system/render/` - `RenderBackend` and
`EditorRenderHooks`, and no third (see [engine.md](engine.md#absolutes) for why
this seam matters). `app/engine_app.h:27` includes `gl_backend.h` and constructs
`GLBackend` at `:111`; that is not an exception to the rule but the point of it -
a host is the composition root, the one place allowed to pick which backend the
engine gets.

Within a file, includes are grouped, each group separated by one blank line, in
this order:

1. Standard library (`<vector>`, `<cstdint>`, ...)
2. Third-party (`<glm/glm.hpp>`, `<imgui.h>`, `<nlohmann/json_fwd.hpp>`, ...)
3. Local project includes

Local includes never come before stdlib. A local header that transitively
drags in a stdlib header can mask a missing include if ordering is wrong.

---

## 2. Header file structure (.h)

Every header follows this skeleton, in this exact order:

```
1.  #pragma once          (never #ifndef guards)
2.  (blank line)
3.  Standard library includes
4.  (blank line)
5.  Third-party includes        (if any)
6.  (blank line)
7.  Local project includes
8.  (blank line)
9.  namespace Vkm::Engine {
10. Forward declarations         (if needed, one indent in)
11. Class / struct / free-function definitions
12. } // namespace Vkm::Engine
13. (blank line)
14. VKM_REFLECT_BEGIN(...) block  (if the type is reflected - see 2.1)
```

Forward-declare a type when you only refer to it by **pointer or reference** in
a signature. Include the full header only when the type appears by value as a
member, as a base class, or where a template needs the full definition. Forward
declarations go immediately inside the namespace:

```cpp
namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct Visibility;

struct RenderView { /* ... */ };

} // namespace Vkm::Engine
```

A type from *another* namespace needs a block of its own, and that block closes
with a bare `}` - 46 of the 47 forward-declaration-only blocks in the tree do
(`backend/opengl/gl_pass.h:3-9`). The `} // namespace Name` close is for a
namespace you put definitions in.

### 2.1 Reflected types close the namespace first

`VKM_REFLECT_BEGIN` opens `namespace Vkm::Engine::Reflect` itself, so it goes at
**global scope, after the `} // namespace` close** - never inside the namespace
the type lives in. From `ecs/component/core/transform.h:109-113`:

```cpp
} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Transform)
    VKM_F(position),
    VKM_F(rotation),
    VKM_F(scale)
VKM_REFLECT_END()
```

The type is named fully qualified from the global scope the macro is written at.
Which types get one, and what a component owes the scene format besides this
block, is in [design.md](design.md#24-a-component-that-serializes).

---

## 3. Implementation file structure (.cpp)

Every `.cpp` follows this skeleton:

```
1.  Own header include      (the matching .h, first, alone)
2.  (blank line)
3.  Standard library includes
4.  (blank line)
5.  Third-party includes     (if any)
6.  (blank line)
7.  Local project includes
8.  (blank line)
9.  namespace Vkm::Engine {
10. (optional) anonymous namespace { ... }   for file-local helpers
11. Definitions
12. } // namespace Vkm::Engine
```

The **own header is always the first include**, alone on its line. This forces
your header to compile as if it were first in any translation unit, catching
missing includes inside it.

The single exception is `#define VKM_LOG_CATEGORY "..."`, which must precede the
own-header include. The own header transitively pulls in `logger.h`, and
`logger.h` defaults `VKM_LOG_CATEGORY` to `nullptr` if nothing set it first;
defining it afterward triggers `-Wmacro-redefined`. Canonical opening, from
`core/engine.cpp:1-14`:

```cpp
#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

#include <atomic>
#include <csignal>

#include "logger.h"

#include "core/engine_config.h"
#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"

namespace Vkm::Engine {
```

### 3.1 File-local helpers go in an anonymous namespace

Never use `static` free functions in a `.cpp`. Use an anonymous namespace,
placed between `namespace Vkm::Engine {` and the first externally visible
definition:

```cpp
namespace Vkm::Engine {

namespace {

constexpr const char* STAGE_NAMES[] = {
    "Input", "Simulation", "Transform", "Visibility", "Render", "UI"
};

} // namespace

void Engine::run() { /* ... */ }

} // namespace Vkm::Engine
```

Helpers inside an anonymous namespace get no extra prefix (`detail_`,
`_internal`) - the namespace already restricts their scope.

---

## 4. Naming

| What             | Convention                   | Example                              |
|------------------|------------------------------|--------------------------------------|
| Class / struct   | PascalCase                   | `RenderView`, `DrawableData`         |
| Method           | camelCase                    | `addSystem()`, `getScene()`          |
| Class member     | `m_` + camelCase             | `m_scene`, `m_systemsByStage`        |
| Struct member    | bare camelCase               | `position`, `viewportWidth`          |
| Local variable   | camelCase                    | `deltaTime`, `worldMin`              |
| File-scope state | `g_` + camelCase             | `g_interrupted`, `g_sink`            |
| `thread_local`   | `t_` + camelCase             | `t_isWorker`                         |
| `static` local   | `s_` + camelCase             | `s_iniPath`, `s_iconFont`            |
| Constant         | UPPER_SNAKE_CASE             | `ALIVE_BIT`, `DEFAULT_THREAD_COUNT`  |
| Enum class value | PascalCase                   | `SystemStage::Render`                |
| Type alias       | PascalCase                   | `EntityId`, `MeshHandle`             |
| Template param   | single letter or PascalCase  | `T`, `ResourceType`                  |
| Namespace        | PascalCase, under `Vkm::`    | `Vkm::Engine`, `Vkm::GL`, `Vkm::Log` |
| File name        | snake_case                   | `render_view.h`, `gl_forward_pass.cpp` |

The three lifetime prefixes mark state that outlives a call, which is what makes
each of them a thread question: `g_interrupted` (`core/engine.cpp:28`) is written
from a signal handler, `t_isWorker` (`platform/threading/thread_pool.cpp:14`) is
how a worker tells itself apart. A file-scope `g_` also goes in the anonymous
namespace ([3.1](#31-file-local-helpers-go-in-an-anonymous-namespace)).

Namespaces nest under one umbrella - `Vkm::Engine` for engine code, `Vkm::GL`
for the vkmGL wrappers, `Vkm::Log` for vkmLog - with helper namespaces nested
further in (`Vkm::Engine::Math`, `Vkm::Engine::HierarchyOperations`). When a
file's whole content lives in one, open it in the C++17 one-line form -
`namespace Vkm::Engine::Math {`, closed by a single `}` - rather than opening
each level separately.

**The OpenGL backend is `Vkm::Engine`, not `Vkm::GL`.** `Vkm::GL` is vkmGL, the
platform layer. 50 of the 53 headers under `src/backend/opengl/` open
`Vkm::Engine`; 37 of the 39 `Vkm::GL` blocks in there are forward declarations of
vkmGL types, and `gl_pass.h:3-9` shows both in the same file, four lines apart. A
new backend file that opens `namespace Vkm::GL` compiles and puts its type in the
wrong library's namespace.

The two that really do define into `Vkm::GL` are `data/gl_screen_triangle.h` and
`data/gl_instance_buffer.h`, and each says why in its own `@brief`: both are GL
primitives by kind - an attribute-less fullscreen draw, a per-instance matrix
buffer - that vkmGL cannot own, one because it is a rendering idiom rather than a
GL object and the other because glm has no business in a GL wrapper's API. That
is the whole exception. A backend *type the engine renders through* is
`Vkm::Engine`.

### 4.1 The struct/class member rule

This is the single most common slip. The rule:

| Kind                | Members      | Rule of 5 | Examples                          |
|---------------------|--------------|-----------|-----------------------------------|
| Data-only struct    | bare `name`  | none      | `Transform`, `DrawableData`, `FrameContext` |
| Class with behavior | `m_name`     | explicit  | `Engine`, `RenderSystem`, `SparseSet<T>` |

If you want to put `m_` on a struct member, the struct is probably a class. If
you're skipping the Rule of 5 on a `class`, it's probably a struct. (For the one
deliberate hybrid, `Resource`, see [13.1](#131-resource-hybrid-struct).)

### 4.2 Method names

- Getters: `getX()`. Boolean getters: `isX()` / `hasX()`.
- Setters: `setX(value)`.
- Mutators are verb-first: `addSystem`, `removeFromParent`, `clear`, `commit`.
- Predicates are verb-first: `isAlive`, `isVisible`, `hasFixedUpdate`.

---

## 5. Formatting

| Rule              | Setting                                                       |
|-------------------|---------------------------------------------------------------|
| Indent            | 4 spaces, never tabs                                          |
| Braces            | K&R - opening brace on the same line                          |
| Access specifier  | indented 4 spaces from `class`                                |
| Member body       | indented 8 spaces from `class` (4 inside the access specifier) |
| Keyword spacing   | `if (`, `for (`, `while (`, `switch (` - space after keyword  |
| Call spacing      | `fn()`, `obj.method()` - no space before `(`                  |
| Pointer / ref     | `T& name`, `T* name` - the `&`/`*` binds to the type          |
| Rvalue ref        | `T && name` - one space on each side of `&&`                  |
| Callable param    | `Fn&& fn` - unspaced, the one exception ([5.2](#52-where-the-rvalue-spacing-rule-bends)) |
| Line length       | soft target ~110 columns; break parameter lists when wider    |
| Charset           | strictly ASCII in source and comments - no Unicode            |
| Namespace close   | `} // namespace Name`; a forward-declaration block, bare `}`  |

The access-specifier / member indentation is distinctive - note the double
indent on members:

```cpp
class Clock {
    public:
        void beginFrame();

        float getDeltaTime() const { return m_deltaTime; }
        bool  isPaused() const     { return m_paused; }

    private:
        float m_deltaTime = 0.0f;
        bool  m_paused    = false;
};
```

(`core/clock.h:22-110`, trimmed to the shape.)

### 5.1 Vertical alignment

Align related initializers, defaults, and trailing comments when the column
form reads more clearly. Do not align across a blank line - alignment signals
"these belong together," and a blank line has broken that:

```cpp
float m_deltaTime   = 0.0f;
float m_simDelta    = 0.0f;
float m_accumulator = 0.0f;

bool  m_paused       = false;
int   m_pendingSteps = 0;
float m_timeScale    = 1.0f;
```

(`core/clock.h:103-109`. The blank line is the point: the three time values align
with each other and the three play-state values with each other, and neither
group aligns across the gap.)

### 5.2 Where the rvalue spacing rule bends

In `src/engine` headers `&&` is spaced at all 113 sites but one shape: a
**callable parameter** is
`Fn&& fn`, unspaced, 10 times out of 10 (`core/reflect.h:50`, `ecs/scene.h:186`,
`core/memory/sparse_set.h:131`, `resource/resource_manager.h:301`, six more). The
split is not rvalue-vs-forwarding - `Scene::add(EntityId, T && component)`
(`ecs/scene.h:114`) is a forwarding reference and is spaced. `Fn&&` reads as one
token: the thing you hand a lambda to.

### 5.3 Multi-line parameter lists

Break when the list does not fit, and keep one style within a single signature.
Both forms are live and neither is the house style - full-breaks are outnumbered
roughly four to one, and `render_view.h` uses each in the same class: `build` at
`:105-110` full, `buildDrawables` at `:174-175` and `buildShadowCasters` at
`:188-189` aligned to the open paren. Full-break when the parameters are long or
each wants a `@param` beside it; align when the rest fit on the continuation.

```cpp
void build(
    const Scene& scene,
    const Visibility& visibility,
    const UIDrawData* ui,
    const PoseBuffer* poses
);

void buildDrawables(const Scene& scene, const Visibility& visibility,
                    const PoseBuffer* poses);
```

What is not allowed is mixing the two inside one signature.

### 5.4 No decorative separators

Do not divide code with banner comments:

```cpp
// ----------------------------- BAD -----------------------------
// === Section: rendering ===
// ***************************************************************
```

Organize with `public:` / `private:`, blank lines, and `@brief` docs. (Runtime
log strings are output, not code structure, and are exempt - the build dump at
`debug/build_info.h:20-27` is the whole population; see
[13.4](#134-decorative-log-strings).)

**Zero** tree-wide: `src/`, `app/`, `examples/` and `templates/` carry no banner
of any width. There is nothing to copy from, so a banner arriving in a diff is
new - and a long panel `.cpp`, where the sections feel like they want labels, is
where it shows up.

---

## 6. Documentation and comments

The default is **no comment**. Good names and clear structure do most of the
explaining. Add a comment only when a reader would otherwise have to ask:

- Why does this code exist? (a hidden constraint, a bug it works around)
- What invariant does it hold that the type system cannot encode?
- Why is the obvious alternative wrong? (a measured perf reason, a platform quirk)

Do **not** comment to restate the code (`// Increment the counter`), to
reference a task or commit (`// Added in PR #142`), or to sign off
(`// vkm 2026-03-12`).

Four comment styles, picked by audience and scope:

| Style                | Use for                                                          |
|----------------------|-----------------------------------------------------------------|
| `/** @brief ... */`  | Public API: class/struct definitions, public methods, exported free functions |
| `///`                | Single-line clarification on a member or function                |
| `///< trailing`      | Inline annotation on a struct/class member                       |
| `//`                 | Implementation notes inside a function body - the *why*          |

### 6.1 What bounds a comment

Two different things are bounded two different ways, and running them together is
how this rule gets broken in both directions at once.

**Inside a function, a comment is bounded by lines.** It sits in the middle of
the work, so every line of it is a line of code the reader is not reading:

| Comment                     | Bound                                |
|-----------------------------|--------------------------------------|
| `//` inside a function body | 1-3 lines. Four is already a smell.  |
| `///` on a plain member     | 1 line.                              |

Past three lines the comment is a diagnosis before it is a comment - one of three
things has gone wrong and
[implementation.md](implementation.md#6-comment-only-the-non-obvious-why) names
them. A body comment that has grown ALL-CAPS headings has gone furthest:
`editor/framework/scene_io_controller.cpp:219-234` is a sixteen-line run opening
`// WHAT AN OPEN DOES TO THE SESSION'S IMPORTS, stated because the answer is`.
That is a design brief filed inside a function body.

**On a declaration, a comment is bounded by relevance to a caller.** A
declaration block is not in the reader's way. It is what a caller reads *instead*
of reading the implementation, which is the whole reason the block exists. So:
one sentence of `@brief`, then as much detail as a caller cannot work out for
themselves - what it is, why it is shaped this way, what the obvious alternative
would have cost, what invariant it holds, what a caller must not do.

**A seam or a format earns twenty lines, and no number governs it.**
`RenderBackend`, `EditorRenderHooks`, `AudioDevice` and
`RenderView::skinMatrices` (`render_view.h:52-65`) each carry a long block, and
each is right to: none of what they say is inferable from the signature, and
deleting it would lose the only record of it. A declaration block is too long
when a line of it is something the caller already knew, never when it passes a
count.

What a declaration block may **not** do, at any length:

- **Argue the decision to an imagined reviewer.** A commit body defends a choice
  against an objection; a comment states the constraint the choice protects. Cut
  the arguing sentence, keep the constraint. In
  `system/audio/audio_device.h:53-54`, *"What lives on each side of that line is
  worth stating, because it is what the design is"* is the sentence to cut - and
  the paragraph it introduces is not, because which side of the seam owns a
  voice's lifetime is exactly what a caller cannot infer.
- **Carry section headings.** ALL-CAPS headers, banner separators, numbered
  parts, a second topic after a blank ` *` line. The same block says the same
  thing in paragraphs. If a part of it genuinely needs headings, that part has
  become a document: move it to `docs/reference/` and leave a one-line pointer.
- **Narrate history.** What it used to be, what was tried, which release changed
  it. The log keeps that, accurately and forever.
- **Restate what the signature already says.** `@param scene The scene` is
  nothing. `@param scene Scene whose Light components are gathered`
  (`render_view.h:129`) is the half a caller could not have guessed.

Those four are the failure, not the length. A block that avoids all four is the
right length whatever it measures.

The test that settles most cases: **strike the comment and read the code.** If a
competent reader who knows this engine would now ask a question, keep the answer
to that question and nothing else. If they would not ask anything, the comment
was never load-bearing.

This section is the one home for how long a comment may be.
[implementation.md](implementation.md#6-comment-only-the-non-obvious-why) covers
what to do when yours is too long, and
[review.md](review.md#17-a-comment-that-argues) covers what an arguing comment
tells you about the code under it. Neither restates the bounds.

Rules:

- The `@brief` is a **single complete sentence**, ending at the first blank line
  in the block. Do not insert a blank ` *` line mid-sentence - it ends the brief
  and confuses tooling. Detail goes after the first blank line.
- No multi-paragraph `///` blocks. Past ~3 lines or when you need `@param` /
  `@return`, switch to `/** @brief */`.
- **Keep the full block on the documented surface.** Public APIs and non-trivial
  class / template methods carry a full `/** ... */` block: a one-sentence
  `@brief`, a detail paragraph for the non-obvious *why*, and `@param` /
  `@tparam` / `@return` for the parameters and result. Do **not** collapse an
  existing documented block down to a bare one-line `@brief`. A `@param` that
  names what an argument is is expected Doxygen, not a what-comment - anti-pattern
  #7 is about inline `//` that restate a *statement*, not about parameter docs.
  Always use the **multi-line** form (`/**` on its own line, then ` * @brief
  ...`) on a documented declaration - never the single-line `/** @brief ... */`,
  and never a trailing `///<` on a function/method declaration (`///<` is for
  plain data members only).

```cpp
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};  ///< Local position.
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};

    /**
     * @brief Compute the model matrix from transform data.
     *
     * Uses fused TRS construction: builds translation, rotation, and scale
     * directly without intermediate matrix multiplications.
     */
    static glm::mat4 computeModelMatrix(const Transform& transform);
};
```

---

## 7. Class anatomy

### 7.1 Rule of 5 - write it out

Resource-owning classes spell out all five special members explicitly, in this
order: default ctor, dtor, copy ctor, copy assign, move ctor, move assign. This
documents intent and prevents accidental copies:

```cpp
class Engine {
    public:
        Engine();
        ~Engine();

        Engine(const Engine& other) = delete;
        Engine& operator=(const Engine& other) = delete;

        Engine(Engine && other) = delete;
        Engine& operator=(Engine && other) = delete;
    // ...
};
```

- The parameter is always named `other`, even when `= delete`.
- `&&` is spaced on both sides: `Engine && other`.
- `= default` for trivial implementations, `= delete` to forbid.
- A blank line separates the copy pair from the move pair.

### 7.2 The public-block pattern

Classes commonly use two or three `public:` blocks, in order:

1. Constructors, destructor, Rule of 5.
2. The interface - methods that do work.
3. (optional) short inline accessors / getters.

Then `private:` holds the `m_`-prefixed members.

### 7.2.1 Data members get their own trailing section

Data members are **always last**, in a `private:` section of their own. Never mix
them with nested types, methods, or constants - even when that means two
`private:` blocks:

```cpp
class RenderSystem : public System {
    public:
        void update(FrameContext& ctx) override;
        void setBackend(std::unique_ptr<RenderBackend> backend);

    private:
        void installPending(FrameContext& ctx);

    private:
        std::unique_ptr<RenderBackend> m_backend;
        std::unique_ptr<RenderBackend> m_pending;

        RenderView     m_view;
        RenderSettings m_settings;
};
```

(`system/render/render_system.h:20-95`, trimmed to the shape.)

The second `private:` is not redundant. The state of an object is the thing a
reader most often wants to find, and it should be in one place at the bottom of
every class in the engine, not somewhere in the middle of a particular one.

### 7.3 Non-copyable, non-movable for resource owners

Anything owning a GPU handle, file handle, thread, or unique scene state is
non-copyable **and** non-movable. Move ownership with `std::unique_ptr<T>`
instead of writing a move constructor. Lightweight value types (`StorageIndex`,
`Clock`) `= default` their special members or omit them.

### 7.4 Virtual override discipline

- Every override carries `override`, including the destructor:
  `~VisibilitySystem() override = default;`.
- Do not also write `virtual` on a derived override - `override` implies it.

### 7.5 const-correctness and noexcept

- Methods that do not mutate state are `const`. Provide const/non-const getter
  pairs where both reads and writes are needed.
- Pass non-trivial types by `const T&`; pass small trivially-copyable types
  (ints, handles) by value.
- Use `noexcept` deliberately - on real move ctors/assign, and on pure
  observers where the guarantee matters to callers (`hasSource() const noexcept`).
  Do not reflexively annotate every method.

---

## 8. Templates

- **A template anyone else can instantiate lives entirely in a header.** No
  `.tpp`. Fourteen `.cpp`s define a template anyway, in three shapes, and all
  three keep that rule rather than break it:
    - **File-local**, in the anonymous namespace
      [3.1](#31-file-local-helpers-go-in-an-anonymous-namespace) already mandates
      (`io/scene/component_serializer.cpp:63-123`,
      `io/asset/cooked_loader.cpp:22-89`). Twelve of the fourteen.
    - **A private member template** whose only callers are in that same file -
      `BehaviorSystem::guard` (declared `system/script/behavior_system.h:172-173`,
      defined `behavior_system.cpp:54`) and `GLView::ensure`
      (`backend/opengl/gl_view.h:168`, defined `gl_view.cpp:22`).
    - **Explicitly instantiated,** where the header ends in `extern template`
      declarations and the `.cpp` holds the bodies plus one `template class` line
      per instantiation. `editor/framework/editor_commands.h:759-765` and
      `editor_commands.cpp:175-180` are the only case, and the reason is in the
      header: every panel that pushes a command would otherwise carry the full
      bodies. The set of instantiations is closed, so no other translation unit
      can ask for one that is not there.

  If a second translation unit could want an instantiation you have not named,
  it belongs in the header.
- Use `if constexpr` for compile-time type dispatch instead of SFINAE. From
  `system/script/reflected_behavior.h:35-60`, routing one reflected field by its
  type:

  ```cpp
  if constexpr (std::is_enum_v<V>) {
      // ... enum path
  } else if constexpr (IS_ASSET_REF<V>) {
      visitor.assetField(name, value.name, ASSET_TYPE<typename V::asset_t>);
  } else if constexpr (Reflect::IS_REFLECTED<V>) {
      // ... recurse into the nested struct
  } else {
      static_assert(DEPENDENT_FALSE<V>, "...");
  }
  ```

  Note the terminal `static_assert(DEPENDENT_FALSE<V>)`: an unhandled type is a
  compile error naming what to add, not a silently skipped field.

- Use fold expressions for parameter packs: `(fn(args), ...)` reads better than
  recursion.
- CTAD with an explicit **deduction guide** is the idiom for letting an
  aggregate deduce its template arguments at the call site. From
  `core/reflect.h`, the reflection `Field` type:

  ```cpp
  template<typename T, typename M>
  struct Field {
      std::string_view name;
      M T::*           ptr;
  };

  // Deduction guide: Field{"position", &Transform::position} deduces
  // T = Transform, M = glm::vec3 without spelling the arguments out.
  template<typename T, typename M>
  Field(const char*, M T::*) -> Field<T, M>;
  ```

  The guide keeps `Field` a plain aggregate (no constructor, so it stays a
  literal type usable in `constexpr` contexts) while still giving call-site
  deduction.

---

## 9. Error handling

| Context                          | Mechanism                          |
|----------------------------------|------------------------------------|
| Preconditions (programmer error) | `VKM_ASSERT(condition, "message")` |
| Initialization failures          | `throw std::runtime_error(...)`    |
| Runtime lookup not found         | return `nullptr` or `false`        |
| Invalid handle                   | null sentinel (index 0)            |

- `VKM_ASSERT` is from `vkmLog` and compiles to nothing in release - never put
  side-effectful code in the condition. Message form:
  `VKM_ASSERT(isAlive(entity), "Scene::add called with dead/stale entity")`.
- **No exceptions in hot paths** - systems, ECS queries, rendering. Exceptions
  are reserved for startup, asset loading, and explicit recovery boundaries.
- **An enum that indexes a table gets a `Count` sentinel and a `static_assert`
  pinning the table's length to it.** `core/engine.cpp:18-22` (`STAGE_NAMES` vs
  `SystemStage::Count`), `io/asset/asset_library.cpp:25-27` (`TYPE_DIRS` vs
  `AssetType::Count`). Without it, adding an enumerator is a silent
  out-of-bounds read at the next lookup.

### 9.1 Where `VKM_ASSERT` actually lives

All 30 uses are in `src/engine`, 29 of them in a foundation type - `Scene`,
`SparseSet`, `SlotAllocator`, `ResourceManager`, `HierarchyOperations`, `Bus`,
`Resource`. The thirtieth is in a System: `hierarchy_system.cpp:39` asserts the
`Hierarchy`-implies-`WorldTransform` pairing its own resolve reads through.
**Zero** in `src/backend`, `src/editor`, `src/tools` or `app/`, which guard and
degrade instead. And a destructive operation asserts *and* guards on the same
condition, so a release build refuses rather than corrupts: `scene.h:72-73`,
`resource_manager.h:122-123`, `slot_allocator.h:64-65` each pair the assert with
`if (!cond) return;`. The assert is for the programmer who broke it; the guard is
for the user who ships it.

### 9.2 Three channels, and which one an error takes

An error reaches a human three ways, and they are not interchangeable. Pick by
**who caused it and who can act on it.**

- **`LOG_ERROR` and friends** reach the developer's console. Always. Every
  failure logs.
- **`reportError(category, source, message)`** (`debug/engine_error_log.h:76`)
  reaches the editor's Errors tab and the cooker's load summary. For a
  recoverable failure the **project author** caused: a throwing script hook, a
  scene asset that will not resolve, a gameplay module that will not load.
- **`state.pushToast(ToastKind::...)`** (`editor/framework/editor_state.h:151`)
  reaches the author now, beside the gesture. For an **editor action** that
  succeeded or failed. Editor-only - all 37 sites are in `src/editor`.

`reportError` logs *and* appends, so it never wants a `LOG_ERROR` beside it. Only
the editor and the cooker install a sink (`editor/editor_system.cpp:144`,
`app/cooker/main.cpp:67`); the runtime installs none, so a shipped game keeps the
log line and nothing else.

**An editor operation that fails takes the first channel and the third,** not
`reportError` - the author caused nothing and there is nothing to file. The log
line carries the path and the reason for whoever reads a console; the toast
carries the filename for the author who just pressed the button.
`framework/scene_io_controller.cpp:127-129`, `:241-244` and
`framework/project_controller.cpp:38-39` are the shape.

The two are not always adjacent, and that is the part to get right: of the 16
error toasts in `src/editor`, 5 have a `LOG_ERROR` beside them and the rest sit
above a call that already logged where it failed. So write the toast at the
gesture and make sure something logged - not a second log line restating a
message the layer below already printed.

Logging is categorized: `#define VKM_LOG_CATEGORY "RENDER"` at the top of the
`.cpp`, then `LOG_TRACE` / `LOG_INFO` / `LOG_WARNING` / `LOG_ERROR` with
printf-style formatting. **A category names a subsystem, not a file** - 26 exist
across the tree and one of them, `"BACKEND::GL"`, covers 41 of the 43 backend
`.cpp`s - the two without it log nothing at all.
Reuse the one your neighbours use; inventing a narrower one splits a subsystem's
output across two filters for no gain.

---

## 10. Performance conventions

- `reserve()` before a loop that `push_back()`s a known count.
- `clear()` to reuse a buffer's capacity across frames - never `= {}` or
  reassign a fresh container.
- `memcpy` for bulk transfers of trivially-copyable data.
- `thread_local` for per-thread scratch, declared in a file-scope anonymous
  namespace.
- Iterate `SparseSet` densely instead of random access by id.
- Use generational handles; check generation rather than storing raw pointers.
- Early-continue / early-return over deep nesting. From
  `system/visibility/visibility_system.cpp:265-272`:

  ```cpp
  for (uint32_t i = 0; i < meshCount; ++i) {
      const bool visible = m_visibleFlags[i] != 0;
      const bool caster  = m_casterFlags[i]  != 0;
      if (!visible && !caster) continue;

      if (visible) m_result.entries.push_back(m_scratch[i]);
      if (caster)  m_result.shadowCasters.push_back(m_scratch[i]);
  }
  ```

### 10.1 PROFILE_* macros

Profile zones go through a facade and compile to no-ops in release. Engine code
never includes Tracy directly - only `debug/profiler.h` and
`backend/opengl/gl_profiler.h` do. Wrap the work, not the call site, and use the
`_NAMED` variants for runtime-known names.

There are **two** families, and a render pass needs the second:

| Family | Header | Macros |
|--------|--------|--------|
| CPU | `debug/profiler.h` | `PROFILE_SCOPE` / `_NAMED`, `PROFILE_PLOT` |
| GPU | `backend/opengl/gl_profiler.h` | `PROFILE_GPU_CONTEXT`, `PROFILE_GPU_COLLECT`, `PROFILE_GPU_SCOPE` / `_NAMED` |

`gl_profiler.h` pulls in the CPU macros too, so a backend file includes only it,
and its header block states the context/collect lifecycle. Two placement
conventions the macros cannot tell you:

- **A `System::update` opens with `PROFILE_SCOPE("<ClassName>")` as its first
  statement.** 13 of 14 do; `system/sky/sky_system.cpp` is the gap, not the
  licence.
- **A backend pass opens no zone of its own.** `gl_backend.cpp:279-282` already
  wraps every `execute()` in a `PROFILE_SCOPE_NAMED` *and* a
  `PROFILE_GPU_SCOPE_NAMED` keyed on the pass's registered name. Adding one
  inside `execute()` duplicates it. Sub-zones for phases within a pass are
  welcome - `gl_shadow_pass.cpp:134,140,144` splits gather / upload / draw.

---

## 11. Quick checklist before pushing

- [ ] Header has `#pragma once` and a `} // namespace Vkm::Engine` close comment.
- [ ] Includes ordered own-header / stdlib / third-party / local, blank line
      between groups.
- [ ] No tabs, no Unicode, no decorative separator comments.
- [ ] `&&` rvalue refs are spaced: `T && other`, parameter named `other`; a
      callable parameter is `Fn&& fn`, unspaced.
- [ ] Every virtual override has `override`.
- [ ] Struct members are bare; class members have `m_`.
- [ ] Data members are last, in a `private:` section of their own.
- [ ] No multi-paragraph `///` blocks; `@brief` is one sentence.
- [ ] No what-comments (`// Increment the counter`); no task/commit references.
- [ ] No `//` run past three lines in a function body; `///` on a member is one
      line.
- [ ] No declaration block that argues a decision, carries section headings,
      narrates history, or restates the signature
      ([6.1](#61-what-bounds-a-comment)). Its length is not the test.
- [ ] No `static` free functions in a `.cpp` - use an anonymous namespace.
- [ ] Hot paths use early-continue, `reserve()`, and `clear()` for reuse.

---

## 12. Anti-patterns reviewers flag

1. Local includes before stdlib.
2. `static` free functions in a `.cpp` (use an anonymous namespace).
3. Missing `override` on a virtual override.
4. `m_` on a struct member, or bare members on a class.
5. Multi-paragraph `///` blocks (switch to `/** @brief */`).
6. A blank ` *` line inside a `@brief` paragraph.
7. What-comments that restate the code.
8. Decorative separator comments - including a `// ----` banner introducing a
   section of a .cpp. Use an anonymous namespace and blank lines.
9. A `//` run past three lines inside a function body, or a declaration block
   that argues a decision, carries ALL-CAPS headings, narrates history, or
   restates the signature ([6.1](#61-what-bounds-a-comment)). On a declaration,
   length alone is not the defect - a seam's block is long because a caller
   cannot infer any of it.
10. Unicode in source - ASCII only.
11. `= default` move on a non-movable class (forgot the `= delete`).
12. Forward-declaring a type later used by value.
13. Commenting out an unused parameter name (`void f(int /*count*/)`) - the
    project builds with `-Wno-unused-parameter`, so keep the name
    (`void f(int count)`) or omit it entirely (`void f(int)`).

The design-level anti-patterns (speculative abstraction, half-finished
refactors) live in [implementation.md](implementation.md).

---

## 13. Known exceptions

Intentional deviations. Do not introduce new ones without team agreement.

### 13.1 `Resource` hybrid struct

`resource/resource.h` declares `struct Resource` with **bare member names**
(data-struct convention) but a full **out-of-line Rule of 5**. The reason: its
`source` descriptor is a `std::unique_ptr<nlohmann::json>` held against a
forward declaration, so the special members must be defined in `resource.cpp`
where the full json type is visible. It is semantically a data struct
(subclasses are loaded/saved generically by `name`) that happens to own a
non-trivial pointer. Do not copy this pattern - if your type needs the Rule of
5, it is almost always a class.

### 13.2 `ScriptComponent` move-only component

`system/script/script_component.h` holds
`std::vector<std::unique_ptr<Behavior>>`, making it **move-only** - the one ECS
component that is not a trivially-copyable aggregate. It works because
`SparseSet<T>` already has a `std::move` path for non-trivially-copyable types.
Deep copy goes through `Behavior::clone()`. Do not generalize from this: a
component should be a plain data struct unless it must own polymorphic instances.

### 13.3 Backend flat includes

Files under `src/backend/opengl/` use flat `gl_`-prefixed includes
(`#include "gl_backend.h"`) rather than module-qualified paths. The backend is a
single internal unit; the flat form keeps its includes short. Engine code never
reaches in - it sees only `RenderBackend` and friends.

The cost of a flat root is that a name can collide with vkmGL's, which is flat
too: `gl_texture.h` exists as both `src/backend/opengl/data/gl_texture.h` and
`modules/vkmGL/src/texture/gl_texture.h`. A quoted include resolves relative to
the including file first, so which one you get depends on where you are writing
from. When you add a backend header, check the name is not already vkmGL's.

### 13.4 Decorative log strings

Decorative separators are forbidden in source comments but allowed inside
runtime log strings (boot banner, build dump). They are visible output, not code
structure.

### 13.5 `VKM_LOG_CATEGORY` precedes the own-header

As covered in [section 3](#3-implementation-file-structure-cpp), the
`#define VKM_LOG_CATEGORY "..."` is the one `#define` allowed before the own
header include. Every other configuration macro stays in its natural position.

### 13.6 Terse value accessors

An accessor drops the `getX` prefix when its **name already reads as the thing
it returns**: `Handle::id()`, `Scene::environment()` / `entityCount()` /
`epoch()` / `physics()`, `GenerationIndex::alive()` / `generation()`,
`SparseSet::size()`, `ThreadPool::threadCount()`, `WindowManager::mode()` /
`vsync()`, `RenderSystem::backendInfo()` / `maxAnisotropy()` / `backend()`.

The test is the **name**, not the class's kind. `RenderSystem` is this guide's
archetypal class-with-behavior ([7.2.1](#721-data-members-get-their-own-trailing-section))
and three of its accessors are on that list, so "terse accessors are for value
types" is not the boundary and never was. `getX` is for the reads that are an
*operation* - where the prefix says work happens, or where the bare noun would
read as a command rather than a value.

Which means one class carries both, and that is correct rather than sloppy:
`RenderSystem::backendInfo()` beside `getSettings()`, `Scene::entityCount()`
beside `getStorage()` (which *creates* the storage if it is missing),
`WindowManager::mode()` beside `getWidth()`. Ask it per name, not per class.

### 13.7 A `System` subclass spells out the Rule of 5 it inherits

`core/system.h:98-102` already deletes all four copy/move members, so
[7.3](#73-non-copyable-non-movable-for-resource-owners) would let a stateless
subclass omit them. All 16 write them out anyway, `sky_system.h`,
`particle_system.h`, `animation_system.h` and `async_loader_system.h` included,
and those four hold no data members at all. Match them: the block is how a reader
recognises a `System` at a glance.

A system's class `@brief` also carries what no signature can - **which
`SystemStage` it runs at and why that one**, argued against a named sibling.
`sky_system.h:27-28` is the model: "Runs in the Simulation stage, so the rotation
it writes is in place before HierarchySystem resolves world transforms in the
Transform stage."

### 13.8 Reflected behavior fields are bare publics

Authored fields on `Behavior` subclasses are bare public members on a class,
violating 4.1 deliberately: the field name is the serialized identity (scene JSON
+ inspector label), and an `m_` prefix would leak into both. Runtime-only state on
behaviors still uses `m_`. The shipped model is `Spinner::degreesPerSecond`
(`templates/default/src/game.h:21`); the two example projects use the same shape
at larger scale (`examples/potion_runner/src/potion_runner.h`, 7 fields;
`examples/stress_arena/src/stress_arena.h`, 22).
