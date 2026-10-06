# Code Style Guide

The mechanical rules: how vkmEngine code is laid out, named, formatted and
documented. Every rule here is observed in the tree; where an example and the
code disagree, the code is right and the example is a defect to report.

The goal is **predictability** - someone opening a random file should be able to
guess the shape of the next one. The judgment above the mechanics is in the
sibling guides: [engine.md](engine.md), [design.md](design.md),
[implementation.md](implementation.md), [review.md](review.md).

---

## Absolutes

Mechanics admit no judgment. Every one of these is a rule.

- `#pragma once`, never include guards.
- **ASCII only**, in source, in comments and in the manual.
- **4 spaces**, never tabs.
- In a `.cpp`, the **own header is the first include**, alone. The only thing
  allowed before it is `#define VKM_LOG_CATEGORY`.
- **No `static` free functions in a `.cpp`.** A function a `.cpp` defines is
  declared in a header or sits in an anonymous namespace.
- **Every virtual override carries `override`**, destructors included.
- **Struct members are bare; class members take `m_`.**
- **A class's data members come last, in a `private:` section of their own** -
  never mixed with nested types, methods or constants. A data-only struct is the
  other shape: bare members first, static helpers after
  ([4.1](#41-the-structclass-member-rule)).
- **A template another file could instantiate lives entirely in a header.** No
  `.tpp` ([section 8](#8-templates)).
- **A namespace you put definitions in closes with `} // namespace Name`.** A
  block that only forward-declares closes with a bare `}`.
- **Backend code is `Vkm::Engine`,** not `Vkm::GL` ([section 4](#4-naming)).
- **Nothing is named `near`, `far` or `pascal`** ([4.0.1](#401-three-names-windows-has-already-taken)).
- **No decorative separator comments** of any kind.
- **No task, version or commit reference in a comment.** That is the commit's job.
- **A comment states a constraint to a reader** - in C++, CMake, GLSL and Python
  alike. It never argues a decision to a reviewer, never narrates history, and
  never carries section headings ([6.1](#61-what-bounds-a-comment)).
- **A `@param` names a parameter of the declaration it documents.**
- **A list that does not fit on one line breaks whole** - opener last on its
  line, one item per line, closer first on its line - never aligned to the open
  paren and never several items to a line ([5.3](#53-a-list-that-does-not-fit)).
- **No comment inside an argument list** (`/*startPaused=*/false`)
  ([5.5](#55-name-the-value-do-not-annotate-it)).
- **Every `class` writes its Rule of 5** ([7.1](#71-rule-of-5---write-it-out)).
- **A `.cpp` that logs declares its `VKM_LOG_CATEGORY`; one that does not,
  declares none** ([section 9](#9-logging-and-profiling)).
- **A reference page says what is.** No history, no release numbers, no line
  numbers ([6.4](#64-the-manual)).

What a machine can check is checked: `tests/docs/docs_tests.cpp` fails the
build on a function a `.cpp` exposes undeclared or makes `static`, a Windows
macro name, a stray `@param`, a log category out of step with the logging, a
line past 110 columns or a tab (shaders included), a parameter, argument or
braced list broken any way but whole, several items to a line of a list that is
not a table, a list broken when it fits, a comment inside an argument list, a
class missing any of its five special members, or a struct with a private
section - in the engine, the examples and the template alike - and on a
reference page that is not ASCII, cites a line number, or names a file, link or
member that does not exist.

---

## 1. Include roots and include order

Code lives under six include roots:

| Root                  | Include style                    | Example                                  |
|-----------------------|----------------------------------|------------------------------------------|
| `src/engine/`         | module-qualified                 | `#include "system/render/render_view.h"` |
| `src/backend/opengl/` | module-qualified; its own root files are flat | `#include "asset/gl_mesh.h"`, `#include "gl_backend.h"` |
| `src/tools/`          | module-qualified                 | `#include "import/texture_loaders.h"`    |
| `src/editor/`         | module-qualified, from **two** roots | `#include "panels/inspector_panel.h"` and `#include "ecs/scene.h"` |
| `app/`                | repo-root-qualified              | `#include "app/engine_app.h"`            |
| `tests/`              | area-qualified; the harness is bare | `#include "physics/physics_support.h"`, `#include "support.h"` |

The backend's own root files are flat because vkmGL exports only flat names,
which is also why **a backend header never shares a name with a vkmGL one**: a
quoted include resolves beside the including file first, so which of the two you
got would depend on where you wrote it.

The editor has a root of its own and the engine's, so an editor `.cpp` includes
`panels/`, `command/` and `ui/` from the first and `core/`, `ecs/`, `system/`
from the second; its own root files are included bare (`#include
"editor_state.h"`). The hosts add the repo root, which is why `app/` is spelled
into the path.

Always include the **module path**, never the bare filename:

```cpp
// good
#include "ecs/scene.h"
#include "system/render/render_view.h"

// bad
#include "scene.h"
#include "render_view.h"
```

**Engine code never reaches into `backend/`.** It sees the GPU only through
`RenderBackend` and `EditorRenderHooks` in `system/render/`
([engine.md](engine.md#absolutes)). A host constructing `GLBackend` is not an
exception but the point: the host is the composition root, the one place that
picks which backend the engine gets, or none.

Within a file, includes come in groups separated by one blank line:

1. Standard library (`<vector>`, `<cstdint>`, ...)
2. Third-party (`<glm/glm.hpp>`, `<imgui.h>`, `<nlohmann/json_fwd.hpp>`, ...)
3. Local project includes

A local header that drags in a standard header can hide a missing include when
the order is reversed; that is why local comes last.

---

## 2. Header file structure (.h)

```
1.  #pragma once
2.  Standard library includes
3.  Third-party includes        (if any)
4.  Local project includes
5.  namespace Vkm::Engine {
6.    Forward declarations      (if needed)
7.    Class / struct / free-function definitions
8.  } // namespace Vkm::Engine
9.  VKM_REFLECT_BEGIN(...) block (if the type is reflected - 2.1)
```

Groups are separated by one blank line.

Forward-declare a type you use only by **pointer or reference** in a signature;
include its header when it appears by value, as a base class, or where a template
needs the full definition. Forward declarations go immediately inside the
namespace:

```cpp
namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct Visibility;

struct RenderView { /* ... */ };

} // namespace Vkm::Engine
```

A type from *another* namespace takes a block of its own, closed with a bare `}`
(`backend/opengl/gl_frame_context.h` shows both kinds).

### 2.1 Reflected types close the namespace first

`VKM_REFLECT_BEGIN` opens `namespace Vkm::Engine::Reflect` itself, so it goes at
**global scope, after the namespace close**, naming the type fully qualified.
From `ecs/component/core/transform.h`:

```cpp
} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Transform)
    VKM_F(position)
    VKM_F(rotation)
    VKM_F(scale)
VKM_REFLECT_END()
```

What else a component owes the scene format is
[design.md](design.md#24-a-component-that-serializes).

---

## 3. Implementation file structure (.cpp)

```
1.  Own header include      (first, alone)
2.  Standard library includes
3.  Third-party includes     (if any)
4.  Local project includes
5.  namespace Vkm::Engine {
6.    anonymous namespace { ... }   (file-local helpers, if any)
7.    Definitions
8.  } // namespace Vkm::Engine
```

The **own header comes first** so it compiles as if it were first in any
translation unit, which catches a missing include inside it.

The one thing allowed above it is `#define VKM_LOG_CATEGORY "..."`: the own
header pulls in `logger.h`, which defaults the category when nothing set it, and
defining it afterwards redefines the macro. From `core/engine.cpp`:

```cpp
#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

#include <atomic>
#include <csignal>

#include "logger.h"

#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"

namespace Vkm::Engine {
```

### 3.1 File-local helpers go in an anonymous namespace

Never `static` free functions. A file may open several anonymous namespaces,
each directly above the definitions that use it - which is also how a long
`.cpp` marks where a section begins, instead of a banner comment that can go
stale:

```cpp
namespace Vkm::Engine {

namespace {

constexpr const char* STAGE_NAMES[] = {"Input", "Simulation", "Transform", "Visibility", "Render", "Editor"};

} // namespace

void Engine::run() { /* ... */ }

} // namespace Vkm::Engine
```

Helpers inside take no extra prefix (`detail_`, `_internal`): the namespace
already restricts them.

---

## 4. Naming

| What             | Convention                   | Example                              |
|------------------|------------------------------|--------------------------------------|
| Class / struct   | PascalCase                   | `RenderView`, `ObjectDraw`           |
| Method           | camelCase                    | `addSystem()`, `getScene()`          |
| Class member     | `m_` + camelCase             | `m_scene`, `m_systemsByStage`        |
| Struct member    | bare camelCase               | `position`, `viewportWidth`          |
| Local variable   | camelCase                    | `deltaTime`, `worldMin`              |
| File-scope state | `g_` + camelCase             | `g_interrupted`                      |
| `thread_local`   | `t_` + camelCase             | `t_isWorker`                         |
| `static` local   | `s_` + camelCase             | `s_iniPath`                          |
| Constant         | UPPER_SNAKE_CASE             | `ALIVE_BIT`, `DEFAULT_TICK_RATE`     |
| Enum class value | PascalCase                   | `SystemStage::Render`                |
| Type alias       | PascalCase                   | `EntityId`, `MeshHandle`             |
| Template param   | single letter or PascalCase  | `T`, `ResourceType`                  |
| Namespace        | PascalCase, under `Vkm::`    | `Vkm::Engine`, `Vkm::GL`, `Vkm::Log` |
| File name        | snake_case                   | `render_view.h`, `gl_forward_pass.cpp` |

The three lifetime prefixes mark state that outlives a call, which makes each a
threading question: `g_interrupted` (`core/engine.cpp`) is written from a signal
handler, `t_isWorker` (`platform/threading/thread_pool.cpp`) is how a worker
recognises itself. A file-scope `g_` lives in the anonymous namespace.

Namespaces nest under one umbrella - `Vkm::Engine` for engine code, `Vkm::GL`
for vkmGL, `Vkm::Log` for vkmLog - with helper namespaces further in
(`Vkm::Engine::Math`). A file whose content lives in one opens it in the one-line
form, `namespace Vkm::Engine::Math {`, closed by one `}`.

**The OpenGL backend is `Vkm::Engine`, not `Vkm::GL`.** `Vkm::GL` is vkmGL, the
platform layer; every `Vkm::GL` block in the backend is a forward declaration of
a vkmGL type. A backend file that opens `namespace Vkm::GL` compiles and puts
its type in the wrong library's namespace. Where a type lives and which
namespace it opens are the same question.

### 4.0 Do not redefine what glm or the standard library already names

`glm::epsilon<float>()`, `glm::pi<float>()`, `glm::half_pi<float>()` and the
rest of `glm/gtc/constants.hpp` are the engine's vocabulary. A local `PI` or
`EPSILON` is a second name for a value that already has one, and the two drift.
That holds for tolerances too: guarding a division or asking whether a vector
can be normalized is a question about floats, and `glm::epsilon<float>()`
answers it.

Declare a constant of your own when the value is the engine's: a slope limit, a
sleep threshold, a cascade count. If you cannot say what the number means in the
engine's terms, it is an epsilon in disguise.

### 4.0.1 Three names Windows has already taken

`windef.h` defines `near`, `far` and `pascal` as empty macros, so on Windows a
local of any of those names vanishes and the next line is a syntax error that
mentions neither.

### 4.1 The struct/class member rule

| Kind                | Members      | Rule of 5 | Examples                          |
|---------------------|--------------|-----------|-----------------------------------|
| Data-only struct    | bare `name`  | none      | `Transform`, `ObjectDraw`, `FrameContext` |
| Class with behavior | `m_name`     | explicit  | `Engine`, `RenderSystem`, `SparseSet<T>`, `Resource` |

If you want `m_` on a struct member, the struct is probably a class. If you are
skipping the Rule of 5 on a `class`, it is probably a struct.

### 4.2 Method names

- **An accessor is named for what it returns** when the bare noun reads as the
  thing: `Handle::id()`, `Scene::entityCount()`, `SparseSet::size()`,
  `Resource::name()`. `get` stays where it says work happens or where the noun
  alone would read as a command - `Scene::getStorage<T>()` creates the storage,
  so it sits beside `entityCount()` on one class. Decide per name, not per class;
  the `getX()` accessors already in the tree are not renamed for this alone.
- **A predicate reads as a yes-or-no question about its object**:
  `Clock::isPaused()`, `SparseSet::contains(key)`, `NetSession::simulates(entity)`,
  `WindowManager::shouldClose()`. `is` / `has` is the common form, not the only
  one. A function whose `bool` is an outcome - `load`, `save`, `open` - is named
  for the work, as any mutator is. Setters are `setX(value)`.
- Mutators are verb-first: `addSystem`, `removeFromParent`, `clear`, `commit`.

---

## 5. Formatting

| Rule              | Setting                                                       |
|-------------------|---------------------------------------------------------------|
| Indent            | 4 spaces, never tabs                                          |
| Braces            | K&R - opening brace on the same line                          |
| Access specifier  | indented 4 spaces from `class`                                |
| Member body       | indented 8 spaces from `class` (4 inside the access specifier) |
| Keyword spacing   | `if (`, `for (`, `while (`, `switch (`                        |
| Call spacing      | `fn()`, `obj.method()` - no space before `(`                  |
| Pointer / ref     | `T& name`, `T* name` - the `&`/`*` binds to the type          |
| Rvalue ref        | `T && name` - one space each side                             |
| Callable param    | `Fn&& fn` - unspaced ([5.2](#52-where-the-rvalue-spacing-rule-bends)) |
| Line length       | 110 columns; a list that would pass it breaks whole (5.3)     |
| Statements        | one to a line; a body of more than one takes its own lines    |
| Namespace close   | `} // namespace Name`; a forward-declaration block, bare `}`  |

Note the double indent on members:

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

### 5.1 Vertical alignment

Align related initializers, defaults and trailing comments when the columns read
better. Never align across a blank line - alignment says "these belong
together", and a blank line has said they do not. Alignment is for columns of
declarations; a continuation line is never aligned to an open paren or to an
operand above it - it is indented ([5.3](#53-a-list-that-does-not-fit)):

```cpp
float m_deltaTime   = 0.0f;
float m_simDelta    = 0.0f;
float m_accumulator = 0.0f;

bool  m_paused       = false;
int   m_pendingSteps = 0;
float m_timeScale    = 1.0f;
```

### 5.2 Where the rvalue spacing rule bends

A **forwarded callable or pack** is unspaced - `Fn&& fn`, `Args&&... args`,
`auto&&... f` - because it reads as one token: the thing you hand a lambda to.
Every other `&&` is spaced, forwarding references included:
`Scene::add(EntityId, T && component)`.

### 5.3 A list that does not fit

A parameter list, an argument list or a braced initializer list sits on one line
when the line fits in 110 columns. When it does not, it breaks whole, in the one
form the tree uses: the opener ends its line, each item takes a line of its own
one indent past the statement, and the closer starts a line at the statement's
indent, carrying whatever follows it (`);`, `) const override {`). From
`app/editor/main.cpp` and `system/render/render_view.h`:

```cpp
engine.addSystem<Vkm::Engine::EditorSystem>(
    Vkm::Engine::SystemStage::Editor,
    engine.getWindow().getWindowContext(),
    cameraController,
    sys.render,
    engine.getRenderSettings(),
    sys.audio,
    sys.behaviors,
    scriptModule
);

void build(
    const Scene& scene,
    const Visibility& visibility,
    const UIDrawData* ui,
    const SplashFrame* splash,
    const PoseBuffer* poses,
    const LiveParticles* particles
);
```

Never the forms between: items aligned under the open paren, several items to a
continuation line, or the closer tucked after the last item. Each is a layout a
reader has to decode, and each is redone by hand the day a name changes length.
`tools/reflow_lists <file>...` rewrites a file's parenthesised lists to these
two forms - joining one that fits, breaking one that does not - changing only
whitespace; a braced list, a table and a control statement's condition it
leaves to you, and the `docs` suite names what remains.

Five shapes keep a layout of their own:

- **A call whose last argument is a lambda** opens the lambda on the call's line
  and closes with `});`, because the lambda is the call's body:

  ```cpp
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
      return keys[a] < keys[b];
  });
  ```

- **A table of data** - a matrix, vertex rows, an X-macro list, an array whose
  rows mean something - is laid out as its rows, columns aligned, and a row of a
  macro table may pass 110 columns rather than break the table.
- **A constructor's initializer list** that does not fit on the signature's line
  starts on the next, one indent in, one member to a line with the comma leading,
  and the body's brace takes a line of its own so it does not read as part of the
  last member:

  ```cpp
  EditorSystem::EditorSystem(
      GLFWwindow* window,
      CameraControllerSystem& cameraController,
      RenderSystem& renderSystem,
      RenderSettings& render,
      AudioSystem& audioSystem,
      BehaviorSystem& behaviorSystem,
      ScriptModule& scriptModule
  )
      : m_cameraController(cameraController)
      , m_renderSystem(renderSystem)
      ...
  {
      ...
  ```

- **A CMake command** keeps its target and scope keyword on the opener's line,
  then one item to a line and the `)` alone: `target_link_libraries(vkm_core
  PRIVATE` above the libraries.
- **A raw-string regular expression** stays one literal, whatever its length.

A long **expression** is better split into named steps than wrapped. When one
must wrap - a condition, a return, a sum - it breaks before an operator and the
continuation is indented one level past the statement, never aligned under an
operand:

```cpp
const bool movable = body.invMass > 0.0f
    && body.motion == RigidbodyMotion::Dynamic
    && body.sleepTimer < SLEEP_TIME;
```

### 5.4 No decorative separators

```cpp
// ----------------------------- BAD -----------------------------
// === Section: rendering ===
```

Organize with access sections, blank lines, anonymous namespaces and `@brief`
blocks. There is no banner anywhere in `src/`, `app/`, `examples/` or
`templates/`, so one arriving in a diff is new. A separator inside a runtime log
string - the boot banner, the build dump - is output, not structure, and is
exempt.

### 5.5 Name the value, do not annotate it

No comment goes inside an argument list:

```cpp
// bad
setupEngineApp(engine, AppConfig{title.c_str(), /*startPaused=*/false, /*logFps=*/true});

// good
AppConfig config;
config.windowTitle = title.c_str();
config.logFps      = true;
setupEngineApp(engine, config);
```

A comment there is one more thing to keep in step with the parameter it names,
and nothing checks that it is. Where the call has to show what a value means -
two bare bools side by side always do - name the value (a field set by name, a
local, a constant, an enum in place of a bool) rather than label it.

---

## 6. Documentation and comments

The default is **no comment**. Write one only when a reader would otherwise have
to ask: why does this exist, what invariant does it hold that the types cannot,
why is the obvious alternative wrong (a measured cost, a platform quirk, an
upstream bug).

Before writing one, try to make the code say it: a name for the value, a
function for the step, a type for the state. A comment that restates the line
under it goes; a declaration whose name and signature say everything a caller
needs takes no doc block at all. Code a reader cannot follow without a paragraph
beside it is usually code to restructure, not to annotate.

| Style                | Use for                                                          |
|----------------------|-----------------------------------------------------------------|
| `/** @brief ... */`  | Public API a caller needs more than the signature for           |
| `///`                | A note on a member or function, above it                         |
| `///< trailing`      | A short note on a data member, beside it when the line fits     |
| `//`                 | Inside a function body - the *why*                               |

### 6.1 What bounds a comment

Two kinds of comment, bounded two ways.

**Inside a function, a comment is bounded by lines**, because every line of it
is a line of code the reader is not reading: `//` runs 1-3 lines, and `///` on a
plain member is one. Past three lines the comment is usually a diagnosis -
[implementation.md](implementation.md#8-a-long-comment-is-a-diagnosis) names
the three things it can mean. The exception is a correctness argument the code
cannot state, at the one place a reader would otherwise reconstruct it: why
friction is clamped as a vector, why last tick's impulse is applied before the
first pass. Strike one of those and a competent reader asks the question again.
A second paragraph in a body comment is the tell that it may have become a
document, which belongs under `docs/reference/` with a pointer left behind.

**On a declaration, a comment is bounded by relevance to a caller.** It is what
a caller reads *instead of* the implementation: one sentence of `@brief`, then
whatever a caller cannot work out - what it is, why it has this shape, what it
must not be asked to do. A seam or a format earns twenty lines when none of them
is inferable (`RenderBackend`, `EditorRenderHooks`, `AudioDevice`); a block is
too long when a line of it is something the caller already knew, never because
it passed a count.

At any length, a comment may not:

- **Speak for other code.** A comment says what the code it sits on does and
  why. When the why depends on code elsewhere, *name* that code - a symbol or a
  file the build can resolve - instead of describing what it does: *"see
  `GLSceneCapture`"* stays checkable, *"the capture re-binds the camera block"*
  goes stale the day the capture changes, and nothing points back at the comment.
  No list of readers or callers (*"the decal and GTAO passes read this"*,
  *"both callers"*), no *"every X"*, and no count of anything a `static_assert`
  or a test does not hold. Most wrong comments were true when written, about
  code that later moved.
- **Argue.** A commit defends a choice; a comment states the constraint the
  choice protects. *"Applying before pushing is deliberate and matches the rest
  of the editor"* is all argument. *"The rename is applied before the command is
  pushed: the command carries the reverse of an edit that has already happened"*
  (`editor/editor_actions.h`) is the constraint.
- **Narrate history.** What it used to be, what was tried, what a measurement
  once said. The log keeps that. *"Used to lag with big asset trees"* dates
  itself; *"lags with big asset trees, so this one scans once when the popup
  opens"* (`editor/ui/asset_picker.h`) stays true.
- **Carry section headings** - ALL-CAPS titles, numbered parts, a second topic
  after a blank ` *` line. A block that needs them has become a document.
- **Restate the signature.** `@param scene The scene` says nothing;
  `@param scene Scene whose Light components are gathered` says the half a caller
  could not guess.

The test that settles most cases: **strike the comment and reread.** If a
competent reader who knows this engine would now ask a question, keep the answer
to that question and nothing else.

Doxygen rules:

- `@brief` is **one complete sentence**, ending at the first blank line.
- A documented declaration uses the **multi-line** form - `/**` on its own line,
  then ` * @brief ...` - with `@param` / `@tparam` / `@return` where there are
  any. Never a single-line `/** @brief ... */`, and never a trailing `///<` on a
  function (that form is for data members).
- No multi-paragraph `///` block. A note on a data member may run to a few
  `///` lines above it when it does not fit beside it; a function's note that
  passes a line, or needs `@param`, is a `/** */` block. Never collapse a
  documented block down to a bare `@brief`.

```cpp
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};        ///< Local position
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};  ///< Local rotation as quaternion (identity = no rotation)
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};        ///< Local scale

    /**
     * @brief Compute the model matrix from transform data.
     *
     * Uses fused TRS construction: builds translation, rotation, scale
     * directly without intermediate matrix multiplications.
     *
     * @param transform The local TRS to compose.
     * @return Its model matrix, translation * rotation * scale.
     */
    static glm::mat4 computeModelMatrix(const Transform& transform);
};
```

### 6.2 The bounds are about the comment, not the language

[6.1](#61-what-bounds-a-comment) turns on what a comment *does*, which reads the
same in a `CMakeLists.txt`, a shader and the Python under `tools/`. A `#` run
inside a `foreach()` is a body comment; a block above a target, a function or a
uniform is a declaration block.

Build files earn more explanation per line than C++: an `install()` destination,
`PUBLIC` against `PRIVATE`, why a target is `INTERFACE` - none of it is
inferable from the line. What concerns the build as a whole belongs in
[../reference/building.md](../reference/building.md).

### 6.3 What the build checks in a comment

Comments are held the way the manual is: by checks that fail the build, so the
mechanical half of a review is never a person's job.

- The `docs` suite (`tests/docs/docs_tests.cpp`) fails when a comment names a
  file that does not exist, a `function()` the source does not declare, a
  qualified name whose scope does not have that member or whose scope the tree
  does not declare at all, a `@param` that is not a parameter, or narrates
  history in a phrase that has no present-tense reading.
- A Clang build turns `-Wdocumentation` into an error for first-party code: a
  `@param` or `@return` that does not match the declaration.
- `tools/comment_sweep` lists, for a change, every comment and doc line
  elsewhere that names what the change touched - the lines that state a fact
  the change may have made false. Read them before committing.

What no check can see - a sentence that names real code and says something
untrue about it - is what a review reads for
([review.md](review.md#7-reviewing-a-change)).

### 6.4 The manual

The pages under `docs/reference/` are bounded the way a declaration's comment
is, and for the same reason: they are read instead of the code, and believed. A
page says what the engine is and does.

- **No history.** Not what a thing used to be, which release built it, what was
  tried, or what a bug did before it was fixed: true the day it is written,
  misleading every day after. Restate the constraint in the present - "`playing`
  is session state; saved, a finished one-shot would restart on load". Where the
  history is a decision - built and reverted, measured and declined - it is a row
  in [engine.md section 4](engine.md#4-what-has-already-been-decided), the one
  place history is kept, and the page states the outcome.
- **No line numbers.** Cite a file and a symbol - `ecs/scene.h`, `Scene::add`.
  The first edit above a cited line makes the number lie.
- **What it names exists.** Every link, anchor, backticked path and qualified
  name in the manual is checked against the tree.

---

## 7. Class anatomy

### 7.1 Rule of 5 - write it out

Every `class` spells out its five special members, in this order - a resource
owner deletes copy and move, a value type defaults them:

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

The parameter is always `other`, even when deleted; a blank line separates the
copy pair from the move pair; `= default` for trivial members, `= delete` to
forbid. A gameplay `Behavior` is the exception: `clone()` copies it, and the
authored fields are its whole interface (10.2).

**A `System` subclass writes the block too**, though `core/system.h` already
deletes copy and move: the block is how a reader recognises a system at a
glance. Its class `@brief` also says **which stage it runs at and why that
one**, against a named sibling (`system/sky/sky_system.h`).

### 7.2 The public-block pattern

A class is laid out in this order, each block present only when it has
something, a blank line between blocks:

1. `public:` the types, aliases and constants a caller names;
2. `public:` construction and the Rule of 5;
3. `public:` the interface - static functions with it;
4. `public:` short inline accessors;
5. `protected:` the same order, for a base meant to be derived from;
6. `private:` types and constants;
7. `private:` methods;
8. `private:` data (7.2.1).

A reader opening any class finds what it is for at the top and what it holds at
the bottom.

### 7.2.1 Data members get their own trailing section

In a class, data members are **always last**, in a `private:` section of their
own, even when that means two `private:` blocks:

```cpp
class RenderSystem : public System {
    public:
        void update(FrameContext& ctx) override;

    private:
        void writeScreenshot(const std::string& path);

    private:
        std::unique_ptr<RenderBackend> m_backend;

        RenderView m_view;
};
```

The state of an object is what a reader most often looks for; it is at the
bottom of every class, not in the middle of some.

### 7.3 Non-copyable, non-movable for resource owners

Anything owning a GPU handle, file handle, thread or unique scene state is
non-copyable **and** non-movable; ownership moves with `std::unique_ptr<T>`, not
a hand-written move constructor, and never an `= default` move on a class that is
meant not to move. A value class (`Clock`) defaults its five; a plain struct
(`StorageIndex`) has none to write.

### 7.4 Virtual override discipline

Every override carries `override`, the destructor included
(`~VisibilitySystem() override = default;`), and never also `virtual`.

### 7.5 const-correctness and noexcept

- A method that does not mutate is `const`; provide const/non-const pairs where
  both reads and writes are needed.
- Pass non-trivial types by `const T&`, small trivially-copyable ones by value.
- `noexcept` deliberately - on real move operations and on observers where the
  guarantee matters to callers - not reflexively.
- An unused parameter keeps its name or loses it; never comment it out
  (`void f(int /*count*/)`). The build has `-Wno-unused-parameter`.

---

## 8. Templates

- **A template anyone else can instantiate lives entirely in a header.** A
  `.cpp` may define one in three shapes, all of which keep that rule:
    - **file-local**, in the anonymous namespace (the reflection driver in
      `io/scene/component_serializer.cpp`);
    - **a private member template** whose only callers are in that file
      (`GLView::ensure`);
    - **explicitly instantiated**: the header ends in `extern template`
      declarations and the `.cpp` holds the bodies plus one `template class` per
      instantiation (`editor/command/editor_commands.h`), because the set is
      closed and every includer would otherwise compile the bodies.

  If a second translation unit could want an instantiation you have not named,
  it belongs in the header.
- `if constexpr` for compile-time dispatch, not SFINAE, ending in a
  `static_assert(Reflect::DEPENDENT_FALSE<V>, "...")` (`core/reflect.h`) so an
  unhandled type is a compile error naming what to add rather than a silently
  skipped case.
- Fold expressions for packs, not recursion.
- CTAD with an explicit deduction guide keeps an aggregate an aggregate while
  deducing at the call site (`Field` in `core/reflect.h`).

---

## 9. Logging and profiling

Logging is categorized: `#define VKM_LOG_CATEGORY "RENDER"` above the own
header ([section 3](#3-implementation-file-structure-cpp)), then `LOG_TRACE` /
`LOG_INFO` / `LOG_WARNING` / `LOG_ERROR` with printf-style formatting.

- **A category names a subsystem, not a file.** The whole backend logs as
  `"BACKEND::GL"`; reuse your neighbours' rather than splitting a subsystem across
  two filters.
- **The category goes with the logging** (an absolute): a file that logs with
  none sends its lines where no filter reaches them, and a silent file that
  declares one reads as if it logs.

Which channel a failure takes - the log, `reportError` or a toast - is judgment,
not mechanics: [implementation.md](implementation.md#5-when-it-fails).

Profile zones go through a facade and compile to nothing without the profiler.
Engine code never includes Tracy; only `debug/profiler.h` and
`backend/opengl/gl_profiler.h` do.

| Family | Header | Macros |
|--------|--------|--------|
| CPU | `debug/profiler.h` | `PROFILE_SCOPE` / `_NAMED`, `PROFILE_PLOT` |
| GPU | `backend/opengl/gl_profiler.h` | `PROFILE_GPU_CONTEXT`, `PROFILE_GPU_COLLECT`, `PROFILE_GPU_SCOPE` / `_NAMED` |

- **A `System`'s frame entry point opens with `PROFILE_SCOPE("<ClassName>")`**
  as its first statement - `fixedUpdate` for a system with no `update`. A system
  with both suffixes the second (`"BehaviorSystem::fixed"`) so the two do not
  merge.
- **A backend pass opens no top-level zone**: the pass loop in
  `gl_backend.cpp` wraps every `execute()` in a CPU and a GPU zone named for the
  pass. Sub-zones for phases inside a pass are welcome.

---

## 10. Known exceptions

Deliberate deviations. Do not add one without the owner.

### 10.1 `ScriptComponent` is a move-only component

It holds `std::vector<std::unique_ptr<Behavior>>`, which makes it the one
component that cannot be copied. `SparseSet<T>` has a move path for such types;
deep copy goes through `Behavior::clone()`. Do not generalize from it.

### 10.2 Reflected behavior fields are bare publics

Authored fields on a `Behavior` subclass are bare public members on a class,
breaking [4.1](#41-the-structclass-member-rule) on purpose: the field name *is*
the serialized identity, and the inspector's label is made from it, so `m_`
would leak into both.
Runtime state on the same class takes `m_`. The model is
`Spinner::degreesPerSecond` (`templates/default/src/game.h`).
