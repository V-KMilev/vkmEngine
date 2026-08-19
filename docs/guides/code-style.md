# Code Style Guide

The mechanical rules: how vkmEngine code is laid out, named, formatted, and
documented. Every rule here is observed in the current codebase. If an example
here disagrees with the code, treat the code as the source of truth and flag
the drift.

The goal is **predictability**. Someone opening a random file should be able to
guess the shape of the next file without looking. Two sibling guides cover the
parts above the mechanics:

- [development.md](development.md) - how to think about a problem and fit it to the engine.
- [implementation.md](implementation.md) - what separates a good implementation from a bad one.

---

## 1. Include roots and include order

Engine code lives under four include roots. Each has its own style:

| Root                  | Include style                    | Example                                  |
|-----------------------|----------------------------------|------------------------------------------|
| `src/engine/`         | module-qualified                 | `#include "system/render/render_view.h"` |
| `src/backend/opengl/` | flat (every file is `gl_`-prefixed) | `#include "gl_backend.h"`             |
| `src/tools/`          | module-qualified                 | `#include "loader/texture_loaders.h"`    |
| `src/editor/`         | module-qualified (from engine root) | `#include "core/system.h"`            |

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
only through the `RenderBackend` abstract interface (see
[development.md](development.md) for why this seam matters).

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
defining it afterward triggers `-Wmacro-redefined`. Canonical opening, taken
from `core/engine.cpp`:

```cpp
#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

#include <algorithm>
#include <chrono>

#include "logger.h"

#include "core/engine_config.h"
#include "debug/profiler.h"

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
| Constant         | UPPER_SNAKE_CASE             | `ALIVE_BIT`, `DEFAULT_THREAD_COUNT`  |
| Enum class value | PascalCase                   | `SystemStage::Render`                |
| Type alias       | PascalCase                   | `EntityId`, `MeshHandle`             |
| Template param   | single letter or PascalCase  | `T`, `ResourceType`                  |
| Namespace        | PascalCase, under `Vkm::`    | `Vkm::Engine`, `Vkm::GL`, `Vkm::Log` |
| File name        | snake_case                   | `render_view.h`, `gl_forward_pass.cpp` |

Namespaces nest under one umbrella - `Vkm::Engine` for engine code, `Vkm::GL`
for the vkmGL wrappers, `Vkm::Log` for vkmLog - with helper namespaces nested
further in (`Vkm::Engine::Math`, `Vkm::Engine::HierarchyOperations`). When a
file's whole content lives in one, open it in the C++17 one-line form -
`namespace Vkm::Engine::Math {`, closed by a single `}` - rather than opening
each level separately.

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
| Line length       | soft target ~110 columns; break parameter lists when wider    |
| Charset           | strictly ASCII in source and comments - no Unicode            |
| Namespace close   | always `} // namespace Name`                                  |

The access-specifier / member indentation is distinctive - note the double
indent on members:

```cpp
class System {
    public:
        virtual ~System() = default;

        virtual void update(FrameContext& ctx) = 0;

        bool isEnabled() const { return m_enabled; }

    private:
        bool m_enabled = true;
};
```

### 5.1 Vertical alignment

Align related initializers, defaults, and trailing comments when the column
form reads more clearly. Do not align across a blank line - alignment signals
"these belong together," and a blank line has broken that:

```cpp
glm::mat4 view           = {1.0f};
glm::mat4 projection     = {1.0f};
glm::mat4 viewProjection = {1.0f};

float m_fpsLogTimer = 0.0f;
bool  m_fpsLog      = false;
```

### 5.2 Multi-line parameter lists

When a parameter list does not fit on one line, break **every** parameter onto
its own line. Never half-break:

```cpp
void build(
    const Scene& scene,
    const ResourceManager& resources,
    const Visibility& visibility,
    uint32_t viewportWidth,
    uint32_t viewportHeight
);
```

### 5.3 No decorative separators

Do not divide code with banner comments:

```cpp
// ----------------------------- BAD -----------------------------
// === Section: rendering ===
// ***************************************************************
```

Organize with `public:` / `private:`, blank lines, and `@brief` docs. (Runtime
log strings like `LOG_INFO("---- Build ----")` are output, not code structure,
and are exempt - see [13.4](#134-decorative-log-strings).)

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

- **Templates live entirely in headers.** No `.tpp`, no template definitions in
  a `.cpp`.
- Use `if constexpr` for compile-time type dispatch instead of SFINAE:

  ```cpp
  if constexpr (std::is_trivially_copyable_v<T>) {
      std::memcpy(&m_data[dst], &m_data[src], sizeof(T));
  } else {
      m_data[dst] = std::move(m_data[src]);
  }
  ```

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
- Logging is categorized: `#define VKM_LOG_CATEGORY "RENDER"` at the top of the
  `.cpp`, then `LOG_TRACE` / `LOG_INFO` / `LOG_WARNING` / `LOG_ERROR` with
  printf-style formatting.

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
- Early-continue / early-return over deep nesting:

  ```cpp
  for (auto& pass : m_passes) {
      if (!pass->isEnabled()) continue;
      pass->execute(backend, view, resources);
  }
  ```

### 10.1 PROFILE_* macros

CPU/GPU profile zones use `PROFILE_*` macros from `debug/profiler.h`; they
compile to no-ops in release. Engine code never includes Tracy directly - go
through the facade. Wrap the work, not the call site, and use the `_NAMED`
variants for runtime-known names.

---

## 11. Quick checklist before pushing

- [ ] Header has `#pragma once` and a `} // namespace Vkm::Engine` close comment.
- [ ] Includes ordered own-header / stdlib / third-party / local, blank line
      between groups.
- [ ] No tabs, no Unicode, no decorative separator comments.
- [ ] `&&` rvalue refs are spaced: `T && other`. Parameter named `other`.
- [ ] Every virtual override has `override`.
- [ ] Struct members are bare; class members have `m_`.
- [ ] No multi-paragraph `///` blocks; `@brief` is one sentence.
- [ ] No what-comments (`// Increment the counter`); no task/commit references.
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
8. Decorative separator comments.
9. Unicode in source - ASCII only.
10. `= default` move on a non-movable class (forgot the `= delete`).
11. Forward-declaring a type later used by value.
12. Commenting out an unused parameter name (`void f(int /*count*/)`) - the
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

### 13.4 Decorative log strings

Decorative separators are forbidden in source comments but allowed inside
runtime log strings (boot banner, build dump). They are visible output, not code
structure.

### 13.5 `VKM_LOG_CATEGORY` precedes the own-header

As covered in [section 3](#3-implementation-file-structure-cpp), the
`#define VKM_LOG_CATEGORY "..."` is the one `#define` allowed before the own
header include. Every other configuration macro stays in its natural position.

### 13.6 Terse value accessors

Small value-like types keep single-noun accessors without the `getX` prefix
when the name reads as the thing itself: `Handle::id()`, `Scene::environment()`,
`GenerationIndex::alive()` / `generation()`, `SparseSet::size()`,
`ThreadPool::threadCount()`, `WindowManager::mode()` / `vsync()`,
`AnimationTrack::keyframeCount()`. The `getX` rule applies to behavioral
classes; renaming these idiomatic accessors would churn call sites for no
clarity gain. Do not mix styles on one class.

### 13.7 Reflected behavior fields are bare publics

Authored fields on `Behavior` subclasses (`CubeSpinner::degreesPerSecond`)
are bare public members on a class, violating 4.1 deliberately: the field
name is the serialized identity (scene JSON + inspector label), and an `m_`
prefix would leak into both. Runtime-only state on behaviors still uses `m_`.
