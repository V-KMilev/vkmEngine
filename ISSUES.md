# vkmEngine - Issues & Improvement Tracker

Comprehensive audit of the codebase from the `vkm/dev/performance-overall` branch through the current UI work. Items are grouped by category and prioritized within each section.

---

## Critical Bugs

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 1 | Done | Stale EntityIds in visibility consumers | `animation_system.cpp:42`, `render_view.cpp:106` | Added `scene.isAlive(id)` guard in both AnimationSystem and RenderView. |
| 2 | Done | EventSystem push() IMMEDIATE deadlock | `event_system.cpp:26` | Moved IMMEDIATE event execution outside the lock in `push()`. |
| 3 | Done | Window size race condition | `window.h:76` | Not a bug - GLFW guarantees callbacks fire on the calling thread during `glfwPollEvents()`. Added documentation. |

---

## High Priority

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 4 | N/A | Visibility ignores cached model matrices | `visibility_system.cpp:167` | Current Transform has no caching, only static `computeModelMatrix()`. |
| 5 | Done | No hierarchy cycle detection | `hierarchy_utils.cpp:5` | Added ancestor-walk cycle detection with LOG_WARNING and depth-32 guard. |
| 6 | | No RenderTarget/Framebuffer abstraction | `render_backend.h` | `RenderBackend` has no concept of render-to-texture. This blocks shadow maps, post-processing, deferred rendering, and bloom. Add abstract `RenderTarget` interface now so future features don't require architecture rework. |
| 7 | Done | Keyboard input state race | `input_handle.h:72` | Not a bug - GLFW guarantees callbacks fire on the calling thread during `glfwPollEvents()`. Added documentation. |
| 8 | | EngineCore links vkmGL | `CMakeLists.txt:139` | Core engine should not depend on OpenGL. TODO already noted in the file. Move vkmGL dependency to BackendOpenGL only; expose GLM separately. |

---

## File Structure

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 9 | | Editor split across two include roots | `src/engine/editor/` + `src/editor/` | `CameraController` lives in `src/engine/editor/` (EngineCore), `EditorSystem` and panels live in `src/editor/` (EngineEditor). Different include domains. Includes like `#include "core/engine.h"` in `src/editor/` work only by accident through transitive deps. Move all editor code into `src/engine/editor/`. |
| 10 | | CameraController in EngineCore | `CMakeLists.txt:129-131` | Camera controller is editor-specific code living in the core engine library. Every app links it even without an editor. Move to EngineEditor. |

---

## CMake Architecture

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 11 | | Every file listed manually (125+) | Entire `CMakeLists.txt` | Adding a new `.cpp` means editing CMake. Error-prone and doesn't scale. Use `file(GLOB_RECURSE)` per library or `target_sources`. |
| 12 | | Headers listed as sources | Lines 62-131 | `.h` files in `add_library` are not compiled. Only helps IDE indexing. CMake 3.25+ has `FILE_SET HEADERS` for this. |
| 13 | Done | Duplicate APP_* compile definitions | Lines 270-276 vs 296-306 | Extracted shared definitions into `BuildInfo` INTERFACE target. EngineEditor and executable both link it. |
| 14 | | BackendOpenGL exposes 4 include dirs | Lines 204-210 | Every subdirectory of the backend is a public include path. Consumers can `#include "gl_mesh.h"` from anywhere, breaking encapsulation. Use single public include dir with qualified paths internally. |
| 15 | Done | Warning flags commented out | Line 293 | Enabled `-Wall -Wextra` on all targets with targeted suppressions for submodule false positives. |
| 16 | | Single 300-line CMakeLists.txt | Root file | Doesn't scale. Split into per-subdirectory `CMakeLists.txt` files (`src/engine/CMakeLists.txt`, `src/backend/opengl/CMakeLists.txt`, etc.). |
| 17 | Done | EngineRendering has no include dir | Lines 151-164 | Added explicit `target_include_directories(EngineRendering PUBLIC src/engine)`. |
| 18 | | No install/export targets | Entire file | No `install()` or `export()` rules. Can't use this engine as a CMake dependency from another project. Low priority but future-proofs the build. |
| 19 | Done | project() called after manual set() | Lines 4-21 | Replaced manual `set()` variables with `project(engine VERSION 0.0.1 ...)` directly. |
| 20 | Done | C++17 set globally, not per-target | Lines 10-12 | Removed global `CMAKE_CXX_STANDARD`. Set `CXX_STANDARD 17` per-target via `set_target_properties` in foreach. |

---

