# Code Style Guide

The mechanical rules: how vkmEngine code is laid out, named, formatted and
documented, so that any file's shape can be guessed from its neighbours. Where
an example here and the code disagree, the code is right and the example is a
defect to report.

---

## Absolutes

- `#pragma once`, never include guards.
- **ASCII only**, in source, in comments and in the manual.
- **color**, as GL and ImGui spell it, in source, in comments and in the manual.
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

The `docs` suite (`tests/docs/docs_tests.cpp`) checks most of these in the
engine, the examples and the template: an undeclared or `static` function in a
`.cpp`, a Windows macro name, a stray `@param`, a log category out of step with
the logging, a line past 110 columns or a tab (shaders included), a list broken
any way but whole or broken when it fits, a comment inside an argument list, a
class missing a special member, a struct with a private section, color spelt the
British way - and a reference page that is not ASCII, cites a line number, or
names a file, link or member that does not exist.

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

The editor's two roots are its own and the engine's; its own root files are
included bare (`#include "editor_state.h"`). Otherwise always include the
**module path** (`"ecs/scene.h"`), never the bare filename (`"scene.h"`).

**A backend header never shares a name with a vkmGL one**: vkmGL's names are
flat too, and a quoted include resolves beside the including file first, so
which of the two you got would depend on where you wrote it.

**Engine code never reaches into `backend/`.** It sees the GPU only through
`RenderBackend` and `EditorRenderHooks` in `system/render/`, and the window's
`WindowSurface` in `platform/window/` ([engine.md](engine.md#absolutes)). A host
constructing `GLBackend` and `GLWindowSurface` is the composition root, not an
exception.

Includes come in three groups, one blank line between: standard library,
third-party (`<glm/glm.hpp>`, `<imgui.h>`, ...), then local - last, so a local
header that drags in a standard one cannot hide a missing include.

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

Forward-declare a type you use only by **pointer or reference** in a signature;
include its header when it appears by value, as a base class, or where a template
needs the full definition. Forward declarations go immediately inside the
namespace; a type from *another* namespace takes a block of its own, closed with
a bare `}` (`backend/opengl/gl_frame_context.h` shows both kinds).

### 2.1 Reflected types close the namespace first

`VKM_REFLECT_BEGIN` opens `namespace Vkm::Engine::Reflect` itself, so it goes at
**global scope, after the namespace close**, naming the type fully qualified:

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

The own header comes first so a missing include inside it fails here. Only
`#define VKM_LOG_CATEGORY "..."` may precede it: `logger.h` defaults the
category when nothing set it, so defining it afterwards redefines the macro.

### 3.1 File-local helpers go in an anonymous namespace

A file may open several, each directly above the definitions that use it -
which is also how a long `.cpp` marks where a section begins. Helpers inside
take no extra prefix (`detail_`, `_internal`).

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

The three lifetime prefixes mark state that outlives a call - each a threading
question. A file-scope `g_` lives in the anonymous namespace.

Helper namespaces nest under their umbrella (`Vkm::Engine::Math`); a file whose
content lives in one opens it in the one-line form,
`namespace Vkm::Engine::Math {`, closed by one `}`.

**The OpenGL backend is `Vkm::Engine`.** `Vkm::GL` is vkmGL; every `Vkm::GL`
block in the backend forward-declares a vkmGL type.

### 4.0 Do not redefine what glm or the standard library already names

`glm::pi<float>()`, `glm::epsilon<float>()` and the rest of
`glm/gtc/constants.hpp` are the engine's vocabulary; a local `PI` or `EPSILON`
is a second name that drifts. Declare your own constant only when the value is
the engine's - a slope limit, a sleep threshold, a cascade count.

### 4.0.1 Three names Windows has already taken

`windef.h` defines `near`, `far` and `pascal` as empty macros: on Windows a
local of those names vanishes and the next line is a syntax error naming
neither.

### 4.1 The struct/class member rule

| Kind                | Members      | Rule of 5 | Examples                          |
|---------------------|--------------|-----------|-----------------------------------|
| Data-only struct    | bare `name`  | none      | `Transform`, `ObjectDraw`, `FrameContext` |
| Class with behavior | `m_name`     | explicit  | `Engine`, `RenderSystem`, `SparseSet<T>`, `Resource` |

If you want `m_` on a struct member, the struct is probably a class. If you are
skipping the Rule of 5 on a `class`, it is probably a struct.

### 4.2 Method names

- **An accessor is named for what it returns** when the bare noun reads as the
  thing: `Handle::id()`, `Scene::entityCount()`. `get` stays where it says work
  happens or where the noun alone would read as a command -
  `Scene::getStorage<T>()` creates the storage. Decide per name, not per class;
  the `getX()` accessors already in the tree are not renamed for this alone.
- **A predicate reads as a yes-or-no question about its object**:
  `Clock::isPaused()`, `SparseSet::contains(key)`, `WindowManager::shouldClose()`.
  A function whose `bool` is an outcome - `load`, `save`, `open` - is named for
  the work, as any mutator is. Setters are `setX(value)`.
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
        bool isPaused() const { return m_paused; }

    private:
        bool m_paused = false;
};
```

### 5.1 Vertical alignment

Align related initializers, defaults and trailing comments when the columns read
better, never across a blank line. Alignment is for columns of declarations; a
continuation line is indented, never aligned to an open paren or an operand
above it ([5.3](#53-a-list-that-does-not-fit)).

### 5.2 Where the rvalue spacing rule bends

A **forwarded callable or pack** is unspaced - `Fn&& fn`, `Args&&... args`,
`auto&&... f` - since it reads as one token. Every other `&&` is spaced,
forwarding references included: `Scene::add(EntityId, T && component)`.

### 5.3 A list that does not fit

A parameter list, an argument list or a braced initializer list sits on one line
when the line fits in 110 columns. When it does not, it breaks whole: the opener
ends its line, each item takes a line of its own one indent past the statement,
and the closer starts a line at the statement's indent, carrying whatever
follows it (`);`, `) const override {`):

```cpp
void build(
    const Scene& scene,
    const Visibility& visibility,
    const UIDrawData* ui,
    const UIDrawData* cover,
    const SplashFrame* splash,
    const PoseBuffer* poses,
    const LiveParticles* particles,
    const OceanData* ocean
);
```

Never the forms between: items aligned under the open paren, several items to a
continuation line, or the closer tucked after the last item - each is redone by
hand the day a name changes length. `tools/reflow_lists <file>...` rewrites a
file's parenthesised lists to these two forms, changing only whitespace; a
braced list, a table and a control statement's condition it leaves to you, and
the `docs` suite names what remains.

Five shapes keep a layout of their own:

- **A call whose last argument is a lambda** opens the lambda on the call's line
  and closes with `});`, because the lambda is the call's body.
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
      LoadingSystem& loadingSystem,
      BehaviorSystem& behaviorSystem,
      ScriptModule& scriptModule
  )
      : m_cameraController(cameraController)
      , m_renderSystem(renderSystem)
      ...
  {
  ```

