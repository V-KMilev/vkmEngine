# Code Style Guide

This is the canonical style reference for vkmEngine. Every example is taken
from the real codebase — if an example here disagrees with the code, the
code is the bug. If you are about to write a new file, read this guide first.

The goal of this document is **predictability**: someone opening a random
file should be able to guess the layout of the next file without looking.

---

## Table of Contents

1. [Design philosophy](#1-design-philosophy)
2. [Project layout & include conventions](#2-project-layout--include-conventions)
3. [Header file structure (.h)](#3-header-file-structure-h)
4. [Implementation file structure (.cpp)](#4-implementation-file-structure-cpp)
5. [Naming](#5-naming)
6. [Formatting](#6-formatting)
7. [Documentation & comments](#7-documentation--comments)
8. [Class anatomy](#8-class-anatomy)
9. [Templates](#9-templates)
10. [Error handling](#10-error-handling)
11. [Performance conventions](#11-performance-conventions)
12. [Common anti-patterns](#12-common-anti-patterns)
13. [Known exceptions](#13-known-exceptions)

---

## 1. Design philosophy

The rules in the rest of this guide are mechanical: brace placement, header
order, naming. This section is the layer above them — how we *think* before
we type. If the mechanical rules answer "what does compliant code look
like?", this section answers "what does **good** code look like?"

vkmEngine is a long-term project that is growing month by month. Every
class, system, and abstraction you add will be read, extended, and
refactored by other people — and by your future self after you have
forgotten the details. Write for them. The cost of a bad design is paid
every time someone touches the code; the cost of a good design is paid
once, by you, today.

### 1.1 Good design — fit the engine, not the task

Before you write a new system, look at how the existing ones are shaped.
A new render pass should look like the other render passes; a new System
should plug into the system pipeline the same way every other System does;
a new ECS component should be a plain data struct, like every other
component. **The engine has a grain — work with it.**

Ask, in order:
1. **Does this already exist?** Half-written abstractions are worse than
   none. If 80 % of what you need is already in `HierarchyOperations`,
   extend it rather than starting a parallel utility.
2. **Where does it belong?** Hot-path data lives near hot-path code.
   Editor-only logic stays in `src/editor/`. Resource I/O lives in
   `tools/loader/`. If your code doesn't fit anywhere obvious, the
   directory tree is telling you something.
3. **What is the smallest change that fits?** A one-line method on an
   existing class is almost always better than a new helper file.

### 1.2 Good implementation — solve today's problem, not tomorrow's

Don't design for hypothetical futures. Concrete rules:

- **No flags "in case we ever need it."** Add the flag the day a real
  caller needs it, not the day you imagine one might.
- **No abstractions for one user.** If only one class implements the
  interface, the interface is a virtual function call wearing a costume.
  Wait for the second user; then refactor.
- **No "framework code" without a feature.** Internal frameworks justify
  themselves by removing duplication that already exists, not by
  promising to remove duplication that might appear.

When in doubt, **write the concrete version first**. Extracting an
abstraction from two working implementations is straightforward;
predicting the right abstraction from zero is guesswork.

### 1.3 Simple implementation — the simplest thing that fits

Pick the construct that costs the reader the least:

- A clear `if` / `else` beats a virtual hierarchy that exists to express
  two cases.
- Three slightly repetitive lines beat a premature template.
- A free function beats a singleton when one would do.
- A plain `struct` beats a `class` when there's no invariant to protect.

"Clever" is a warning sign. If a line takes you ten seconds to write and
the next reader thirty to understand, you owe the codebase the rewrite.
Most performance wins in this engine come from data layout (`SparseSet`,
generational handles, batched draws), not from clever syntax — there is
almost never a reason to be cute.

This is not an argument *against* good abstractions. `RenderBackend`,
`System`, and `SparseSet<T>` are excellent abstractions because they each
remove real duplication and serve many callers. The argument is against
**speculative** abstraction: complexity introduced before its weight is
justified.

### 1.4 Clean, easy-to-read code is the deliverable

Code is read far more often than it is written. The compiler doesn't care
how readable your function is, but the next person opening the file does.
A few habits make a large difference:

- **Name things for what they mean, not for what they are.** `entity` is
  better than `id`, `worldMatrix` is better than `m`. The variable name
  carries the comment you didn't write.
- **Keep functions short enough to see end-to-end.** If a function spans
  more than one screen, look for a sub-step that wants to become its own
  named helper.
- **One responsibility per function, one shape per file.** Files that mix
  unrelated types or behaviors are a sign the design slipped.
- **Early-return / early-continue** over deep nesting. The reader should
  not have to track which branch they're in three levels deep.
- **Vertically align related declarations.** Columns help the eye
  scan — see [§5 Formatting](#6-formatting).
- **Delete dead code.** Unused code is not "free" — every reader has to
  ask whether it matters. Commit the deletion; git remembers.

If you can't explain what a function does in one sentence, it probably
does too much.

### 1.5 Commented code — when, and only when, the *why* is non-obvious

The default is **no comment**. Good naming and clear structure do most of
the explaining. Add a comment when a reader would otherwise have to ask:

- Why does this code exist? (a hidden constraint, a bug it works around)
- What invariant does it preserve? (something the type system can't
  encode)
- Why is the obvious alternative wrong? (a measured performance reason,
  a platform quirk)

Do **not** comment to:
- Restate the code (`// Increment the counter`).
- Reference the current task or commit (`// Added in PR #142`).
- Sign off (`// vkm — refactored 2026-03-12`).

When you *do* need to comment, pick the right style — see
[§7 Documentation & comments](#7-documentation--comments). Public API
gets `/** @brief */`. Members get `///<` trailing or short `///` lines.
The why-notes inside a function body are plain `//`.

### 1.6 The long-term test

Before committing a non-trivial change, ask yourself the three long-term
questions:

1. **Will the next person understand this in five minutes** without asking
   me?
2. **If the engine doubles in size,** does this still fit, or will it have
   to be rewritten?
3. **If I delete this comment,** is the code still understandable?

If the answer to any of those is "no," the change isn't done yet.

---

## 2. Project layout & include conventions

Engine code lives under three include roots:

| Root                  | Style                       | Example include                              |
|-----------------------|-----------------------------|----------------------------------------------|
| `src/engine/`         | module-qualified            | `#include "system/render/render_pass.h"`     |
| `src/backend/opengl/` | flat (all files `gl_`-prefixed) | `#include "gl_backend.h"`                |
| `src/tools/`          | module-qualified            | `#include "loader/texture_loaders.h"`        |
| `src/editor/`         | module-qualified            | `#include "panels/hierarchy_panel.h"`        |

**Engine code never reaches into `backend/` directly.** Backend is selected
through the `RenderBackend` abstract interface.

**Always include the module path**, not the bare filename:

```cpp
// good
#include "ecs/scene.h"
#include "system/render/render_pass.h"
#include "resource/mesh_asset.h"

// bad
#include "scene.h"
#include "render_pass.h"
#include "mesh_asset.h"
```

---

## 3. Header file structure (.h)

Every header follows this skeleton, in this exact order:

```
1. #pragma once
2. (blank line)
3. Standard library includes
4. (blank line)
5. Third-party includes (if any)
6. (blank line)
7. Local project includes
8. (blank line)
9. namespace Engine {
10. Forward declarations (if needed) (all one tab in)
11. Class / struct / free-function definitions (all one tab in)
12. } // namespace Engine
```

Use `#pragma once` — never traditional `#ifndef` guards.

### 3.1 Example: data struct (`render_view.h`)

Data-only structs use **bare member names** (no `m_` prefix), default
initializers, and no Rule-of-5 boilerplate:

```cpp
#pragma once

#include <vector>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "resource/mesh_asset.h"
#include "resource/material_asset.h"
#include "ecs/component/light.h"

namespace Engine {

class Scene;
class ResourceManager;
struct Visibility;

struct CameraData {
    glm::mat4 view           = {1.0f};
    glm::mat4 projection     = {1.0f};
    glm::mat4 viewProjection = {1.0f};

    glm::vec3 position = {0.0f, 0.0f, 0.0f};
};

struct DrawableData {
    MeshHandle mesh;
    MaterialHandle material;
    MaterialType materialType = MaterialType::Opaque;

    glm::mat4 model = {1.0f};
};

struct RenderView {
    CameraData camera;

    std::vector<DrawableData> drawables;
    std::vector<LightData>    lights;

    uint32_t viewportWidth  = 0;
    uint32_t viewportHeight = 0;

    void build(
        const Scene& scene,
        const ResourceManager& resources,
        const Visibility& visibility,
        uint32_t viewportWidth,
        uint32_t viewportHeight
    );
};

} // namespace Engine
```

Key patterns:
- Align related member initializers vertically — readers scan the column.
- Multi-line parameters: each on its own line, indented one level.
- Forward-declare types used only as pointers/references in the header.
- Group related fields with blank lines (camera-related, vector-of-x, viewport).

### 3.2 Example: class with behavior (`render_pipeline.h`)

Classes that own resources or have non-trivial behavior use **`m_`-prefixed
members**, the **Rule of 5**, and the **two/three public-block pattern**:

```cpp
#pragma once

#include <memory>
#include <vector>
#include <cstdint>

#include "system/render/render_pass.h"

namespace Engine {

class RenderPipeline {
    public:
        RenderPipeline() = default;
        ~RenderPipeline() = default;

        RenderPipeline(const RenderPipeline& other) = delete;
        RenderPipeline& operator=(const RenderPipeline& other) = delete;

        RenderPipeline(RenderPipeline && other) = delete;
        RenderPipeline& operator=(RenderPipeline && other) = delete;

    public:
        void addPass(std::unique_ptr<RenderPass> pass);
        void clear();
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height);
        void execute(
            RenderBackend& backend,
            const RenderView& view,
            const ResourceManager& resources
        );

    public:
        size_t passCount() const { return m_passes.size(); }
        RenderPass& getPass(size_t index) { return *m_passes[index]; }
        const RenderPass& getPass(size_t index) const { return *m_passes[index]; }

    private:
        std::vector<std::unique_ptr<RenderPass>> m_passes;
};

} // namespace Engine
```

Block discipline:
- **First `public:`** — constructors, destructor, Rule-of-5. Always in this
  order: default ctor, dtor, copy-ctor, copy-assign, move-ctor, move-assign.
- **Second `public:`** — the class interface (methods that do work).
- **Third `public:`** *(optional)* — short inline accessors / getters.
- **`private:`** — members, always prefixed `m_`.

Spacing for copy/move:
- `ClassName && other` — single space between `ClassName` and `&&`,
  single space before parameter name.
- Parameter is always named `other`, even when the method is `= delete`.

### 3.3 Example: system subclass (`visibility_system.h`)

Subclasses of `System` add `override` and a destructor override:

```cpp
#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "core/memory/types.h"

namespace Engine {

class VisibilitySystem : public System {
    public:
        struct Settings {
            float minPixels   = 3.0f;    ///< Screen-pixel cull threshold.
            float maxDistance = 500.0f;  ///< World-space cull distance.
        };

        VisibilitySystem() = default;
        ~VisibilitySystem() override = default;

        VisibilitySystem(const VisibilitySystem& other) = delete;
        VisibilitySystem& operator=(const VisibilitySystem& other) = delete;

        VisibilitySystem(VisibilitySystem && other) = delete;
        VisibilitySystem& operator=(VisibilitySystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

        Settings&       getSettings()       { return m_settings; }
        const Settings& getSettings() const { return m_settings; }
        void setSettings(const Settings& s) { m_settings = s; }

    private:
        Settings m_settings;

        EntityId m_cachedCameraEntity{};
        Visibility m_result;  ///< Persistent buffer - vectors reuse capacity across frames.

        /// Persistent buffers for the multithreaded path - reuse capacity across frames.
        std::vector<uint8_t>   m_visibleFlags;
        std::vector<glm::mat4> m_modelMatrices;
        std::vector<glm::vec3> m_worldMins;
        std::vector<glm::vec3> m_worldMaxs;
};

} // namespace Engine
```

Key patterns:
- `override` on every virtual override (including the destructor).
- Tunables grouped into a nested `Settings` data-struct (bare members,
  default initializers) exposed through one get/set pair, instead of a
  getter/setter per field.
- Trivial accessors inline in the class body; align their bodies vertically.
- `///< trailing` for short member annotations.
- Align related initializers (the `Settings` fields, the accessor column).

### 3.4 Example: free-function header / namespace block (`frustum_culler.h`)

Headers that only export free functions still wrap them in the engine
namespace and may use a nested namespace for grouping:

```cpp
#pragma once

#include <glm/glm.hpp>

#include "system/visibility/visibility_context.h"

namespace Engine {

/**
 * @brief Frustum culling: reject AABBs fully outside the view frustum.
 *
 * Uses the half-space test: for each frustum plane, the AABB is outside if
 * its "positive vertex" (furthest along the plane normal) is on the
 * negative side.
 */
namespace FrustumCuller {

inline bool isVisible(
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    const VisibilityContext& context
) {
    // ... implementation in header because it's inline ...
}

} // namespace FrustumCuller

} // namespace Engine
```

### 3.5 Forward declarations

Forward-declare types you only refer to as **pointer or reference** in
member signatures. Include the full header only when:
- The type appears by value as a member.
- The type appears as a base class.
- A template needs the full definition.

Forward declarations go **inside** `namespace Engine { ... }`, immediately
after the namespace open:

```cpp
namespace Engine {

class Scene;
class ResourceManager;
struct Visibility;

struct RenderView { ... };

} // namespace Engine
```

---

## 4. Implementation file structure (.cpp)

Every `.cpp` follows this exact skeleton:

```
1. Corresponding header include
2. (blank line)
3. Standard library includes
4. (blank line)
5. Third-party includes (if any)
6. (blank line)
7. Local project includes
8. (blank line)
9. namespace Engine {
10. (Optional) anonymous namespace { ... } for file-local helpers
11. Method / function implementations
12. } // namespace Engine
```

The corresponding header is **always the first include**, alone on its line,
followed by a blank line. This compiles your own header as if it were the
first include in any TU — catching missing includes inside it.

**Include order matters.** Local includes do *not* come before stdlib. If a
file has only some of the groups, keep the relative order:

```cpp
// good — own header, stdlib, third-party, local
#include "system/render/render_view.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "logger.h"

#include "ecs/scene.h"
#include "system/visibility/visibility.h"
#include "resource/resource_manager.h"
```

```cpp
// bad — local-first, stdlib at the bottom
#include "panels/bottom_panel.h"
#include "framework/editor_common.h"      // local before stdlib
#include "system/render/render_system.h"

#include <cstdio>
#include <string>
```

### 4.1 Example: short implementation (`render_pipeline.cpp`)

```cpp
#include "system/render/render_pipeline.h"

#include "debug/statistics.h"

namespace Engine {

void RenderPipeline::addPass(std::unique_ptr<RenderPass> pass) {
    m_passes.emplace_back(std::move(pass));
}

void RenderPipeline::clear() {
    m_passes.clear();
}

void RenderPipeline::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    for (auto& pass : m_passes) {
        pass->onResize(backend, width, height);
    }
}

void RenderPipeline::execute(
    RenderBackend& backend,
    const RenderView& view,
    const ResourceManager& resources
) {
    for (auto& pass : m_passes) {
        if (!pass->isEnabled()) continue;
        PROFILE_SCOPE_NAMED(pass->getName().c_str());
        PROFILE_GPU_SCOPE_NAMED(pass->getName().c_str());
        pass->execute(backend, view, resources);
    }
}

} // namespace Engine
```

Key patterns:
- Own header always first.
- Multi-line parameter list mirrors the header.
- Early-continue over nested ifs.
- `std::move` on ownership transfer.
- One blank line between methods; no blank lines inside short methods.

### 4.2 Anonymous namespace for file-local helpers

**Never** use `static` free-functions in `.cpp`. Use an anonymous namespace
instead. It lives between the `namespace Engine {` open and the first
externally-visible definition:

```cpp
#include "system/render/render_view.h"

#include <algorithm>

#include "ecs/scene.h"

namespace Engine {

namespace {

void sortDrawables(std::vector<DrawableData>& drawables) {
    std::sort(drawables.begin(), drawables.end(), /* ... */);
}

} // namespace

void RenderView::build(/* ... */) {
    // ...
    sortDrawables(drawables);
}

} // namespace Engine
```

Helpers inside the anonymous namespace do **not** get an extra prefix
(`detail_`, `_internal`, etc.) — the namespace already restricts their
scope. Name them as you would any other function.

---

## 5. Naming

| What            | Convention                | Example                                      |
|-----------------|---------------------------|----------------------------------------------|
| Class / struct  | PascalCase                | `RenderPipeline`, `DrawableData`             |
| Method          | camelCase                 | `addPass()`, `getScene()`                    |
| Class member    | `m_` + camelCase          | `m_passes`, `m_minPixels`                    |
| Struct member   | bare camelCase            | `position`, `viewportWidth`                  |
| Local variable  | camelCase                 | `meshCount`, `worldMin`                      |
| Constant        | UPPER_SNAKE_CASE          | `MAX_DEPTH`, `WORLD_AXIS_Y_UP`               |
| Enum class      | PascalCase :: PascalCase  | `LightType::Directional`                     |
| Type alias      | PascalCase                | `EntityId`, `EasingFunction`                 |
| Template param  | PascalCase or single letter | `T`, `First`, `HandleType`                 |
| Namespace       | PascalCase                | `Engine`, `Core`, `Easing`                   |
| File name       | snake_case                | `render_pipeline.h`, `gl_forward_pass.cpp`   |

### 5.1 Method names

- **Getters:** `getX()` for non-trivial, `x()` allowed for cheap accessors.
  Boolean getters: `isX()` or `hasX()`.
- **Setters:** `setX(value)`.
- **Counters:** `count()` / `size()` (match the container convention).
- **Mutators:** verb-first (`addPass`, `removePass`, `clear`, `commit`).
- **Boolean-returning predicates:** verb-first (`isVisible`, `hasSource`,
  `wantsCapture`, `canExecute`).

### 5.2 Member naming — struct vs class

This is the most common slip-up. The rule:

| Kind                  | Members           | Rule-of-5 | Example                  |
|-----------------------|-------------------|-----------|--------------------------|
| Data-only struct      | bare `name`       | none      | `DrawableData`, `CameraData`, `Transform` |
| Class with behavior   | `m_name`          | full      | `RenderPipeline`, `VisibilitySystem`      |

If you find yourself wanting to put `m_` on a struct member, the struct is
probably a class. If you're skipping the Rule of 5 on a `class`, it's
probably a struct.

---

## 6. Formatting

| Rule              | Setting                                                  |
|-------------------|----------------------------------------------------------|
| Indent            | 4 spaces, **never** tabs                                 |
| Braces            | K&R (opening brace on the same line)                     |
| Line length       | Soft target ~110 cols; break parameter lists when wider  |
| Access specifiers | Indented 4 spaces from `class`                           |
| Member body       | Indented 8 spaces from `class` (4 inside the access spec)|
| Keyword spacing   | `if (`, `for (`, `while (`, `switch (` — space after keyword |
| Call spacing      | `fn()`, `obj.method()` — no space before `(`             |
| Pointer / ref     | `T& name`, `T* name` — `&`/`*` binds to the type         |
| Rvalue ref        | `T && name` — single space on either side of `&&`        |
| Alignment         | Vertically align related declarations and initializers   |
| Charset           | Strictly ASCII — no Unicode in source or comments        |
| Namespace close   | Always `} // namespace Name`                             |

### 6.1 No decorative separators

Do **not** divide code with separator lines like:

```cpp
// ---------------------------- BAD ----------------------------
// === Section: rendering ===
// ************************************************************
```

Use `public:` / `private:` access specifiers, blank lines, and `@brief`
documentation comments to organize sections.

Runtime *log strings* like `LOG_INFO("---- Build ----")` are not affected
by this rule — it applies to source-code comments only.

### 6.2 Vertical alignment

Align related initializers, default values, and trailing comments when the
column form is clearly more readable:

```cpp
// good
glm::mat4 view           = {1.0f};
glm::mat4 projection     = {1.0f};
glm::mat4 viewProjection = {1.0f};

float m_minPixels   = 3.0f;
float m_maxDistance = 500.0f;
```

Do not align across blank-line groups — alignment communicates "these are
related." Across a blank line, related-ness has been broken.

### 6.3 Multi-line parameter lists

When a parameter list doesn't fit on one line, break **every** parameter
onto its own line:

```cpp
void execute(
    RenderBackend& backend,
    const RenderView& view,
    const ResourceManager& resources
);
```

Don't half-break — either all on one line or all on their own lines.

---

## 7. Documentation & comments

vkmEngine uses four comment styles. Pick by audience and scope.

| Style                | Use for                                                      |
|----------------------|--------------------------------------------------------------|
| `/** @brief ... */`  | Public API: class / struct definitions, public methods on a class, free functions intended to be called from outside the file |
| `///`                | Single-line clarification on a member or function; short notes that don't need `@param` / `@return` structure |
| `///< trailing`      | Inline annotation on a struct / class member; the field name already carries the meaning |
| `//`                 | Implementation notes inside a function body. Explain **why**, not what. |

### 7.1 Rule: no multi-paragraph `///` blocks

If you find yourself writing more than ~3 consecutive `///` lines, or
needing `@param` / `@return` fields, switch to `/** @brief ... */`.

```cpp
// bad — too long for ///
/// Persistent buffer; reused across frames so vectors can keep their
/// capacity instead of reallocating every tick. The flush phase is
/// what clears them. Do not externally clear.
Visibility m_result;

// good
/**
 * @brief Persistent buffer; reused across frames so vectors keep their
 * capacity instead of reallocating every tick. The flush phase clears them.
 */
Visibility m_result;
```

### 7.2 Rule: don't break `@brief` with an empty continuation line

The `@brief` paragraph runs from the `@brief` tag to the **first blank
line** in the doxygen block. Inserting a blank ` *` line in the middle of
a sentence breaks the brief and confuses tooling:

```cpp
// bad — blank " *" inside @brief paragraph
/**
 * @brief Reads the local Transform and the Hierarchy parent link; writes
 *
 * only the resolved WorldTransform (pre-seeded at setParent time,
 * so this loop never adds storage).
 */

// good — @brief is a complete sentence; the body follows after one blank
/**
 * @brief Resolve hierarchical transforms into per-entity WorldTransform.
 *
 * Reads the local Transform and the Hierarchy parent link; writes only the
 * resolved WorldTransform (pre-seeded at setParent time, so this loop
 * never adds storage).
 */
```

The `@brief` should be a **single complete sentence**. Detail goes after
the first blank line.

### 7.3 Default to no comment

Add a comment when the *why* is non-obvious: a hidden constraint, a subtle
invariant, a workaround for a specific bug, behavior that would surprise
a reader.

```cpp
// good
// Capture name before move - resource is forwarded below.
auto savedName = name;
manager.add(std::move(resource));

// bad - explains what the code already says
// Increment the counter
++m_counter;

// bad - references the current task or commit
// Added in PR #142 for the auth migration
```

If removing the comment wouldn't confuse a future reader, don't write it.

### 7.4 Comments inside class bodies

For members where the name is enough, no comment. For members where the
constraint or invariant is non-obvious, use `///<` trailing on the same
line:

```cpp
private:
    bool m_enabled = true;   ///< Pass runs unless explicitly disabled.
    Visibility m_result;     ///< Persistent buffer - vectors reuse capacity across frames.
```

For longer explanations, put a `///` block on the line above the member,
or upgrade to `/** @brief */` if it exceeds three lines.

---

## 8. Class anatomy

### 8.1 Rule of 5 — write it out

Resource-owning classes spell out all five special members **explicitly**.
This prevents accidental copies and documents intent:

```cpp
class GLMesh {
    public:
        GLMesh() = default;
        ~GLMesh();

        GLMesh(const GLMesh& other) = delete;
        GLMesh& operator=(const GLMesh& other) = delete;

        GLMesh(GLMesh && other) = delete;
        GLMesh& operator=(GLMesh && other) = delete;
    // ...
};
```

- Order: default ctor, dtor, copy-ctor, copy-assign, move-ctor, move-assign.
- Parameter is always named `other`, even when `= delete`.
- `&&` has a space on both sides: `GLMesh && other`.
- Use `= default` for trivial implementations, `= delete` to forbid.

### 8.2 Non-copyable, non-movable for resource owners

Anything owning a GPU handle, file handle, thread, or unique scene state is
non-copyable **and** non-movable. Use `unique_ptr<T>` to move ownership
between containers; don't write a move constructor for it.

### 8.3 Virtual override discipline

- Every override carries `override`.
- Destructor of a polymorphic class: `~Foo() override = default;` (defaulted
  in the header is fine when no out-of-line resources are owned).
- Avoid `virtual` on methods of derived classes — `override` already implies it.

### 8.4 `const` correctness

- Methods that don't mutate state are `const`.
- Pass by `const T&` for non-trivial types; pass by value for small
  trivially-copyable types (ints, handles).
- Don't write `const` on by-value return types (`const int foo()`) — it has
  no effect.

### 8.5 `noexcept`

Use `noexcept` on:
- Move constructors and move assignment (when the type is actually movable).
- Destructors (already implicitly `noexcept`, but the explicit form is
  acceptable for clarity).
- Pure observers (`hasSource() const noexcept`) where exception-safety
  guarantees matter to callers.

Do not annotate every method with `noexcept` reflexively.

---

## 9. Templates

- **Templates live entirely in headers.** No `.tpp` files, no template
  definitions in `.cpp` files.
- Use `if constexpr` for compile-time type dispatch instead of SFINAE
  pyramids:
  ```cpp
  template<typename T>
  void process() {
      if constexpr (std::is_same_v<T, MeshAsset>) { /* ... */ }
      else if constexpr (std::is_same_v<T, TextureAsset>) { /* ... */ }
      else { static_assert(sizeof(T) == 0, "Unsupported type"); }
  }
  ```
- Use fold expressions for parameter packs: `(expr && ...)`,
  `(expr, ...)` — clearer than recursion.
- For class templates with both public template API and private template
  helpers, split into two regions:

  ```cpp
  template<typename T>
  class Foo {
      public:
          template<typename U> void publicHelper(U && value);

      private:
          template<typename U> void implHelper(U && value);
  };
  ```

- When you need to instantiate the same template across many TUs and
  compile times matter, declare `extern template` in the header and force
  the instantiation in one `.cpp` (see `editor_commands.h` for an example).

---

## 10. Error handling

| Context                              | Mechanism                          |
|--------------------------------------|------------------------------------|
| Preconditions (programmer error)     | `VKM_ASSERT(condition, "message")` |
| Initialization failures              | `throw std::runtime_error(...)`    |
| Runtime lookups (not found)          | Return `nullptr` or `false`        |
| Invalid handles                      | Null sentinel (index 0)            |

**No exceptions in hot paths** — systems, ECS queries, rendering. Exceptions
are reserved for startup, asset loading, and explicit recovery boundaries.

`VKM_ASSERT` is from `vkmLog`. It compiles to nothing in release builds, so
do not put side-effectful code inside the condition.

---

## 11. Performance conventions

- **`reserve()` before loops that `push_back()`.** If you know the final
  size, set it.
- **`clear()` to reuse capacity across frames** — never `= {}` or
  reassign with a fresh vector.
- **`memcpy` for bulk trivially-copyable transfers** when you have a
  contiguous source and destination.
- **`thread_local` for per-thread scratch buffers** — declare in an
  anonymous namespace at file scope.
- **Dense `SparseSet` iteration** over random access by id.
- **Generational handles** prevent use-after-free; check generation, don't
  store raw pointers.
- **Early-out / early-continue** to avoid deep nesting. Prefer the form:
  ```cpp
  if (!pass->isEnabled()) continue;
  pass->execute(...);
  ```
  over:
  ```cpp
  if (pass->isEnabled()) {
      pass->execute(...);
  }
  ```

### 11.1 PROFILE_* macros

CPU and GPU profile zones use `PROFILE_*` macros from `debug/profiler.h`.
They compile to no-ops when `VKM_PROFILER=0` (release builds). Engine
code never includes Tracy headers directly - go through the facade.

Place a CPU + GPU scope around the work, not the call site. Use the
`_NAMED` variants for runtime-known names (pass names, system labels);
use the literal variant otherwise.

```cpp
for (auto& pass : m_passes) {
    if (!pass->isEnabled()) continue;
    PROFILE_SCOPE_NAMED(pass->getName().c_str());
    PROFILE_GPU_SCOPE_NAMED(pass->getName().c_str());
    pass->execute(backend, view, resources);
}
```

The macros: `PROFILE_FRAME_MARK`, `PROFILE_SCOPE`, `PROFILE_SCOPE_NAMED`,
`PROFILE_GPU_CONTEXT`, `PROFILE_GPU_COLLECT`, `PROFILE_GPU_SCOPE`,
`PROFILE_GPU_SCOPE_NAMED`, `PROFILE_PLOT`.

---

## 12. Common anti-patterns

A non-exhaustive list of things reviewers ask people to fix:

1. **Local includes before stdlib.** Causes flaky builds when a stdlib
   header is dragged in transitively via a local header.
2. **`static` free-functions in `.cpp`.** Use an anonymous namespace.
3. **Missing `override`** on virtual overrides.
4. **`m_` on struct members** or **bare members on classes**. Pick one
   shape — see [§5.2](#52-member-naming--struct-vs-class).
5. **Multi-paragraph `///` blocks.** Switch to `/** @brief */` past ~3 lines.
6. **Empty ` *` line inside a `@brief` paragraph.** Either close the brief
   with a period and add a blank line, or keep the brief on one line.
7. **Identifier-explaining comments** (`// Increment the counter`).
   Remove them.
8. **Decorative separator comments** (`// ---- Section ----`). Use blank
   lines and access specifiers.
9. **Unicode in source.** ASCII only — no curly quotes, no em-dashes,
   no symbols.
10. **`= default` move on a non-movable class.** Forgot to write
    `= delete` for the move pair.
11. **Forward-declaring a type that is later used by value.** Compiler
    will reject; just include the header.
12. **Speculative abstraction.** A `Manager` / `Factory` / `Helper` with a
    single user is a virtual function call wearing a costume. Inline it
    until a second user appears.
13. **Half-finished refactors.** If you rename, rename everywhere; if you
    move a file, move all its callers. Half-done is worse than not-done.
14. **Plain `mkdir`-style nesting:** `if (a) { if (b) { if (c) { ... } } }`.
    Use early-returns.
15. **Commenting out unused parameter names** (`void foo(int /*count*/)`).
    Keep the name so the signature still reads as documentation; the
    project compiles with `-Wno-unused-parameter`, so no warning is
    silenced by hiding the name. Either write `void foo(int count)` or,
    if the parameter is truly meaningless to the reader, leave it
    unnamed: `void foo(int)`.

---

## 13. Known exceptions

There are a few intentional deviations in the codebase. New code should
not introduce new exceptions without team agreement.

### 13.1 `Resource` — hybrid struct

[`src/engine/resource/resource.h`](../../src/engine/resource/resource.h)
declares `struct Resource` with **bare member names** (data-struct
convention) but also defines a full **out-of-line Rule-of-5**. The reason
is that the source-descriptor JSON is held in a
`std::unique_ptr<nlohmann::json>` against a *forward declaration*, so the
special members must be defined in `resource.cpp` where the full type is
visible.

This is a deliberate compromise: the type is still semantically a data
struct (subclasses are loaded/saved generically by name), but it owns a
non-trivial unique_ptr. Do not copy this pattern lightly — if your type
needs Rule-of-5, it's almost always a class.

### 13.2 Backend flat includes

Files under `src/backend/opengl/` use **flat `gl_`-prefixed includes**
(`#include "gl_backend.h"`) instead of module-qualified paths. The backend
is a single internal compilation unit; the flat form keeps backend-local
includes short. Engine code never reaches into the backend — it only sees
`RenderBackend` and friends through engine headers.

### 13.3 `LOG_INFO("---- ... ----")`

Decorative separators are forbidden in source comments but allowed inside
**runtime log strings** (boot banner, build info dump). They are visible
output, not code structure.

### 13.4 `#define VKM_LOG_CATEGORY` precedes the own-header

§4 says the corresponding header is always the first include. The single
exception is `#define VKM_LOG_CATEGORY "..."`, which must come BEFORE the
own-header include because the own-header transitively pulls in
`logger.h`, and `logger.h` sets `VKM_LOG_CATEGORY = nullptr` if no value
is already defined. Setting the category after the own-header triggers a
`-Wmacro-redefined` warning.

Canonical layout:

```cpp
#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_system.h"   // own header (pulls logger.h)

#include <algorithm>
...
#include "logger.h"
...
```

This is the only `#define` that precedes the own-header. Other
configuration macros stay in their natural position (between own-header
and the include they configure).

---

## Appendix A — Quick checklist before pushing

- [ ] **Design:** does this fit the engine, or does it sit awkwardly
      beside it? Could it be simpler?
- [ ] **Implementation:** is anything in here speculative — a flag, a
      virtual, an abstraction — without a real user?
- [ ] **Readability:** can the next person read this top-to-bottom and
      follow it without asking me?
- [ ] Header has `#pragma once` and `} // namespace Engine` close comment.
- [ ] Includes ordered: own header, stdlib, third-party, local — each group
      separated by a blank line.
- [ ] No tabs, no Unicode, no decorative separator comments.
- [ ] `&&` rvalue refs are spaced: `T && other`.
- [ ] Every virtual override has `override`.
- [ ] Struct members are bare; class members have `m_`.
- [ ] No multi-paragraph `///` blocks.
- [ ] No `// Increment the counter`-style what-comments.
- [ ] No `/*name*/`-commented unused parameter names — keep the name or omit it.
- [ ] Hot-path code uses early-continue, `reserve()`, `clear()` for reuse.
- [ ] Each public class / method has either `/** @brief */` or no doc — no
      half-finished `///` blobs.
