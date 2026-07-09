# Graphics and Rendering

This document covers the entire graphics subsystem: the DirectX 12 fundamentals, the Render Hardware Interface (RHI) abstraction, the render graph, mesh and material resources, model loading, the camera system, forward lighting (PBR, plus the retired Blinn-Phong model for reference), shadow mapping, transparency, and analytic environment lighting (IBL).

> **Updated for ADR-0010 (2026-07-09).** The renderer now uses a multi-model
> draw-submission API (`load_model` → `ModelId`, per-frame `submit()`), a
> per-frame constant-buffer upload ring, and a generalized `BindingLayout`
> instead of hardcoded root-signature variants. The dedicated Blinn-Phong
> model pipeline (`model_vs/ps.hlsl`) was retired — PBR is the only model
> path. The Blinn-Phong theory section is kept as study material and marked
> as such. See `docs/decisions/0010-renderer-submission-and-binding-layout.md`.

## Table of Contents

- [DirectX 12 Fundamentals](#directx-12-fundamentals)
  - [What is DirectX 12?](#what-is-directx-12)
  - [Key DX12 Concepts](#key-dx12-concepts)
  - [The Frame Lifecycle](#the-frame-lifecycle)
  - [Resource Barriers and State Tracking](#resource-barriers-and-state-tracking)
  - [Descriptor Heaps and Binding](#descriptor-heaps-and-binding)
  - [Root Signatures](#root-signatures)
  - [Pipeline State Objects (PSOs)](#pipeline-state-objects-psos)
  - [Debug Layer, GPU Validation, and DRED](#debug-layer-gpu-validation-and-dred)
- [The RHI Abstraction (`rhi.h`)](#the-rhi-abstraction-rhih)
  - [Why an RHI?](#why-an-rhi)
  - [Handle-Based Resource Management](#handle-based-resource-management)
  - [RhiDevice API Overview](#rhidevice-api-overview)
  - [The RHI Type System (`rhi_types.h`)](#the-rhi-type-system-rhi_typesh)
- [Render Graph (`render_graph.h`)](#render-graph-render_graphh)
  - [What is a Render Graph?](#what-is-a-render-graph)
  - [How the Render Graph Works](#how-the-render-graph-works)
  - [Barrier Compilation Algorithm](#barrier-compilation-algorithm)
  - [The Command Stream](#the-command-stream)
- [Mesh and Material Resources](#mesh-and-material-resources)
  - [Vertex Format](#vertex-format)
  - [Mesh and SubMesh](#mesh-and-submesh)
  - [Materials](#materials)
- [Model Loading (`model_loader.h`)](#model-loading-model_loaderh)
  - [The glTF Format](#the-gltf-format)
  - [The Loading Pipeline](#the-loading-pipeline)
  - [Scene-Graph Baking and Instancing](#scene-graph-baking-and-instancing)
  - [Fallbacks for Missing Attributes](#fallbacks-for-missing-attributes)
  - [Material Feature Coverage](#material-feature-coverage)
- [Camera System (`camera.h`)](#camera-system-camerah)
  - [Camera Representation](#camera-representation)
  - [Fly-Through Controller](#fly-through-controller)
- [Forward Lighting with Blinn-Phong](#forward-lighting-with-blinn-phong)
  - [What is Forward Rendering?](#what-is-forward-rendering)
  - [The Blinn-Phong Lighting Model](#the-blinn-phong-lighting-model)
  - [Light Types](#light-types)
  - [Constant Buffer Layout](#constant-buffer-layout)
  - [The Vertex Shader](#the-vertex-shader)
  - [The Pixel Shader](#the-pixel-shader)
- [Shadow Mapping](#shadow-mapping)
  - [How Shadow Mapping Works](#how-shadow-mapping-works)
  - [The Shadow Pass](#the-shadow-pass)
  - [Shadow Sampling with PCF](#shadow-sampling-with-pcf)
  - [Shadow Acne and Depth Bias](#shadow-acne-and-depth-bias)
- [PBR Materials](#pbr-materials)
  - [Why PBR?](#why-pbr)
  - [The Metallic-Roughness Workflow](#the-metallic-roughness-workflow)
  - [Cook-Torrance BRDF](#cook-torrance-brdf)
  - [PBR Root Signature](#pbr-root-signature)
  - [Per-Material Constant Buffer](#per-material-constant-buffer)
  - [The PBR Vertex Shader](#the-pbr-vertex-shader)
  - [The PBR Pixel Shader](#the-pbr-pixel-shader)
  - [Analytic Environment Lighting (IBL)](#analytic-environment-lighting-ibl)
  - [Tone Mapping and Gamma Correction](#tone-mapping-and-gamma-correction)
  - [Draw Submission API](#draw-submission-api)
- [Transparency](#transparency)
  - [Alpha Modes](#alpha-modes)
  - [Blend State and PSO Variants](#blend-state-and-pso-variants)
  - [Back-to-Front Sorting](#back-to-front-sorting)
  - [Double-Sided Materials](#double-sided-materials)

---

## DirectX 12 Fundamentals

### What is DirectX 12?

DirectX 12 is Microsoft's low-level graphics API. Unlike its predecessor DirectX 11 (which managed many things automatically), DX12 gives the application explicit control over GPU memory, command submission, synchronization, and resource state. This explicit control enables higher performance but requires the application to manage complexity that the driver previously handled.

Think of it like this: DX11 is an automatic transmission — easier to drive but limited in what you can optimize. DX12 is a manual transmission — more work, but you can extract maximum performance when you know what you are doing.

### Key DX12 Concepts

**Device (`ID3D12Device`):** The application's connection to the GPU. Used to create resources, command lists, heaps, and pipeline state objects. Not used for drawing — drawing happens through command lists.

**Command Queue (`ID3D12CommandQueue`):** A GPU-side queue of work. The CPU submits command lists to the queue, and the GPU processes them in order. MyEngine uses a single "direct" queue (capable of graphics, compute, and copy operations).

**Command Allocator (`ID3D12CommandAllocator`):** Backing memory for recorded commands. Each frame slot has its own allocator, so the GPU can read commands from one allocator while the CPU records new commands into another.

**Command List (`ID3D12GraphicsCommandList`):** A sequence of GPU commands (draw calls, state changes, resource transitions). Commands are recorded by the CPU, then submitted to the command queue for execution.

**Swap Chain (`IDXGISwapChain`):** Manages the back buffers that are presented to the display. MyEngine uses a "flip-model" swap chain with 2 back buffers (double buffering). The GPU renders to one buffer while the display shows the other.

**Fence (`ID3D12Fence`):** A synchronization primitive between CPU and GPU. After submitting commands, the CPU signals the fence with a value. The CPU can later wait until the GPU reaches that value, ensuring the GPU has finished processing those commands.

### The Frame Lifecycle

MyEngine uses double buffering (`kRhiFrameCount = 2`). Each frame follows this pattern:

```
Frame N begins:
  1. Wait for frame slot N's fence value
     (ensures the GPU finished frame N-2, freeing this slot's resources)
  2. Reset frame slot N's command allocator
  3. Reset the command list against that allocator
  4. Barrier: back buffer N transitions PRESENT → RENDER_TARGET
  5. Clear the back buffer and depth buffer
  6. Record draw commands (set pipeline, bind resources, draw)
  7. Barrier: back buffer N transitions RENDER_TARGET → PRESENT
  8. Close the command list
  9. Submit the command list to the command queue
  10. Present (swap buffers)
  11. Signal the fence with frame N's value
```

The fence ensures that the CPU never overwrites a command allocator that the GPU is still reading from. With 2 frame slots, up to 2 frames can be "in flight" simultaneously: one being rendered by the GPU, one being recorded by the CPU.

### Resource Barriers and State Tracking

DX12 resources (textures, buffers) have explicit states. A texture might be in the `RENDER_TARGET` state (writable by the GPU as a render target) or the `PIXEL_SHADER_RESOURCE` state (readable by pixel shaders). The application must insert barriers to transition between states:

```cpp
rhi.barrier(back_buffer, ResourceState::Present, ResourceState::RenderTarget);
// ... draw ...
rhi.barrier(back_buffer, ResourceState::RenderTarget, ResourceState::Present);
```

If a resource is used in the wrong state, the debug layer reports an error, and the GPU may produce corrupted output or crash. In MyEngine, the render graph automates barrier insertion (see the Render Graph section).

### Descriptor Heaps and Binding

In DX12, shaders access resources (textures, constant buffers, samplers) through "descriptors" — small data structures stored in "descriptor heaps." A descriptor is like a typed pointer that tells the GPU where a resource lives and how to interpret it.

There are several descriptor types:
- **CBV** (Constant Buffer View): points to a constant buffer.
- **SRV** (Shader Resource View): points to a texture or buffer readable by shaders.
- **UAV** (Unordered Access View): points to a read/write resource (for compute shaders).
- **RTV** (Render Target View): describes a render target for output.
- **DSV** (Depth-Stencil View): describes a depth buffer.
- **Sampler**: configures texture filtering (point, linear, anisotropic) and addressing (wrap, clamp, mirror).

MyEngine uses a **shader-visible** CBV/SRV/UAV heap (4 MB, arena-backed) for resource binding, and **non-shader-visible** heaps for RTVs, DSVs, and samplers.

### Root Signatures

A root signature defines how resources are bound to shader stages. It is like a function signature for the shader: it declares what parameters (constant buffers, textures, samplers) the shader expects and where to find them.

Since ADR-0010 the RHI no longer hardcodes renderer-specific layouts. `PipelineDesc::bindings` carries a **`BindingLayout`** — just two counts — and the RHI derives the root signature from them generically:

```cpp
struct BindingLayout {
    u32 cbv_count       = 2;  // root CBVs b0..b(N-1), bound at slots [0, N)
    u32 srv_table_count = 2;  // single-SRV tables t0..t(M-1), at slots [N, N+M)
};
```

The slot convention is fixed: root parameters `[0, cbv_count)` are root CBVs for registers `b0..bN-1` (bind with `set_constant_buffer(slot, …)`), and parameters `[cbv_count, cbv_count + srv_table_count)` are single-SRV descriptor tables for registers `t0..tM-1` (bind with `set_texture(slot, …)`). Identical layouts (including static-sampler state) share one root signature through an internal **dedupe cache**, so e.g. all four PBR PSO variants reference a single root signature object.

The two layouts currently in use:

**`{2, 2}`** — cube and shadow pipelines:

| Slot | Type | Register | Content |
|------|------|----------|---------|
| 0 | Root CBV | b0 | Per-frame data (MVP for the cube; light view-proj for shadow) |
| 1 | Root CBV | b1 | Per-object data (model matrix, normal matrix) |
| 2 | Descriptor table | t0 | Albedo texture (cube) |
| 3 | Descriptor table | t1 | — (unused) |

**`{3, 6}`** — the PBR pipeline:

| Slot | Type | Register | Content |
|------|------|----------|---------|
| 0 | Root CBV | b0 | Per-frame (view, proj, camera, lights, shadow VP) |
| 1 | Root CBV | b1 | Per-object (model matrix, normal matrix) |
| 2 | Root CBV | b2 | Per-material (base color, metallic, roughness, emissive, alpha mode/cutoff, texture flags, per-map UV transforms) |
| 3 | Descriptor table | t0 | Base color texture (sRGB) |
| 4 | Descriptor table | t1 | Metallic-roughness texture |
| 5 | Descriptor table | t2 | Normal map (tangent-space) |
| 6 | Descriptor table | t3 | Occlusion texture |
| 7 | Descriptor table | t4 | Emissive texture |
| 8 | Descriptor table | t5 | Shadow map (comparison depth) |

Every root signature carries the same two static samplers:
- **s0:** Sampler for material textures. Both the **filter mode** (point / linear / anisotropic) and the **address mode** (wrap / clamp / mirror) are configurable per pipeline via `PipelineDesc::sampler_filter` and `PipelineDesc::sampler_address`. The PBR path builds a PSO block **lazily per address mode** the first time a model needs it, so models with different glTF wrap modes coexist.
- **s1:** Comparison sampler (for shadow map PCF sampling).

A **root CBV** is a constant buffer bound directly by GPU virtual address — no descriptor needed, so it is slightly faster than going through a descriptor table. Since ADR-0010 a root CBV can be bound at a **256-byte-aligned offset** into a buffer (`set_constant_buffer(slot, buffer, offset)`), which is what the per-frame upload ring uses to give every draw its own constants from one big buffer.

### Pipeline State Objects (PSOs)

A PSO bundles all GPU state needed for rendering: vertex and pixel shaders, input layout, rasterizer state (cull mode, fill mode), depth-stencil state, blend state, and render target formats. Setting a PSO is a single GPU command that configures everything at once.

DX12 requires PSOs to be created up front (not modified on the fly). This is a departure from DX11, where individual states could be changed independently. The benefit is that the GPU driver can pre-compile and optimize the entire pipeline configuration at creation time, avoiding runtime overhead.

MyEngine's pipelines:
- **Cube pipeline** (created at init): Vertex + pixel shader for the spinning textured cube — the empty-scene fallback and golden-image baseline — no culling.
- **Shadow pipeline** (created at init): Vertex shader only (no pixel shader — depth-only rendering), back-face culling, depth bias enabled.
- **PBR pipelines** (built lazily): Vertex + pixel shader for Cook-Torrance BRDF rendering (`pbr_vs.hlsl` / `pbr_ps.hlsl`), depth test/write enabled, linear texture sampling, `BindingLayout{3, 6}`. Because cull mode and blend state are baked into the PSO, the renderer builds a **2×2 block of variants** — `{cull Back, cull None} × {Opaque, AlphaBlend}` — and selects one per sub-mesh from the material's `double_sided` and `alpha_mode`. One block exists **per static-sampler address mode** (wrap/clamp/mirror), built the first time a loaded model needs that mode. See [Transparency](#transparency).

(The Phase 3 Blinn-Phong "model pipeline" was retired by ADR-0010; PBR is the only model path.)

### Debug Layer, GPU Validation, and DRED

In Debug builds, MyEngine enables three DX12 debugging features:

1. **Debug Layer (`ID3D12Debug`):** Validates API usage (correct state transitions, valid arguments, resource lifetime) and reports errors to the info queue.

2. **GPU-Based Validation:** Catches errors that only manifest on the GPU (shader bugs, out-of-bounds resource access). Slower than CPU validation but catches more issues.

3. **DRED (Device Removed Extended Data):** When the GPU crashes ("device removed"), DRED provides breadcrumb data showing which GPU operations completed before the crash. Without DRED, a device-removed error gives no information about what went wrong.

The debug layer also sets `SetBreakOnSeverity` for CORRUPTION (always) and ERROR (only when a debugger is attached via `IsDebuggerPresent()`). Without the debugger check, the INT3 breakpoint that fires on an error would propagate as an unhandled SEH exception in headless CTest runs, crashing the test before it can log the error message. The smoke tests check that `debug_error_count() == 0` after running, ensuring the renderer produces zero DX12 validation errors.

---

## The RHI Abstraction (`rhi.h`)

### Why an RHI?

Phase 1 implemented everything directly against DX12 COM objects in the renderer source file. Phase 3 introduced the RHI (Render Hardware Interface) to separate the "what to do" from the "how to do it on DX12":

- **Clean separation of concerns.** The `Renderer` describes what to render (set camera, submit models, shadow pass). The `RhiDevice` executes the low-level GPU commands.
- **Testability.** The render graph can be unit-tested against the RHI's handle-based API without creating a real GPU device.
- **Future backend support.** If a Vulkan backend is added, only `rhi_dx12.cpp` would be replaced — the `Renderer` and render graph would not change.

Since ADR-0010 this separation is complete in practice: the high-level renderer lives in `renderer.cpp` (renamed from `renderer_dx12.cpp`) and contains **no D3D12 or Win32 usage at all** — `rhi_dx12.cpp` is the only file in `ge_gfx` that includes `<windows.h>`/`<d3d12.h>`.

### Handle-Based Resource Management

The RHI identifies GPU resources by opaque handles (from the core `Handle<Tag>` system), not raw pointers or COM interfaces:

```cpp
struct BufferTag {};   using BufferHandle   = Handle<BufferTag>;
struct TextureTag {};  using TextureHandle  = Handle<TextureTag>;
struct ShaderTag {};   using ShaderHandle   = Handle<ShaderTag>;
struct PipelineTag {}; using PipelineHandle = Handle<PipelineTag>;
```

The DX12 backend stores the actual COM objects (`ID3D12Resource*`, `ID3D12PipelineState*`, etc.) in `HandleTable`s inside its pImpl struct. When user code calls `rhi.set_texture(0, tex_handle)`, the RHI looks up the handle in its texture table to find the underlying DX12 descriptor, then binds it.

Benefits:
- No DX12 types leak into public headers.
- Handles are safe to store, copy, and compare.
- Stale handles (to destroyed resources) are caught by the generation check.

### RhiDevice API Overview

The `RhiDevice` provides three categories of methods:

**Resource lifecycle:**
```cpp
BufferHandle   create_buffer(const BufferDesc&);
TextureHandle  create_texture(const TextureDesc&);
ShaderHandle   create_shader(const ShaderDesc&);
PipelineHandle create_pipeline(const PipelineDesc&);
void           destroy_buffer/texture/shader/pipeline(Handle);
```

**Command recording** (between `begin_frame()` and `end_frame()`):
```cpp
void barrier(TextureHandle, ResourceState before, ResourceState after);
void set_render_targets(const TextureHandle* rts, u32 count, TextureHandle depth);
void clear_render_target(TextureHandle rt, const f32 color[4]);
void clear_depth(TextureHandle ds, f32 depth);
void set_pipeline(PipelineHandle);
void set_vertex_buffer(BufferHandle, u32 stride);
void set_index_buffer(BufferHandle, Format);
void set_constant_buffer(u32 slot, BufferHandle, u32 offset = 0); // offset: 256-aligned
void set_texture(u32 slot, TextureHandle);
void draw_indexed(u32 index_count, u32 start_index, i32 base_vertex);
```

Invalid or stale handles trip a `GE_ASSERT` in command recording (and the call is skipped gracefully in release) — a silently dropped draw is much harder to diagnose than an assert.

**Frame management:**
```cpp
void begin_frame();        // Wait for frame slot's fence, reset allocator/list
void end_frame();          // Close and submit command list
void present(bool vsync);  // Swap chain present + signal fence
u32  frame_index();        // Current frame slot in [0, kRhiFrameCount) —
                           // the only sanctioned way to pick per-frame resources
void wait_idle();          // Block until the GPU is idle (before destroying
                           // resources that in-flight frames may reference)
```

**Uploads and heaps.** Buffers created with `cpu_visible = false` live on the **DEFAULT heap** (VRAM): their `initial_data` is staged through a temporary upload buffer and copied on a **dedicated upload command list**, so static vertex/index data is read from VRAM every frame instead of streaming across PCIe from system memory. The same upload list handles texture uploads and back-buffer readback — resource creation never touches the frame command list, so it is safe mid-frame. CPU-visible buffers (constant buffers, the upload ring) stay on the UPLOAD heap and are written through `map_buffer()`.

### The RHI Type System (`rhi_types.h`)

All RHI types are plain data — no OS or DX12 headers leak through. This file defines:

- **Format enum:** Pixel/vertex element formats (`R8G8B8A8_UNORM`, `D32_FLOAT`, etc.).
- **Usage enums:** How a buffer or texture will be used (`Vertex`, `Index`, `Constant`, `ShaderResource`, `DepthStencil`, etc.).
- **ResourceState enum:** For barrier transitions (`RenderTarget`, `PixelShaderResource`, `DepthWrite`, `Present`, etc.).
- **State enums:** `CullMode`, `FillMode`, `CompareFunc`, `FilterMode`, `AddressMode` (wrap/clamp/mirror), and `BlendMode` (`Opaque`, `AlphaBlend`).
- **`BindingLayout`:** the CBV / SRV-table counts a pipeline's root signature is derived from (see [Root Signatures](#root-signatures)).
- **Descriptor structs:** `BufferDesc`, `TextureDesc`, `ShaderDesc`, `PipelineDesc` — all the parameters needed to create a resource. `PipelineDesc` carries `blend_mode`, `sampler_address`, and `bindings` alongside the shader/state fields.
- **`kRhiFrameCount`:** the number of frames in flight (2), shared by the RHI and any caller keeping per-frame CPU-writable resources (like the renderer's upload ring).

---

## Render Graph (`render_graph.h`)

### What is a Render Graph?

A render graph (also called a "frame graph") is an abstraction that describes the rendering operations for a single frame as a sequence of passes, each declaring which resources it reads and writes. The graph then **automatically inserts resource barriers** between passes where state transitions are needed.

Without a render graph, the programmer must manually track every resource's current state and insert barriers at the right places. This is error-prone (missing a barrier causes GPU corruption) and hard to maintain as the number of passes grows.

### How the Render Graph Works

Each frame, the renderer builds the graph:

```cpp
RenderGraph graph;
graph.begin();

// Declare initial resource states
graph.import(back_buffer, ResourceState::Present);
graph.import(shadow_map,  ResourceState::PixelShaderResource);

// Shadow pass: writes to the shadow map as a depth target
graph.add_pass("shadow", shadow_pass_fn, &shadow_data);
graph.pass_write(shadow_map, ResourceState::DepthWrite);

// Main pass: reads the shadow map, writes to the back buffer
graph.add_pass("main", main_pass_fn, &main_data);
graph.pass_read(shadow_map, ResourceState::PixelShaderResource);
graph.pass_write(back_buffer, ResourceState::RenderTarget);

// Back buffer must end in Present state for the swap chain
graph.set_final_state(back_buffer, ResourceState::Present);

graph.compile();   // Generates barriers
graph.execute(rhi); // Executes passes with barriers interleaved
```

### Barrier Compilation Algorithm

The `compile()` method walks passes in order and maintains a "tracked state" for each resource. For each resource access in each pass:

1. Look up the resource's current tracked state.
2. If the current state differs from the required state, emit a barrier.
3. Update the tracked state.

After all passes, check final-state requirements and emit any remaining barriers (e.g., back buffer must transition from RenderTarget to Present).

```
Pass "shadow":
  shadow_map: PixelShaderResource → DepthWrite  →  BARRIER emitted
  (shadow_map tracked state updated to DepthWrite)

Pass "main":
  shadow_map: DepthWrite → PixelShaderResource  →  BARRIER emitted
  back_buffer: Present → RenderTarget            →  BARRIER emitted
  (tracked states updated)

Final states:
  back_buffer: RenderTarget → Present            →  BARRIER emitted
```

The compiled output is a sequence of **commands**: either a barrier or a pass execution callback. The `execute()` method replays this sequence, calling `rhi.barrier()` for barriers and the pass callbacks for passes.

### The Command Stream

Internally, the graph stores compiled commands as a tagged union array:

```cpp
struct Command {
    enum Type : u8 { CmdBarrier, CmdPass };
    Type type;
    union {
        struct { u32 barrier_index; } barrier;
        struct { u32 pass_index; }    pass;
    };
};
```

Execution is a simple linear walk through this array, dispatching each command to either a barrier call or a pass callback. This is efficient and deterministic.

---

## Mesh and Material Resources

### Vertex Format

The engine uses a single vertex format for all lit geometry:

```cpp
struct LitVertex {
    f32 position[3];             // 12 bytes — object-space position           (offset 0)
    f32 normal[3];               // 12 bytes — unit surface normal             (offset 12)
    f32 uv[2];                   //  8 bytes — texture coordinates, set 0      (offset 24)
    f32 uv1[2]   = {0,0};        //  8 bytes — texture coordinates, set 1      (offset 32)
    f32 color[4] = {1,1,1,1};    // 16 bytes — vertex colour (glTF COLOR_0)    (offset 40)
    f32 tangent[4];              // 16 bytes — xyz = tangent, w = bitangent sign (offset 56)
};
// Total stride: 72 bytes
```

Two UV sets are carried so a material can reference either `TEXCOORD_0` or `TEXCOORD_1` per texture (common for ambient-occlusion / lightmap channels). `color` holds the optional glTF `COLOR_0` vertex colour, defaulting to white so it is a harmless multiply when absent.

The `tangent.w` component encodes the **bitangent sign**: the bitangent is reconstructed in the shader as `cross(normal, tangent.xyz) * tangent.w`. This is the convention used by glTF 2.0 and avoids storing the redundant third vector.

The depth-only shadow pipeline declares only the first three elements in its input layout (`position`/`normal`/`uv` at offsets 0/12/24) — those offsets are deliberately fixed so growing the vertex did not disturb them. Only the PBR pipeline declares the full layout:
- `POSITION`  : `R32G32B32_FLOAT`   at offset 0
- `NORMAL`    : `R32G32B32_FLOAT`   at offset 12
- `TEXCOORD0` : `R32G32_FLOAT`      at offset 24
- `TEXCOORD1` : `R32G32_FLOAT`      at offset 32
- `COLOR`     : `R32G32B32A32_FLOAT`at offset 40
- `TANGENT`   : `R32G32B32A32_FLOAT`at offset 56

### Mesh and SubMesh

A `Mesh` owns shared GPU vertex and index buffers. A `SubMesh` describes a range within those buffers:

```
Vertex Buffer: [v0 v1 v2 v3 v4 v5 v6 v7 v8 ...]
Index Buffer:  [0 1 2  2 1 3  4 5 6  6 5 7 ...]

SubMesh 0: index_offset=0,  index_count=6,  base_vertex=0,  material=0
SubMesh 1: index_offset=6,  index_count=6,  base_vertex=4,  material=1
```

This design packs an entire model into two GPU buffers (one vertex, one index), minimizing buffer-binding changes during rendering. Each submesh can reference a different material. Each `SubMesh` also stores an object-space **`centroid[3]`**, computed at load time from its baked vertex positions, which the renderer uses to sort transparent sub-meshes back-to-front (see [Back-to-Front Sorting](#back-to-front-sorting)).

### Materials

`Material` stores the full glTF 2.0 PBR metallic-roughness surface description:

```cpp
enum class AlphaMode : u8 { Opaque, Mask, Blend };

// KHR_texture_transform + UV-set selector, stored per texture map.
struct TexTransform {
    f32 offset[2] = {0,0};
    f32 scale[2]  = {1,1};
    f32 rotation  = 0.0f;   // radians
    u32 uv_set    = 0;      // 0 → TEXCOORD_0, 1 → TEXCOORD_1
};

struct Material {
    // Base colour (albedo)
    TextureHandle albedo_texture;
    f32           base_color[4] = {1,1,1,1};   // RGBA multiplier
    bool          has_albedo_texture = false;
    TexTransform  albedo_xform;

    // Metallic-roughness
    TextureHandle metallic_roughness_texture;   // G = roughness, B = metallic
    f32           metallic_factor  = 1.0f;
    f32           roughness_factor = 1.0f;
    bool          has_metallic_roughness_texture = false;
    TexTransform  mr_xform;

    // Tangent-space normal map
    TextureHandle normal_texture;
    bool          has_normal_texture = false;
    f32           normal_scale = 1.0f;          // glTF normalTexture.scale
    TexTransform  normal_xform;

    // Ambient occlusion
    TextureHandle occlusion_texture;            // R channel
    bool          has_occlusion_texture = false;
    f32           occlusion_strength = 1.0f;    // glTF occlusionTexture.strength
    TexTransform  occlusion_xform;

    // Emissive (KHR_materials_emissive_strength folded into the factor)
    TextureHandle emissive_texture;
    f32           emissive_factor[3] = {0,0,0}; // Linear emissive scale
    bool          has_emissive_texture = false;
    TexTransform  emissive_xform;

    // Transparency / culling
    AlphaMode     alpha_mode   = AlphaMode::Opaque;
    f32           alpha_cutoff = 0.5f;          // threshold for AlphaMode::Mask
    bool          double_sided = false;
};
```

Each texture slot is optional. When a texture is absent, the scalar factor (`base_color`, `metallic_factor`, etc.) is used directly, and the shader's texture-presence flag is `false`. At runtime the renderer substitutes default 1×1 textures (white for colour/metallic/roughness/occlusion, flat-normal `{128,128,255}` for the normal map) so all 6 texture slots in the PBR root signature are always bound with valid descriptors.

Each map additionally carries a `TexTransform`: a glTF `KHR_texture_transform` (UV offset/rotation/scale) plus a `uv_set` selector that chooses between the vertex's two UV channels. `normal_scale`, `occlusion_strength`, and the emissive strength are read from the glTF material and applied in the pixel shader. `alpha_mode`, `alpha_cutoff`, and `double_sided` drive the transparency path (see [Transparency](#transparency)).

---

## Model Loading (`model_loader.h`)

### The glTF Format

glTF (GL Transmission Format) is an open standard for 3D models. It stores:
- **Meshes:** vertex positions, normals, UVs, indices.
- **Materials:** base color, textures, PBR parameters.
- **Images:** embedded (in GLB binary format) or as external file references.
- **Scene hierarchy:** nodes with transforms.

The engine uses the **cgltf** library (header-only, MIT license, vendored under `third_party/cgltf/`) to parse glTF files. cgltf handles the complex binary format parsing; the engine then converts the parsed data into its own types.

### The Loading Pipeline

```
.glb file on disk
       |
       v
  cgltf_parse()          Parse the binary glTF container
       |
       v
  cgltf_load_buffers()   Load embedded buffer data
       |
       v
  Load textures:         stb_image to decode PNG/JPG → RGBA8 pixels
                         rhi.create_texture(pixel_data)
       |
       v
  Build materials:       Map cgltf material → ge::gfx::Material
                         (factors, texture handles + transforms/UV sets,
                          alpha mode/cutoff, double-sided, normal scale,
                          occlusion strength, emissive strength)
       |
       v
  Flatten scene graph:   Walk nodes → (mesh, world-transform) instances
       |
       v
  Extract vertices:      Convert cgltf accessors → LitVertex[]
                         (position, normal, uv0, uv1, color, tangent;
                          GENERATE normals / tangents when absent;
                          BAKE the node world transform into each vertex)
  Extract indices:       Convert cgltf accessors → u32[]
       |
       v
  Create GPU buffers:    rhi.create_buffer(vertex_data)   [DEFAULT heap:
                         rhi.create_buffer(index_data)     staged upload to VRAM]
       |
       v
  Build sub-meshes:      Map primitives → ge::gfx::SubMesh[] (+ centroid)
       |
       v
  Return Model struct    (mesh + materials + textures + sampler mode)
```

The loader merges all mesh primitives — across every instanced node — into a single pair of vertex/index buffers for efficiency. Each primitive instance becomes a `SubMesh` with its offset, count, material index, and centroid.

### Scene-Graph Baking and Instancing

glTF positions geometry through a **node hierarchy**: a mesh is placed in the world by the accumulated transform of the node that references it, and one mesh can be referenced by several nodes. The loader honours this by flattening the graph into a list of `(mesh, world-matrix)` instances via `cgltf_node_transform_world`, then baking each instance's world transform directly into its vertices:

- **Positions** are transformed by the node's world matrix.
- **Normals and tangents** are transformed by the **inverse-transpose** of the world matrix, so non-uniform node scale does not distort them.

A mesh referenced by N nodes is emitted N times at its N world positions. If no node references any mesh (a degenerate file), the loader falls back to baking every mesh at identity. cgltf writes world matrices column-major; copying element `arr[i*4+j]` into our row-major `mat4[i][j]` yields the transpose, which is exactly the row-vector matrix the engine needs.

### Fallbacks for Missing Attributes

glTF makes several vertex attributes optional. The loader synthesizes anything a primitive omits so downstream shading is always well-defined:

| Missing | Fallback |
|---------|----------|
| **Indices** | Sequential `0,1,2,…` generated per primitive |
| **Normals** | Area-weighted normals accumulated from triangle face normals, then normalized |
| **Tangents** | Derived from positions + UVs (Lengyel's method), orthonormalized against the normal (Gram-Schmidt) with a computed handedness sign. If the primitive also lacks UVs, a stable `{1,0,0,1}` frame is used |
| **UV set 1 / vertex colour** | `uv1` defaults to `(0,0)`; `color` defaults to white `(1,1,1,1)` |
| **Materials** | A single default `Material` (white dielectric) is created |

### Material Feature Coverage

Beyond the base metallic-roughness factors and the five texture maps, the loader reads:

- **`alpha_mode`** (OPAQUE / MASK / BLEND) and **`alpha_cutoff`**.
- **`double_sided`** — disables back-face culling for that material.
- **`normal_texture.scale`** and **`occlusion_texture.strength`**.
- **`KHR_materials_emissive_strength`** — multiplied into the emissive factor at load.
- **`KHR_texture_transform`** and the per-texture **UV-set index** for every map.
- The dominant glTF **sampler wrap mode** (from the base-colour texture's sampler), stored on the `Model` and used to configure the PBR static sampler.

Textures are decoded to RGBA8; base-colour and emissive maps are authored in sRGB and linearised in the shader, while metallic-roughness, normal, and occlusion maps are treated as linear data.

---

## Camera System (`camera.h`)

### Camera Representation

The camera stores position and Euler angles (yaw and pitch):

```cpp
struct Camera {
    vec3 position = {0, 1.5f, -3.0f};
    f32  yaw      = 0.0f;     // Radians around Y axis
    f32  pitch    = 0.0f;     // Radians around X axis, clamped to [-89, 89] degrees
    f32  fov_y    = 60 * k_deg_to_rad;  // Vertical field of view
    f32  near_z   = 0.1f;
    f32  far_z    = 100.0f;
};
```

The forward direction is derived from yaw and pitch using spherical coordinates:

```cpp
vec3 forward() const {
    f32 cy = cosf(yaw),   sy = sinf(yaw);
    f32 cp = cosf(pitch), sp = sinf(pitch);
    return {sy * cp, sp, cy * cp};
}
```

The view matrix is computed via `mat4_look_at_lh(position, position + forward(), up)`.

### Fly-Through Controller

The `camera_fly_update()` function implements an FPS-style camera controller:

1. **Mouse look:** Mouse delta is multiplied by a sensitivity factor and added to yaw/pitch. Pitch is clamped to prevent the camera from flipping upside down (the "gimbal lock" of Euler angles).

2. **WASD movement:** Forward, right, and up movement inputs are combined into a velocity vector. If non-zero, the velocity is normalized to prevent diagonal movement from being faster than cardinal movement.

3. **Position update:** `position += velocity * speed * dt`.

The controller uses the camera's own forward and right vectors for movement, so pressing W always moves in the direction the camera is facing.

---

## Forward Lighting with Blinn-Phong

> **Historical / study material.** The dedicated Blinn-Phong model pipeline
> (`model_vs.hlsl` / `model_ps.hlsl`) was retired by ADR-0010 — PBR is the
> only model path now. The lighting *theory* below is kept because it is the
> foundation the PBR section builds on, and the per-frame constant-buffer
> layout shown here is still exactly what the PBR shaders consume.

### What is Forward Rendering?

In forward rendering, each object is drawn once (or once per light), and the lighting calculation happens in the pixel shader during the draw call. This is the simplest rendering approach and is efficient for scenes with few lights.

The alternative is deferred rendering, where geometry and surface properties are written to a set of "G-buffers" in a first pass, and lighting is applied in a screen-space second pass. Deferred is better for many lights but has higher memory cost and cannot easily handle transparent objects. MyEngine uses forward rendering, which handles transparency naturally — see the [Transparency](#transparency) section for the alpha-blend path.

### The Blinn-Phong Lighting Model

Blinn-Phong is a classic empirical lighting model that approximates how light interacts with surfaces. It has three components:

**1. Ambient:** A constant amount of light everywhere, simulating indirect illumination that bounces off other surfaces. Without ambient, surfaces facing away from all lights would be completely black.

```hlsl
float3 ambient = 0.08;  // Low constant
float3 result = albedo.rgb * ambient;
```

**2. Diffuse (Lambertian):** Light that scatters equally in all directions when hitting a rough surface. The brightness depends on the angle between the surface normal `N` and the light direction `L`:

```hlsl
float NdotL = saturate(dot(N, L));  // 0 to 1
result += albedo.rgb * light_color * intensity * NdotL;
```

`saturate()` clamps to [0, 1] — negative dot products (light behind the surface) contribute zero.

**3. Specular (Blinn-Phong):** The shiny highlight on smooth surfaces. Blinn-Phong uses the "halfway vector" `H = normalize(L + V)` (where `V` is the view direction) and computes the specular contribution as `(N dot H)^shininess`:

```hlsl
float3 H = normalize(L + V);
float NdotH = saturate(dot(N, H));
float spec = pow(NdotH, 64.0);  // 64 = shininess exponent
result += light_color * intensity * spec * 0.3;
```

Higher shininess values produce tighter, more focused highlights. The `0.3` factor scales the specular contribution relative to diffuse.

### Light Types

**Directional light (sun):** Parallel rays from an infinitely distant source. All points in the scene receive light from the same direction. Used for sunlight/moonlight.

```cpp
struct DirectionalLight {
    vec3 direction;  // Direction TO the light
    vec3 color;
    f32  intensity;
};
```

**Point light:** Light radiates from a point in all directions, with distance attenuation. Used for torches, lamps, explosions.

```cpp
struct PointLight {
    vec3 position;
    f32  radius;     // Maximum influence distance
    vec3 color;
    f32  intensity;
};
```

The attenuation model uses an inverse-square falloff with a radius cutoff:

```hlsl
float attenuation = saturate(1.0 - dist / radius);
attenuation *= attenuation;  // Squared for smoother falloff
```

### Constant Buffer Layout

The shader receives lighting data through two constant buffers. Since ADR-0010 both are allocated per frame from the **upload ring** (see [Draw Submission API](#draw-submission-api)) rather than from dedicated buffers:

**Per-frame CB (b0)** — written once per frame, contains camera and light data:

| Field | Type | Description |
|-------|------|-------------|
| `view` | `float4x4` | Camera view matrix |
| `proj` | `float4x4` | Camera projection matrix |
| `camera_pos` | `float3` | Camera world position (for specular) |
| `sun_direction` | `float3` | Directional light direction |
| `sun_color` | `float3` | Directional light color |
| `sun_intensity` | `float` | Directional light intensity |
| `point_positions[4]` | `float4[4]` | Point light positions (xyz) + radius (w) |
| `point_colors[4]` | `float4[4]` | Point light colors (xyz) + intensity (w) |
| `point_light_count` | `int` | Number of active point lights |
| `shadow_enabled` | `float` | 1.0 if shadows are active |
| `light_view_proj` | `float4x4` | Light's view-projection matrix (for shadows) |

**Per-object CB (b1)** — one ring allocation per submitted instance, contains the instance transform:

| Field | Type | Description |
|-------|------|-------------|
| `model` | `float4x4` | Object's world transform matrix |
| `normal_matrix` | `float4x4` | Inverse-transpose of model (for transforming normals) |

The normal matrix is needed because normals must be transformed by the inverse-transpose of the model matrix, not the model matrix itself. If the model has non-uniform scale, using the model matrix directly would distort normals.

### The Vertex Shader

The (retired) Blinn-Phong vertex shader transformed vertices from model space to clip space — the PBR vertex shader does the same, plus the tangent frame:

```hlsl
VSOutput main(VSInput input) {
    float4 world_pos = mul(float4(input.position, 1.0), model);
    output.world_pos = world_pos.xyz;
    output.position  = mul(mul(world_pos, view), proj);
    output.normal    = normalize(mul(input.normal, (float3x3)normal_matrix));
    output.uv        = input.uv;
    return output;
}
```

The transform chain is: `model_space → (model) → world_space → (view) → camera_space → (proj) → clip_space`. The row-vector convention means `mul(point, matrix)`.

World position and normal are passed to the pixel shader as interpolated values (the GPU interpolates them across the triangle during rasterization).

### The Pixel Shader

The (retired) Blinn-Phong pixel shader evaluated the lighting model for each pixel:

1. Sample the albedo texture.
2. Normalize the interpolated normal (interpolation can denormalize it).
3. Compute the view direction from the camera position to the pixel's world position.
4. Accumulate ambient, directional (with shadow), and point light contributions.
5. Output the final color.

---

## Shadow Mapping

### How Shadow Mapping Works

Shadow mapping is a two-pass technique:

**Pass 1 (Shadow pass):** Render the scene from the light's perspective into a depth-only texture (the "shadow map"). This records how far the closest surface is from the light at each pixel.

**Pass 2 (Main pass):** For each pixel visible to the camera, transform its world position into the light's clip space and compare its depth to the shadow map. If the pixel is farther from the light than the recorded depth, it is in shadow.

```
Light's view:                     Camera's view:
+---+---+---+---+                 +---+---+---+---+
| 2 | 3 | 5 | 8 |  shadow map    | . | . | S | . |  S = in shadow
| 1 | 2 | 4 | 6 |  (depth values)| . | S | S | . |  (pixel depth > shadow map depth)
| 1 | 1 | 3 | 5 |                | . | . | S | . |
+---+---+---+---+                +---+---+---+---+
```

### The Shadow Pass

The shadow pass renders the scene with:
- An **orthographic projection** from the light's perspective (directional lights produce parallel rays).
- A **depth-only pipeline** (vertex shader only, no pixel shader) — we only care about depth values, not color.
- A **2048x2048 D32_FLOAT** texture as the depth target.
- **Front-face culling** instead of back-face culling, to reduce self-shadowing artifacts.
- **Depth bias** (constant bias 1000 + slope-scaled bias 1.0) to prevent shadow acne.

The shadow vertex shader simply transforms vertices into the light's clip space:

```hlsl
float4 main(VSInput input) : SV_Position {
    float4 world_pos = mul(float4(input.position, 1.0), model);
    return mul(world_pos, light_view_proj);
}
```

### Shadow Sampling with PCF

Simple shadow mapping produces hard, aliased shadow edges. PCF (Percentage-Closer Filtering) softens the edges by sampling multiple points in the shadow map and averaging the results.

The pixel shader uses a 3x3 PCF kernel:

```hlsl
float calc_shadow(float3 world_pos) {
    float4 light_space = mul(float4(world_pos, 1.0), light_view_proj);
    float3 proj_coords = light_space.xyz / light_space.w;

    // NDC to [0,1] UV
    float2 shadow_uv = float2(proj_coords.x * 0.5 + 0.5,
                               -proj_coords.y * 0.5 + 0.5);
    float depth = proj_coords.z;

    // 3x3 PCF
    float shadow = 0.0;
    float texel = 1.0 / 2048.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            shadow += shadow_map.SampleCmpLevelZero(
                shadow_samp,
                shadow_uv + float2(x, y) * texel,
                depth);
        }
    }
    return shadow / 9.0;  // Average of 9 samples
}
```

`SampleCmpLevelZero` is a special texture sampling function that:
1. Samples the shadow map at the given UV coordinate.
2. Compares the sampled depth to the reference depth value.
3. Returns 1.0 if the pixel is lit (depth <= shadow map) or 0.0 if shadowed.

By averaging 9 such comparisons in a 3x3 grid, the shadow edge transitions smoothly from 0 (fully shadowed) to 1 (fully lit) over a few pixels, producing soft shadow edges.

The `SamplerComparisonState` (bound to register s1) is configured for `Less-Equal` comparison, matching the depth test direction.

### Shadow Acne and Depth Bias

"Shadow acne" is a common artifact where a surface shadows itself due to limited depth precision. The shadow map stores depth at discrete texels; when the surface is viewed at a grazing angle, the sampled depth can oscillate above and below the surface depth, creating a striped pattern.

The solution is **depth bias**: offsetting the depth values during the shadow pass to push them slightly away from the light:

```cpp
PipelineDesc shadow_pipeline;
shadow_pipeline.depth_bias_enable        = true;
shadow_pipeline.depth_bias               = 1000;   // Constant bias (in depth units)
shadow_pipeline.slope_scaled_depth_bias  = 1.0f;   // Scale with surface slope
```

The slope-scaled component is critical: surfaces at steep angles (where acne is worst) get more bias. Combined with back-face culling in the shadow pass (so back faces cast shadows, pushing the depth away from the front surface), this effectively eliminates shadow acne in practice.

---

## PBR Materials

### Why PBR?

Blinn-Phong is a useful approximation but it cannot reproduce metallic surfaces (which have no diffuse component and highly tinted reflections) or the roughness-driven specular broadening seen on rough dielectrics. Physically Based Rendering (PBR) replaces the empirical Blinn-Phong formula with a physically motivated microfacet model grounded in light transport theory. The same material parameters look correct under any lighting environment, including the dynamic shadow + point light setup MyEngine uses.

### The Metallic-Roughness Workflow

glTF 2.0 uses the **metallic-roughness workflow**:

- **Base color:** The albedo of the surface. For dielectrics this is the diffuse colour; for metals it is the colour of the specular reflection.
- **Metallic (0–1):** 0 = dielectric (plastic, stone, skin), 1 = metal (gold, iron). Metals have no diffuse component and their F0 (Fresnel reflectance at normal incidence) is taken from the base colour. Dielectrics have an achromatic F0 ≈ 0.04.
- **Roughness (0–1):** 0 = perfectly smooth mirror, 1 = fully diffuse. Controls the width of the specular highlight via the GGX NDF.

These three parameters can come from scalar constants, textures, or a product of both. The **metallic-roughness texture** packs roughness into the G channel and metallic into the B channel (R channel is unused).

### Cook-Torrance BRDF

The PBR pixel shader evaluates the Cook-Torrance microfacet specular BRDF for each light:

```
f = f_diffuse + f_specular

f_diffuse  = kD * albedo / π          kD = (1 - F) * (1 - metallic)
f_specular = (D * G * F) / (4 * NdotV * NdotL + ε)
```

**D — GGX/Trowbridge-Reitz Normal Distribution Function:** Describes the statistical distribution of microfacet orientations. A rougher surface has a wider NDF, broadening the specular highlight.

```hlsl
float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}
```

**G — Smith Height-Correlated Masking-Shadowing (Schlick approximation):** Accounts for microfacets blocking each other's visibility (masking from the camera side, shadowing from the light side).

```hlsl
float G_Smith_Schlick(float NdotV, float NdotL, float roughness) {
    float r  = roughness + 1.0;
    float k  = (r * r) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}
```

**F — Schlick Fresnel:** The fraction of light reflected vs. refracted at the surface. At grazing angles (HdotV → 0) the Fresnel term approaches 1, producing the bright rim highlight seen on all real surfaces.

```hlsl
float3 F_Schlick(float HdotV, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - HdotV), 5.0);
}
```

F0 is the surface's reflectance at normal incidence: `lerp(0.04, base_color, metallic)`.

### PBR Root Signature

The PBR pipeline uses `BindingLayout{3, 6}` (9 root parameters). See the [Root Signatures](#root-signatures) section for the full binding table. The key addition is **slot 2 (b2)**, the per-material constant buffer, which carries material scalars and texture-presence flags that let the shader avoid sampling missing textures.

### Per-Material Constant Buffer

```hlsl
struct TexXform {                // per-map KHR_texture_transform + UV set
    float4 offset_scale;         // xy = offset, zw = scale
    float4 rot_uvset;            // x = rotation (rad), y = uv_set (0/1)
};

cbuffer PerMaterial : register(b2) {
    float4   base_color_factor;  // RGBA multiplier applied to base colour
    float4   mr_params;          // x=metallic y=roughness z=normal_scale w=occlusion_strength
    float4   emissive_alpha;     // xyz = emissive factor, w = alpha_cutoff
    float4   flags0;             // x=alpha_mode(0/1/2) y=has_base z=has_mr w=has_normal
    float4   flags1;             // x=has_occlusion y=has_emissive
    TexXform xform_base;
    TexXform xform_mr;
    TexXform xform_normal;
    TexXform xform_occlusion;
    TexXform xform_emissive;
};  // 240 bytes (fits the 256-byte constant buffer)
```

Scalars are packed into `float4` rows to respect HLSL's 16-byte cbuffer alignment, and boolean texture-presence flags are stored as `float` (0.0 / 1.0). `alpha_mode` is a float enum (0 = opaque, 1 = mask, 2 = blend). Each of the five maps carries its own `TexXform` so the pixel shader can transform and UV-select per texture. Each loaded model owns one such buffer per material, filled once at load time (`create_material_cbs`) and never changed.

### The PBR Vertex Shader

`pbr_vs.hlsl` reads all six vertex attributes, passes both UV sets and the vertex colour through, and outputs a full TBN frame in world space:

```hlsl
output.world_pos  = mul(float4(input.position, 1.0), model).xyz;
output.normal     = normalize(mul(input.normal,   (float3x3)normal_matrix));
output.tangent    = normalize(mul(input.tangent.xyz, (float3x3)normal_matrix));
output.bitangent  = cross(output.normal, output.tangent) * input.tangent.w;
output.uv         = input.uv;    // TEXCOORD0
output.uv1        = input.uv1;   // TEXCOORD1
output.color      = input.color; // COLOR_0
```

The tangent and bitangent are world-space vectors that form the columns of the TBN matrix used in the pixel shader for normal mapping. Because the model's glTF node transforms are baked into the vertex data at load time, `model` and `normal_matrix` here carry only the per-instance transform passed to `submit()` (see [Draw Submission API](#draw-submission-api)).

### The PBR Pixel Shader

`pbr_ps.hlsl` evaluates the full BRDF for every light and accumulates the result:

1. **UV preparation:** For each map, select UV set 0 or 1 and apply its `KHR_texture_transform` (rotate, scale, offset) via `apply_xform`.
2. **Base colour:** Sample (sRGB → linear), multiply by `base_color_factor` **and the interpolated vertex colour**.
3. **Alpha test:** For MASK materials, `clip(base.a - alpha_cutoff)` discards sub-cutoff fragments.
4. **Normal mapping:** If `has_normal_tex`, sample the normal map, unpack to [-1,1], scale the XY by `normal_scale`, and rotate into world space via `T * nx + B * ny + N * nz`.
5. **Surface parameters:** Sample metallic/roughness (clamped); sample AO and lerp with `occlusion_strength`.
6. **Ambient / IBL:** Analytic environment lighting replaces the old flat constant — see [Analytic Environment Lighting (IBL)](#analytic-environment-lighting-ibl).
7. **Directional light:** Evaluate Cook-Torrance BRDF, multiply by shadow factor from PCF shadow map.
8. **Point lights:** Evaluate Cook-Torrance BRDF for each active light, attenuated by `(1 - dist/radius)^2`.
9. **Emissive:** Add `emissive_factor * srgb_to_linear(emissive_tex)`.
10. **Tone map + gamma:** Apply Reinhard tone mapping and γ correction (see the Tone Mapping section).

The pixel shader outputs `base.a` in the alpha channel, which the blend state consumes for transparent draws.

### Analytic Environment Lighting (IBL)

Direct lights alone leave PBR surfaces looking wrong: a **metal** has no diffuse term, so away from a direct highlight it reflects only its surroundings — with no environment it renders nearly black. Real engines solve this with **image-based lighting (IBL)**: a prefiltered environment cubemap supplies both diffuse irradiance and glossy reflections.

MyEngine ships an **analytic, procedural** environment model that captures the important behaviour without an HDR asset or an offline prefilter pass:

- **Environment function** — a simple hemispheric sky↔ground gradient sampled by direction:

  ```hlsl
  float3 env_sample(float3 dir) {
      return lerp(kGroundColor, kSkyColor, saturate(dir.y * 0.5 + 0.5));
  }
  ```

- **Diffuse IBL** — irradiance from the surface normal, tinted by albedo, removed for metals:

  ```hlsl
  float3 diffuse_ibl = env_sample(N) * base.rgb * (1.0 - metallic);
  ```

- **Specular IBL** — the reflection-vector environment colour weighted by **Karis' split-sum analytic `EnvBRDFApprox`**, a curve fit that reproduces the environment BRDF (scale + bias on F0) with **no BRDF LUT**:

  ```hlsl
  float3 R = reflect(-V, N);
  float3 specular_ibl = env_sample(R) * env_brdf_approx(F0, roughness, NdotV);
  ```

- Both terms are modulated by ambient occlusion: `ambient = (diffuse_ibl + specular_ibl) * ao`.

This gives metals visible, roughness-aware reflections and keeps shadowed areas plausibly lit, at the cost of a couple of `lerp`s per pixel. **True HDR-cubemap IBL** (equirect/cubemap load → prefiltered radiance mips + irradiance map + BRDF LUT) is documented as future work; the analytic model is the stand-in until an environment asset and the offline prefilter pass exist.

### Tone Mapping and Gamma Correction

The PBR BRDF operates in linear light space and can produce values well above 1.0, especially on metallic surfaces under strong lights. Tone mapping compresses the high-dynamic-range result into the [0, 1] range suitable for an 8-bit display:

```hlsl
// Reinhard tone mapping
result = result / (result + 1.0);

// Gamma correction: linear → sRGB (approximate with pow 1/2.2)
result = pow(max(result, 0.0), 1.0 / 2.2);
```

The render target format is `R8G8B8A8_UNORM` (not `_SRGB`), so gamma correction must be applied explicitly in the shader. Input textures that are authored in sRGB space (base colour, emissive) are converted to linear before any math via `pow(c, 2.2)`.

### Draw Submission API

Since ADR-0010 the renderer is a multi-model scene renderer with an immediate-mode draw list:

```cpp
// Load a glTF model; returns an id for submit(), or kInvalidModelId.
// Up to 16 models can be loaded concurrently, each with its own materials.
ModelId Renderer::load_model(const char* path, ge::Allocator alloc);
void    Renderer::unload_model(ModelId id);

// Submit one instance of a loaded model for this frame. Valid between
// begin_frame() and end_frame(); submissions do not persist across frames.
void Renderer::submit(ModelId id, const mat4& transform);
```

`begin_frame(dt)` acquires the frame slot and clears the draw list; `end_frame()` renders everything submitted (shadow pass then PBR pass over the draw list) and presents. If nothing was submitted, the built-in reference cube is drawn instead (the golden-image baseline and empty-scene placeholder).

Each submitted instance gets its own per-object constant block (model matrix + inverse-transpose normal matrix, so non-uniform scale stays correct), bump-allocated from the **upload ring**: one persistently-mapped upload buffer per frame in flight (1 MiB each), reset every frame, handed out in 256-byte-aligned chunks and bound by offset. The shadow and PBR passes share the same per-object allocations. Because every draw owns distinct GPU addresses, no draw can overwrite another's constants — and because `RhiDevice::begin_frame()` fences the frame slot, the CPU never writes a ring buffer the GPU is still reading.

**Example — several spinning instances of two models:**

```cpp
ModelId box    = renderer.load_model("assets/models/box.glb", alloc);
ModelId helmet = renderer.load_model("assets/models/DamagedHelmet.glb", alloc);

float time = 0.0f;
while (running) {
    time += dt;
    renderer.begin_frame(dt);
    for (int i = 0; i < 5; ++i)
        renderer.submit(box, mat4_rotation_y(time + i) *
                             mat4_translation({(i - 2) * 2.0f, 0, 0}));
    renderer.submit(helmet, mat4_rotation_y(time));
    renderer.end_frame();
}
```

---

## Transparency

Forward rendering handles transparent surfaces naturally: a blended fragment is composited over whatever is already in the frame buffer during its own draw call. MyEngine implements the full glTF 2.0 alpha pipeline on the PBR path.

### Alpha Modes

Each material declares one of three glTF alpha modes, read into `Material::alpha_mode`:

- **`Opaque`** — alpha is ignored; the surface is fully opaque.
- **`Mask`** — alpha-tested. The pixel shader runs `clip(base.a - alpha_cutoff)`, discarding fragments below the cutoff. This gives crisp cut-outs (foliage, chain-link) with no blending and no sort dependency, so masked geometry is drawn with the opaque set.
- **`Blend`** — order-dependent alpha blending. The fragment is composited using its alpha as coverage.

### Blend State and PSO Variants

Blend state and cull mode are baked into the pipeline object, so the renderer builds a **2×2 matrix of PBR PSOs** and picks one per sub-mesh:

```
                Opaque blend        AlphaBlend
 cull Back      opaque/masked       one-sided transparent
 cull None      double-sided opaque double-sided transparent
```

The `AlphaBlend` variant uses straight (non-premultiplied) alpha — `SrcBlend = SRC_ALPHA`, `DestBlend = INV_SRC_ALPHA` — and **forces depth-write off** so a transparent surface never occludes what is behind it, while still depth-**testing** against opaque geometry drawn earlier.

### Back-to-Front Sorting

Alpha blending is order-dependent: `over` compositing is not commutative, so transparent surfaces must be drawn far-to-near. Each frame the PBR pass:

1. Partitions all **(instance, sub-mesh)** pairs across the draw list into an **opaque/masked** list and a **blended** list.
2. Draws the opaque/masked list first (depth write on), laying down the depth buffer.
3. Sorts the blended list by **view-space centroid depth** — each `SubMesh` centroid is transformed by its instance's `transform * view` and sorted on the resulting `z` (far first).
4. Draws the blended list back-to-front.

The sort spans every submitted instance of every model, so transparent sub-meshes from different models interleave correctly at the centroid level. Order-independent transparency (OIT) and per-triangle sorting are noted as future work.

### Double-Sided Materials

Thin transparent surfaces (glass, leaves, cloth) need both faces shaded. When `Material::double_sided` is set, the renderer selects the **cull-None** PSO variant for that sub-mesh so back faces are not discarded. Single-sided materials keep back-face culling for efficiency.