- **A CMake command** keeps its target and scope keyword on the opener's line,
  then one item to a line and the `)` alone: `target_link_libraries(vkm_core
  PRIVATE` above the libraries.
- **A raw-string regular expression** stays one literal, whatever its length.

A long **expression** is better split into named steps than wrapped. When one
must wrap, it breaks before an operator and the continuation is indented one
level past the statement, never aligned under an operand:

```cpp
const bool movable = body.invMass > 0.0f
    && body.motion == RigidbodyMotion::Dynamic
    && body.sleepTimer < SLEEP_TIME;
```

### 5.4 No decorative separators

No `// ----- BAD -----` or `// === Section ===`; organize with access
sections, blank lines, anonymous namespaces and `@brief` blocks. A separator
inside a runtime log string (the boot banner) is output, and exempt.

### 5.5 Name the value, do not annotate it

No comment goes inside an argument list:

```cpp
// bad
setupEngineApp(engine, AppConfig{title.c_str(), /*startPaused=*/false, /*logFps=*/true}, surface);

// good
AppConfig config;
config.windowTitle = title.c_str();
config.logFps      = true;
setupEngineApp(engine, config, surface);
```

Nothing keeps such a comment in step with the parameter it names. Where the call
has to show what a value means, name the value: a field set by name, a local, a
constant, an enum in place of a bool.

---

## 6. Documentation and comments

The default is **no comment**. Write one only when a reader would otherwise have
to ask: why does this exist, what invariant does it hold that the types cannot,
why is the obvious alternative wrong. First try to make the code say it - a name
for the value, a function for the step, a type for the state. A declaration
whose name and signature say everything takes no doc block.

| Style                | Use for                                                          |
|----------------------|-----------------------------------------------------------------|
| `/** @brief ... */`  | Public API a caller needs more than the signature for           |
| `///`                | A note on a member or function, above it                         |
| `///< trailing`      | A short note on a data member, beside it when the line fits     |
| `//`                 | Inside a function body - the *why*                               |

### 6.1 What bounds a comment

