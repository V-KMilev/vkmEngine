# Code Style Guide

Practical by-example guide for writing code in vkmEngine. All examples are taken from the actual codebase.

For the full canonical reference, see [STYLE_GUIDE.md](../../STYLE_GUIDE.md).

---

## Header File Structure (.h)

A header follows this skeleton:

```
1. #pragma once
2. Standard library includes
3. Third-party includes
4. Local project includes
5. Namespace open
6. Forward declarations (if needed)
7. Class / struct definition
8. Namespace close with comment
```

### Example: Data Struct (render_view.h)

Data-only structs use bare member names (no `m_` prefix), default initializers, and no Rule-of-5 boilerplate:

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
    std::vector<LightData> lights;

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
- Align related member initializers vertically
- Multi-line parameters: each on its own line, indented one level
- Forward declare types used only as pointers/references
- Group related fields with blank lines

### Example: Class with Behavior (render_pipeline.h)

Classes that own resources or have behavior use `m_` prefixed members, Rule-of-5, and the two-public-block pattern:

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

Key patterns:
- **First `public:` block** -- Constructors, destructor, copy/move (Rule of 5)
- **Second `public:` block** -- Class interface (methods)
- **Third `public:` block** (optional) -- Inline accessors/getters
- **`private:` block** -- Members with `m_` prefix
- Copy/move: `ClassName && other` (space before `&&`), named `other` even for `= delete`
- Non-copyable non-movable for resource-owning classes

### Example: System Subclass (visibility_system.h)

```cpp
#pragma once

#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

#include "core/system.h"
#include "core/memory/types.h"

namespace Engine {

class VisibilitySystem : public System {
    public:
        VisibilitySystem() = default;
        ~VisibilitySystem() override = default;

        VisibilitySystem(const VisibilitySystem& other) = delete;
        VisibilitySystem& operator=(const VisibilitySystem& other) = delete;

        VisibilitySystem(VisibilitySystem && other) = delete;
        VisibilitySystem& operator=(VisibilitySystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

        void setMinPixels(float minPixels) { m_minPixels = minPixels; }
        void setMaxDistance(float maxDistance) { m_maxDistance = maxDistance; }

        float getMinPixels() const { return m_minPixels; }
        float getMaxDistance() const { return m_maxDistance; }

    private:
        float m_minPixels   = 3.0f;
        float m_maxDistance  = 500.0f;

        EntityId m_cachedCameraEntity{};
        Visibility m_result;  ///< Persistent buffer - vectors reuse capacity across frames.

        std::unordered_map<uint32_t, glm::mat4> m_worldMatrixCache;
        std::vector<std::vector<VisibleEntity>> m_workerResults;
};

} // namespace Engine
```

Key patterns:
- `override` on virtual methods, `= default` on destructor override
- Inline trivial getters/setters in the class body
- `///< comment` for brief member docs
- Align related member initializers

### Example: Open Template Registry (scene.h)

```cpp
class Scene {
    public:
        Scene() = default;
        ~Scene() = default;
        // ... Rule of 5 ...

    public:
        Entity createEntity();
        void destroyEntity(Entity entity);

        template<typename T>
        auto& add(Entity entity, T && component);

        template<typename T>
        bool has(Entity entity) const;

        template<typename T>
        T& get(Entity entity);

        template<typename First, typename... Rest, typename Fn>
        void forEach(Fn&& fn);

    private:
        template<typename T>
        SparseSet<T>& getStorage();

        template<typename T>
        const SparseSet<T>* findStorage() const;

    private:
        SlotAllocator m_entityAllocator;
        std::vector<std::unique_ptr<ISparseSet>> m_components;
};
```

Key patterns:
- Templates live entirely in headers (no .tpp files)
- `if constexpr` for type dispatch
- Fold expressions for parameter packs: `(expr && ...)`
- Public template API, private template helpers

---

## Implementation File Structure (.cpp)

