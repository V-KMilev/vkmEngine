# vkmEngine Code Style Guide

Canonical reference for all code in this repository. Follow these conventions when creating or modifying files.

---

## Naming

| Element | Convention | Examples |
|---------|-----------|----------|
| Classes / Structs | PascalCase | `Scene`, `RenderSystem`, `GLMaterial` |
| Methods | camelCase | `createEntity()`, `getScene()`, `forEach()` |
| Member variables | `m_` prefix + camelCase | `m_scene`, `m_entityAllocator`, `m_minPixels` |
| Local variables | camelCase | `meshCount`, `worldMin`, `deltaTime` |
| Constants | UPPER_SNAKE_CASE | `WORLD_AXIS_Y_UP`, `DEFAULT_WINDOW_WIDTH` |
| Enum classes | PascalCase type, PascalCase values | `LightType::Directional`, `EventPriority::HIGH` |
| Type aliases | PascalCase | `EntityId`, `TypeId`, `EventCallback` |
| Namespaces | PascalCase | `Engine`, `Core`, `Engine::HierarchyUtils` |
| Template params | Single uppercase or PascalCase | `T`, `First`, `Rest...`, `HandleType` |

**Getters/Setters:** `getX()` for object getters, `isX()` for bool getters, `setX()` for setters, `count()`/`size()` for quantities.

**Struct members:** Plain data structs (components, POD types) use bare names without `m_` prefix. Classes with behavior use `m_`.

```cpp
// Struct (data-only) -- no m_ prefix
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
};

// Class (owns resources, has behavior) -- m_ prefix
class Scene {
    SlotAllocator m_entityAllocator;
    std::vector<std::unique_ptr<ISparseSet>> m_components;
};
```

---

## File Organization

### Header Guards

Always use `#pragma once`. No `#ifndef` guards.

### Include Ordering

Group includes with blank-line separators in this order:

1. Corresponding header (for `.cpp` files)
2. Standard library headers (`<vector>`, `<string>`, etc.)
3. Third-party headers (`<glm/glm.hpp>`, `<imgui.h>`)
4. Local project headers (`"core/engine.h"`, `"ecs/scene.h"`)

```cpp
#include "animation/animation_system.h"

#include <algorithm>
#include <cmath>

#include "logger.h"

#include "ecs/scene.h"
#include "ecs/component/animation.h"
#include "ecs/component/transform.h"
#include "visibility/visibility.h"
```

### Include Paths

- Engine code: Module-qualified from `src/engine/` root -- `#include "core/engine.h"`
- OpenGL backend: Flat, all gl_-prefixed -- `#include "gl_backend.h"`
- Tools: From `src/tools/` root -- `#include "loader/texture_loaders.h"`

### Forward Declarations

Prefer forward declarations over includes when only pointers/references are used:

```cpp
struct GLFWwindow;

namespace Engine {
class Scene;
class ResourceManager;
}
```

---

## Formatting

### Indentation

4 spaces. No tabs.

### Braces

K&R style (opening brace on same line):

```cpp
class RenderSystem : public System {
    public:
        void update(FrameContext& ctx) override {
            if (condition) {
                // ...
            } else {
                // ...
            }
        }
};
```

### Access Specifiers

Indented by 4 spaces. Order: `public` (interface) -> `private` (implementation). Single `public:` then single `private:` section preferred. Exception: constructors/destructor can be in a separate initial `public:` block.

```cpp
class Window {
    public:
        Window(const std::string& title, int swapInterval = 0);
        ~Window();

        Window(const Window& other) = delete;
        Window& operator=(const Window& other) = delete;

        Window(Window && other) = delete;
        Window& operator=(Window && other) = delete;

    public:
        int getWidth() const;
        int getHeight() const;

    private:
        void cleanup();

    private:
        std::string m_title;
        int m_width = 0;
};
```

### Spacing

- Space after keywords: `if (`, `for (`, `while (`, `switch (`
- No space before function call parens: `createEntity()`
- Spaces around binary operators: `a + b`, `i < n`, `x == y`
- Space after comma: `fn(a, b, c)`
- No trailing whitespace

### Alignment

Align related member declarations and initializers for readability:

```cpp
struct StorageIndex {
    uint32_t index      = 0;
    uint32_t generation = 0;
};

float m_leftPanelWidth    = 260.0f;
float m_rightPanelWidth   = 340.0f;
float m_bottomPanelHeight = 200.0f;
```

### Multi-line Parameters

When parameters don't fit on one line, put each on its own line indented one level:

```cpp
void build(
    const Scene& scene,
    const ResourceManager& resources,
    const Visibility& visibility,
    uint32_t viewportWidth,
    uint32_t viewportHeight
);
```

---

## Classes and Structs

### When to Use Which

- **`struct`**: Data-only types, components, POD aggregates, simple handles
- **`class`**: Types with behavior, resource ownership, systems, interfaces

### Rule of 5

Always declare all 5 special member functions explicitly. Use named `other` parameter and space before `&&` for rvalue references:

```cpp
// Non-copyable, non-movable (resource-owning classes)
class GLMesh {
    public:
        GLMesh() = delete;
        ~GLMesh();

        GLMesh(const GLMesh& other) = delete;
        GLMesh& operator=(const GLMesh& other) = delete;

        GLMesh(GLMesh && other) = delete;
        GLMesh& operator=(GLMesh && other) = delete;

    public:
        explicit GLMesh(const MeshAsset& mesh);
};
```

```cpp
// Copyable/movable (lightweight handles, value types)
class Entity {
    public:
        Entity() = default;
        ~Entity() = default;

        Entity(const Entity& other) noexcept = default;
        Entity& operator=(const Entity& other) noexcept = default;

        Entity(Entity && other) noexcept = default;
        Entity& operator=(Entity && other) noexcept = default;
};
```

**Key points:**
- Copy operations grouped together, then move operations grouped together
- Blank line between copy and move groups
- Space before `&&`: `ClassName && other`, not `ClassName&& other`
- Named parameter `other` in all declarations, even when `= delete`

---

## Templates

### `if constexpr` for Type Dispatch

```cpp
if constexpr (std::is_same_v<T, MeshAsset>) {
    return m_meshStorage;
} else if constexpr (std::is_same_v<T, TextureAsset>) {
    return m_textureStorage;
}
```

### Fold Expressions for Parameter Packs

```cpp
if (!(std::get<SparseSet<Rest>*>(restStorages)->contains(entityIdx) && ...)) return;
```

### Templates Live in Headers

All template implementations go in `.h` files (no separate `.tpp` or explicit instantiation).

---

## Error Handling

| Context | Mechanism | Example |
|---------|-----------|---------|
| Programmer errors (preconditions) | `VKM_ASSERT` | `VKM_ASSERT(isAlive(entity), "dead entity")` |
| Initialization failures | `throw std::runtime_error` | Window/GLEW creation failures |
| Runtime lookups | Return `nullptr` or `false` | `findStorage<T>()` returns nullptr |
| Invalid handles | Sentinel value (index 0 = null) | `operator bool() { return index != 0; }` |

**No exceptions** in hot paths (systems, ECS queries, rendering).

---

## Comments

### Documentation Comments

Use `/** */` blocks for public API documentation:

```cpp
/**
 * @brief Create a new entity and assign a unique EntityId.
 * @return The created Entity.
 */
Entity createEntity();
```

### Inline Member Docs

Use `///<` for brief member documentation:

```cpp
uint32_t index      = 0; ///< Sparse slot index (0 = null/invalid)
uint32_t generation = 0; ///< Generation at time of creation
```

### Implementation Comments

Use `//` for implementation notes:

```cpp
// Swap-and-pop: move last dense element into the hole
if (dataIdx != lastIdx) { ... }
```

### TODOs

Format: `// TODO(vkm): description`

### Section Dividers

Use sparingly and consistently. Prefer file splits over large dividers.

---

## Constants and Enums

### Compile-Time Constants

```cpp
static constexpr uint32_t MAX_DEPTH = 32;
static constexpr int FRAME_HISTORY_SIZE = 240;
```

### Global Constants

```cpp
inline const glm::vec3 WORLD_AXIS_Y_UP = {0.0f, 1.0f, 0.0f};
```

### Enum Classes

Always use `enum class` (not bare `enum`):

```cpp
enum class LightType : uint8_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
};
```

---

## Performance Patterns

- Prefer `reserve()` before loops that `push_back()`
- Reuse containers across frames (`clear()` keeps capacity)
- Use `memcpy` for bulk trivially-copyable transfers
- Prefer `SparseSet` dense iteration over random access
- Use `thread_local` for per-thread scratch buffers
- Mark trivial accessors `noexcept`
- Use generational handles to prevent use-after-free

---

## Charset

Source files must be strictly ASCII. No Unicode box-drawing, arrows, or special symbols in comments or string literals. Use plain ASCII equivalents (`----`, `->`, `*`).

---

## Namespace Closing Comments

Always annotate namespace closing braces:

```cpp
namespace Engine {
// ...
} // namespace Engine
```