**Inside a function, a comment is bounded by lines**: `//` runs 1-3 lines, and
`///` on a plain member is one. Past three lines it is usually a diagnosis
([implementation.md](implementation.md#9-a-long-comment-is-a-diagnosis)); the
exception is a correctness argument the code cannot state, at the one place a
reader would otherwise reconstruct it. A body comment that needs a second
paragraph may have become a document, which belongs under `docs/reference/`
with a pointer left behind.

**On a declaration, a comment is bounded by relevance to a caller**, who reads
it *instead of* the implementation: one sentence of `@brief`, then whatever a
caller cannot work out - what it is, why it has this shape, what it must not be
asked to do. A block is too long when a line of it is something the caller
already knew, never because it passed a count.

At any length, a comment may not:

- **Speak for other code.** *Name* it - a symbol or file the build can resolve,
  *"see `GLSceneCapture`"* - instead of describing what it does. No list of
  readers or callers, no *"every X"*, and no count of anything a `static_assert`
  or a test does not hold.
- **Argue.** A commit defends a choice; a comment states the constraint the
  choice protects.
- **Narrate history** - what it used to be, what was tried, what a measurement
  once said.
- **Carry section headings** - ALL-CAPS titles, numbered parts, a second topic
  after a blank ` *` line.
- **Restate the signature.** Not `@param scene The scene` but
  `@param scene Scene whose Light components are gathered`.

The test that settles most cases: **strike the comment and reread.** If a
competent reader of this engine would now ask a question, keep the answer to it
and nothing else.

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

    /**
     * @brief Compute the model matrix from transform data.
     *
     * @param transform Local TRS to compose, without intermediate matrix products.
     * @return translation * rotation * scale.
     */
    static glm::mat4 computeModelMatrix(const Transform& transform);
};
```

### 6.2 The bounds are about the comment, not the language

[6.1](#61-what-bounds-a-comment) reads the same in a `CMakeLists.txt`, a shader
and the Python under `tools/`: a `#` run inside a `foreach()` is a body comment;
a block above a target, a function or a uniform is a declaration block. Build
files earn more explanation per line - an `install()` destination, `PUBLIC`
against `PRIVATE` - since none of it is inferable from the line. The build as a
whole belongs in [../reference/building.md](../reference/building.md).

### 6.3 What the build checks in a comment

- The `docs` suite (`tests/docs/docs_tests.cpp`) fails when a comment names a
  file that does not exist, a `function()` the source does not declare, a
  qualified name whose scope does not have that member or whose scope the tree
  does not declare at all, a `@param` that is not a parameter, or narrates
  history in a phrase that has no present-tense reading.
- A Clang build turns `-Wdocumentation` into an error for first-party code: a
  `@param` or `@return` that does not match the declaration.
- `tools/comment_sweep` lists, for a change, every comment and doc line
  elsewhere that names what the change touched. Read them before committing.

No check sees a sentence that names real code and says something untrue about
it; that is what a review reads for ([review.md](review.md#7-reviewing-a-change)).

### 6.4 The manual

The docs and the code split the work. A page under `docs/reference/` explains
how a subsystem works - its idea and its concepts; the code holds the detail -
fields, steps, constants, edge cases - and a page names the file rather than
copying it. The guides hold how we work: the practices, what is good and what is
bad, and the workflow. Both exist to help their reader, most often an agent
working in this repo with a limited context: a line it does not need costs it,
and every sentence is one more thing to keep true.

- **What it is, how to use it, what it promises.** A paragraph on what the
  subsystem is for; the calls a project or the engine makes, with an example
  only where the use is not obvious; the contracts and limits a caller must
  respect; and the one non-obvious *why* that stops a reader "fixing" it wrong,
  in a sentence or two.
- **No measurements, no hardware.** A millisecond, a percentage or a frame rate
  is true of one machine on one day. "Cheap enough to run every frame" is what a
  reader needs; the number goes in the commit. A number stays only when it is a
  contract - a capacity, a default, a format's limit.
- **No extremes and no derivations.** One plain example, not the edge case that
  took a week; the conclusion, not the working that reached it.
- **Not the header again.** A field's meaning lives on the field. The page says
  how the pieces fit, and names the header for the rest.
- **One fact, one page.** Where another page owns it, link.
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
deletes copy and move. Its class `@brief` also says **which stage it runs at and
why that one**, against a named sibling (`system/sky/sky_system.h`).

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
8. `private:` data - always last, in a section of its own even when that means
   two `private:` blocks.

### 7.3 Non-copyable, non-movable for resource owners

Anything owning a GPU handle, file handle, thread or unique scene state is
non-copyable **and** non-movable; ownership moves with `std::unique_ptr<T>`, never
a hand-written or `= default` move. A value class (`Clock`) defaults its five.

### 7.4 Virtual override discipline

An override carries `override` and never also `virtual`
(`~VisibilitySystem() override = default;`).

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
    - **file-local**, in the anonymous namespace;
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
  unhandled type fails to compile rather than being skipped.
- Fold expressions for packs, not recursion.
- CTAD with an explicit deduction guide keeps an aggregate an aggregate while
  deducing at the call site (`Field` in `core/reflect.h`).

---

## 9. Logging and profiling

Logging is categorized: `#define VKM_LOG_CATEGORY "RENDER"` above the own
header ([section 3](#3-implementation-file-structure-cpp)), then `LOG_TRACE` /
`LOG_INFO` / `LOG_WARNING` / `LOG_ERROR` with printf-style formatting.

- **A category names a subsystem, not a file.** The whole backend logs as
  `"BACKEND::GL"`; reuse your neighbours'.
- **The category goes with the logging** (an absolute): a file that logs with
  none sends its lines where no filter reaches them.

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

It holds `std::vector<std::unique_ptr<Behavior>>`, so it cannot be copied; deep
copy goes through `Behavior::clone()`. Do not generalize from it.

### 10.2 Reflected behavior fields are bare publics

Authored fields on a `Behavior` subclass are bare public members, breaking
[4.1](#41-the-structclass-member-rule) on purpose: the field name is the
serialized key and the inspector's label, so `m_` would leak into both. Runtime
state on the same class takes `m_`. The model is
`Spinner::degreesPerSecond` (`templates/default/src/game.h`).