## Code Style Consistency

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 21 | Done | Remove all special/unicode symbols from code | Codebase-wide | Replaced all Unicode characters with ASCII equivalents and removed section dividers. |
| 22 | Done | Struct members lack `m_` prefix | `types.h:14`, `render_view.h:72`, `resource_handle.h:46` | Documented convention in STYLE_GUIDE.md: plain members for POD structs, `m_` for classes. |
| 23 | Done | Include ordering not grouped | `gl_view.h:1-15`, `gl_texture_mapping.h` | Fixed include grouping in 5 files: camera_controller.h, gl_view.h, gl_view.cpp, event_system.cpp, gl_instance_buffer.h. |
| 24 | Done | Access specifier reappears | `gl_view.h:178` | Fixed - merged duplicate public/private sections in gl_view.h. |
| 25 | Done | Struct with explicit `public:` | `resource_handle.h:17` | Fixed - removed redundant `public:` from struct Handle. |

---

## API Consistency

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 26 | Done | System constructors vary | `VisibilitySystem` vs `AnimationSystem` | Added explicit constructor, destructor, and copy/move delete to VisibilitySystem (was the only outlier). |
| 27 | Done | Getter naming mixed | `getScene()` vs `isLooking()` vs `count()` | Renamed `keyboard()`/`mouse()` to `getKeyboard()`/`getMouse()`, `globalVersion()` to `getGlobalVersion()`. Convention documented in STYLE_GUIDE.md. |
| 28 | Done | Error handling inconsistent | `Scene::get()` vs `GLView::getMesh()` | Convention documented in STYLE_GUIDE.md: asserts for preconditions, nullable returns for runtime lookups, exceptions for init failures, sentinels for invalid handles. |
| 29 | Done | `FrameContext.visibility` is raw pointer | `system.h:25` | Already documented in FrameContext doc block - non-owning pointer to persistent VisibilitySystem storage. |

---

## Comments & Documentation

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 30 | Done | ~50% of files lack Doxygen headers | Component files, backend resources | Convention: Doxygen on classes/functions only, no file-level headers needed. |
| 31 | Done | Mixed comment styles in same files | `gl_material.h` vs `animation.h` | Minor `///` vs `/** */` in private memory code only. Convention documented in STYLE_GUIDE.md. |
| 32 | Done | TODOs scattered, not centralized | `transform.h:9`, `gl_forward_pass.h:60`, `window.h:10`, `print_helper.h:7`, `occlusion_culler.h:19` | Standardized all 8 TODOs to `// TODO(vkm):` format. |
| 33 | Done | Section dividers inconsistent | Editor files use banners, rest don't | Removed all section divider comments from editor files. |

---

## Architecture Gaps

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 34 | | Single shader per forward pass | `gl_forward_pass.h:60` | All materials use the same shader. Can't support transparent, unlit, or emissive materials without shader variants. Move shader selection to material or add variant dispatch in forward pass. |
| 35 | Deferred | No hierarchy world matrix caching | `hierarchy_utils.cpp:63` | Only ~48 parented entities in benchmark (~0.3%). Visibility system already caches computed world matrices per frame. Adding dirty-flag infrastructure to plain-data Transform would require change detection at every write site. Revisit when hierarchy count grows. |
| 36 | | Animation tracks AoS layout | `animation_track.h:166` | Keyframes stored as `vector<Keyframe<T>>` (time + value interleaved). For ~2,750 animated entities, SoA layout (separate `times[]` and `values[]`) would improve cache locality and enable SIMD. |
| 37 | Done | Visibility results AoS | `visibility.h:20-21` | Replaced parallel vectors with combined `VisibleEntity { EntityId, mat4 }` struct. Single contiguous vector improves cache locality and simplifies worker merge logic. |
| 38 | Done | AABB rotation "bug" | `gl_aabb_debug_pass.cpp` | Not a bug. AABBs grow under rotation because Arvo's method computes the AABB of the rotated bounding box, not the geometry. The 8-corner method produces identical results. Expected and correct for conservative culling. |
| 39 | Done | Bounds validation epsilon too small | `bounds_utils.h:12` | Replaced `glm::epsilon<float>()` with `BOUNDS_EPSILON_SQ = 1e-8f`. |

---

## Dead Code

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 40 | Done | GLNavigationGizmoPass unused | `gl_navigation_gizmo_pass.h/cpp` | Replaced by ImGui `drawNavigationGizmo()`. Removed files and gizmo shaders. |

---

## Summary

| Category | Count | Done |
|----------|-------|------|
| Critical Bugs | 3 | 3 |
| High Priority | 5 | 3 (1 N/A) |
| File Structure | 2 | 0 |
| CMake | 10 | 5 |
| Code Style | 5 | 5 |
| API Consistency | 4 | 4 |
| Comments | 4 | 4 |
| Architecture | 6 | 3 (1 deferred) |
| Dead Code | 1 | 1 |
| **Total** | **40** | **30** |
