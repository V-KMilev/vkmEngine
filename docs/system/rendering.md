# Rendering System

The rendering system is split into an abstract engine-level pipeline and a concrete OpenGL backend.

## Key Files

- `src/engine/system/render/render_system.h` -- RenderSystem (System subclass)
- `src/engine/system/render/render_view.h` -- RenderView (frame snapshot)
- `src/engine/system/render/render_pipeline.h` -- RenderPipeline (pass list)
- `src/engine/system/render/render_pass.h` -- RenderPass (abstract pass)
- `src/engine/system/render/render_backend.h` -- RenderBackend (abstract backend)
- `src/engine/system/render/render_target.h` -- RenderTarget (abstract framebuffer)
- `src/backend/opengl/` -- OpenGL implementation

## Pipeline Overview

```
RenderSystem::update(FrameContext)
  |-- RenderView::build(scene, resources, visibility)
  |     |-- Copy camera data from Visibility
  |     |-- Gather DrawableData from visible entities
  |     |-- Sort drawables by (materialType, material, mesh)
  |     |-- Gather LightData from scene
  |-- RenderPipeline::execute(backend, view, resources)
        |-- ForwardPass::execute(...)
        |-- GridPass::execute(...)
        |-- GizmoPass::execute(...)
```

## RenderSystem

Inherits `System`. Owns a `RenderBackend`, `RenderPipeline`, and `RenderView`.

```cpp
auto& renderSystem = engine.addSystem<RenderSystem>();
renderSystem.setBackend(std::make_unique<GLBackend>(context));
renderSystem.addPass(std::make_unique<GLForwardPass>(pbrShader));
renderSystem.addPass(std::make_unique<GLGridPass>(gridShader));
```

On each `update()`:
1. Checks for viewport resize, calls `resize()` if needed
2. Builds `RenderView` from scene + visibility data
3. Executes all passes in the pipeline

## RenderView

A per-frame snapshot containing everything passes need to render:

| Field | Type | Description |
|-------|------|-------------|
| `camera` | `CameraData` | view, projection, viewProjection matrices + position |
| `drawables` | `vector<DrawableData>` | Sorted by (materialType, material, mesh) for batching |
| `lights` | `vector<LightData>` | All enabled lights with world-space transforms |
| `viewportWidth/Height` | `uint32_t` | Current viewport dimensions |

Drawables are sorted using a two-phase key-index sort to minimize swaps of the 88-byte `DrawableData` structs.

## RenderBackend

Abstract interface. Backends implement `resize()` and provide a default `RenderTarget`.

```cpp
enum class RenderBackendType { NONE, OpenGL, Optix, CPU };
```

## RenderPass

Abstract base for individual rendering stages. Each pass has a name, enabled flag, and implements:

- `onResize(backend, width, height)` -- React to viewport changes
- `execute(backend, view, resources)` -- Perform GPU work

## RenderTarget

Abstract framebuffer interface with `bind()`, `unbind()`, `resize()`.

## OpenGL Backend

### GLBackend

Concrete `RenderBackend`. Owns `Core::Context` (GLEW/GLFW state) and `GLView` (GPU resource manager). Sets up initial GL state (dark gray clear, no face culling).

### GLView

GPU resource synchronizer. Maps CPU handles to GPU objects with version tracking:

- `sync(MeshHandle, MeshAsset)` -- Create/update GLMesh if version changed
- `sync(MaterialHandle, MaterialAsset)` -- Create/update GLMaterial
- `sync(TextureHandle, TextureAsset)` -- Create/update GLTexture

Also owns `GLLights` (UBO) and `GLInstanceBatcher`.

### GLInstanceBatcher

Groups sorted drawables into instance batches. A single pass over the sorted `DrawableData` array detects batch boundaries (same mesh + material). Each batch gets a `GLInstanceBuffer` with per-instance model matrices.

### GPU Resources

| Class | Description |
|-------|-------------|
| `GLMesh` | VAO + VBO + IBO. Vertex layout: pos(3f), normal(3f), uv(2f), tangent(4f) |
| `GLMaterial` | UBO (144 bytes, std140) + texture bindings. 11-bit MaterialTextureFlags |
| `GLTexture` | Wraps `Core::Texture2D` |
| `GLLights` | UBO with up to 32 lights (2064 bytes total) |
| `GLInstanceBuffer` | Per-instance mat4 buffer. Growth factor 1.5x, min capacity 64 |

### Render Passes

| Pass | Shader | Description |
|------|--------|-------------|
| `GLForwardPass` | `shaders/pbr` | PBR instanced rendering. Syncs resources, binds UBOs, draws instanced batches |
| `GLAABBDebugPass` | `shaders/aabb_debug` | Wireframe AABB debug visualization (GL_LINES) |
| `GLGridPass` | `shaders/grid` | Procedural infinite grid with distance fade + alpha blending |
| `GLNavigationGizmoPass` | `shaders/gizmo` | Screen-space axis gizmo (orthographic, RGB = XYZ) |

### Shader Interface

**Vertex attributes:**
- 0-3: Per-vertex (position, normal, uv, tangent)
- 4-7: Per-instance (mat4 model matrix columns, divisor=1)

**UBO bindings:**
- 0: MaterialBlock (144 bytes)
- 1: LightsBlock (2064 bytes: int lightCount + LightGPUData[32])

**Texture slots:**
| Slot | Texture |
|------|---------|
| 0 | Albedo |
| 1 | Normal |
| 2 | MetallicRoughness |
| 3 | AO |
| 4 | Emission |
| 5 | Height |
| 6 | Clearcoat |
| 7 | Transmission |
| 8 | Metallic |
| 9 | Roughness |