A .cpp follows this skeleton:

```
1. Corresponding header include
2. (blank line)
3. Standard library includes
4. (blank line)
5. Third-party includes (if any)
6. (blank line)
7. Local project includes
8. (blank line)
9. Namespace open
10. Anonymous namespace for file-local helpers (if needed)
11. Method implementations
12. Namespace close with comment
```

### Example: render_pipeline.cpp

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
        pass->execute(backend, view, resources);
        STATS_RECORD_RENDER_PASS();
    }
}

} // namespace Engine
```

Key patterns:
- Corresponding header is always the first include
- Short methods: no blank lines between simple one-liners
- Multi-line parameters aligned same as in the header
- Early-continue over nested ifs
- `std::move` for ownership transfer

### Example: render_view.cpp (with anonymous namespace)

```cpp
#include "system/render/render_view.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "logger.h"

#include "ecs/scene.h"
#include "system/visibility/visibility.h"
#include "ecs/component/mesh.h"
#include "ecs/component/transform.h"
#include "resource/resource_manager.h"

namespace Engine {

namespace {
    // File-local helpers in anonymous namespace
    void sortDrawables(std::vector<DrawableData>& drawables) {
        // ... sorting logic ...
    }
}

void RenderView::build(
    const Scene& scene,
    const ResourceManager& resources,
    const Visibility& visibility,
    uint32_t viewportWidth,
    uint32_t viewportHeight
) {
    drawables.clear();
    lights.clear();
    // ...
}

} // namespace Engine
```

Key patterns:
- Anonymous namespace for file-local helpers (never `static` functions)
- Include groups separated by blank lines: own header, stdlib, third-party, local

---

## Naming Quick Reference

| What | Convention | Example |
|------|-----------|---------|
| Class/Struct | PascalCase | `RenderPipeline`, `DrawableData` |
| Method | camelCase | `addPass()`, `getScene()` |
| Class member | `m_` + camelCase | `m_passes`, `m_minPixels` |
| Struct member | bare camelCase | `position`, `viewportWidth` |
| Local var | camelCase | `meshCount`, `worldMin` |
| Constant | UPPER_SNAKE | `MAX_DEPTH`, `WORLD_AXIS_Y_UP` |
| Enum class | PascalCase::PascalCase | `LightType::Directional` |
| Type alias | PascalCase | `EntityId`, `EasingFunction` |
| Template param | PascalCase or single letter | `T`, `First`, `HandleType` |
| Namespace | PascalCase | `Engine`, `Core`, `Easing` |

Getters: `getX()`, `isX()` (bool), `count()`/`size()`.
Setters: `setX()`.

---

## Error Handling

| Context | Mechanism |
|---------|-----------|
| Preconditions (programmer error) | `VKM_ASSERT(condition, "message")` |
| Initialization failures | `throw std::runtime_error(...)` |
| Runtime lookups (not found) | Return `nullptr` or `false` |
| Invalid handles | Null sentinel (index 0) |

No exceptions in hot paths (systems, ECS queries, rendering).

---

## Performance Conventions

- `reserve()` before loops that `push_back()`
- `clear()` to reuse container capacity across frames (not `= {}`)
- `memcpy` for bulk trivially-copyable transfers
- `thread_local` for per-thread scratch buffers
- Dense `SparseSet` iteration over random access
- Generational handles to prevent use-after-free
- Early-out/continue to avoid deep nesting

---

## Formatting Rules

- **Indent**: 4 spaces, no tabs
- **Braces**: K&R (opening brace on same line)
- **Access specifiers**: Indented 4 spaces from class
- **Member body**: Indented 8 spaces from class (4 from access specifier)
- **Spacing**: `if (`, `for (`, `while (` -- space after keyword. `fn()` -- no space before parens
- **Alignment**: Vertically align related declarations
- **Charset**: Strictly ASCII (no Unicode symbols in code or comments)
- **Namespace close**: Always `} // namespace Name`
