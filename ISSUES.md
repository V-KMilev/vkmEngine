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
| 6 | Deferred | No RenderTarget/Framebuffer abstraction | `render_backend.h` | Current engine renders to default framebuffer. Needed for shadow maps/post-processing/deferred — none planned for current milestone. vkmGL already has `Core::FrameBuffer` ready when needed. |
| 7 | Done | Keyboard input state race | `input_handle.h:72` | Not a bug - GLFW guarantees callbacks fire on the calling thread during `glfwPollEvents()`. Added documentation. |
| 8 | Deferred | EngineCore links vkmGL | `CMakeLists.txt:69` | `TextureAsset` inherits `Core::Texture2DParams` which uses `GLenum` types from vkmGL. Full decoupling requires engine-level texture format enums. Revisit when adding a second backend. |

---

## File Structure

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 9 | Done | Editor split across two include roots | `src/editor/` | All editor code (CameraController, EditorSystem, panels) consolidated in `src/editor/` under EngineEditor target. Separate include root by design — keeps editor boundary clear from engine core. |
| 10 | Done | CameraController in EngineCore | `src/editor/camera_controller.h` | Moved CameraController to EngineEditor target in `src/editor/`. No longer compiled into EngineCore. |

---

## CMake Architecture

| # | Status | Issue | Location | Description |
|---|--------|-------|----------|-------------|
| 11 | Done | Every file listed manually (125+) | `CMakeLists.txt` | Replaced 125+ manual file listings with `file(GLOB_RECURSE *.cpp)` per target. Added re-run note for GLOB_RECURSE limitation. |
| 12 | Done | Headers listed as sources | `CMakeLists.txt` | Removed all `.h` files from `add_library` calls. GLOB_RECURSE only collects `.cpp` files now. |
| 13 | Done | Duplicate APP_* compile definitions | Lines 270-276 vs 296-306 | Extracted shared definitions into `BuildInfo` INTERFACE target. EngineEditor and executable both link it. |
| 14 | Done | BackendOpenGL exposes 4 include dirs | `CMakeLists.txt:103` | Reduced to single public include dir `src/backend/opengl`. All cross-directory includes qualified (`core/`, `config/`, `resource/`, `pass/`). ~33 includes updated across 14 files. |
| 15 | Done | Warning flags commented out | Line 293 | Enabled `-Wall -Wextra` on all targets with targeted suppressions for submodule false positives. |
| 16 | Deferred | Single 300-line CMakeLists.txt | Root file | After GLOB_RECURSE, the root CMakeLists.txt is ~200 lines. At that size, splitting adds boilerplate without benefit. |
| 17 | Done | EngineRendering has no include dir | Lines 151-164 | Added explicit `target_include_directories(EngineRendering PUBLIC src/engine)`. |
| 18 | Deferred | No install/export targets | Entire file | No external project consumes vkmEngine as a CMake dependency. Zero value today. |
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
| 34 | Deferred | Single shader per forward pass | `gl_forward_pass.h:60` | Current PBR uber-shader handles all existing materials. Shader variants needed only when transparent/unlit materials are added. TODO already in code. |
| 35 | Deferred | No hierarchy world matrix caching | `hierarchy_utils.cpp:63` | Only ~48 parented entities in benchmark (~0.3%). Visibility system already caches computed world matrices per frame. Adding dirty-flag infrastructure to plain-data Transform would require change detection at every write site. Revisit when hierarchy count grows. |
| 36 | Deferred | Animation tracks AoS layout | `animation_track.h:166` | Tracks have ~10-20 keyframes. Binary search is ~4-5 comparisons. Cache effects negligible at this scale. SoA would only help at 100+ keyframes per track. |
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

| Category | Count | Done | Deferred |
|----------|-------|------|----------|
| Critical Bugs | 3 | 3 | 0 |
| High Priority | 5 | 3 (1 N/A) | 2 |
| File Structure | 2 | 2 | 0 |
| CMake | 10 | 8 | 2 |
| Code Style | 5 | 5 | 0 |
| API Consistency | 4 | 4 | 0 |
| Comments | 4 | 4 | 0 |
| Architecture | 6 | 3 | 3 |
| Dead Code | 1 | 1 | 0 |
| **Total** | **40** | **33** | **7** |
