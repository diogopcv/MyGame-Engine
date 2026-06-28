# Graphics and Rendering

This document covers the entire graphics subsystem: the DirectX 12 fundamentals, the Render Hardware Interface (RHI) abstraction, the render graph, mesh and material resources, model loading, the camera system, forward lighting with Blinn-Phong shading, and shadow mapping.

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

MyEngine uses double buffering (`kFrameCount = 2`). Each frame follows this pattern:

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

MyEngine's Phase 3 root signature has 4 slots:

| Slot | Type | Register | Content |
|------|------|----------|---------|
| 0 | Root CBV | b0 | Per-frame data (view, projection, lights) |
| 1 | Root CBV | b1 | Per-object data (model matrix, normal matrix) |
| 2 | Descriptor table | t0 | Albedo texture |
| 3 | Descriptor table | t1 | Shadow map |

Plus two static samplers:
- **s0:** Linear wrap sampler (for albedo textures). The filter mode is configurable per pipeline — the model pipeline uses linear, others may use point.
- **s1:** Comparison sampler (for shadow map PCF sampling).

A **root CBV** is a constant buffer bound directly by GPU virtual address — no descriptor needed, so it is slightly faster than going through a descriptor table.

### Pipeline State Objects (PSOs)

A PSO bundles all GPU state needed for rendering: vertex and pixel shaders, input layout, rasterizer state (cull mode, fill mode), depth-stencil state, blend state, and render target formats. Setting a PSO is a single GPU command that configures everything at once.

DX12 requires PSOs to be created up front (not modified on the fly). This is a departure from DX11, where individual states could be changed independently. The benefit is that the GPU driver can pre-compile and optimize the entire pipeline configuration at creation time, avoiding runtime overhead.

MyEngine creates PSOs at initialization:
- **Model pipeline:** Vertex + pixel shader for lit geometry (Blinn-Phong), back-face culling, depth test/write enabled, linear texture sampling.
- **Shadow pipeline:** Vertex shader only (no pixel shader — depth-only rendering), front-face culling (to reduce shadow acne), depth bias enabled.

### Debug Layer, GPU Validation, and DRED

In Debug builds, MyEngine enables three DX12 debugging features:

1. **Debug Layer (`ID3D12Debug`):** Validates API usage (correct state transitions, valid arguments, resource lifetime) and reports errors to the info queue.

2. **GPU-Based Validation:** Catches errors that only manifest on the GPU (shader bugs, out-of-bounds resource access). Slower than CPU validation but catches more issues.

3. **DRED (Device Removed Extended Data):** When the GPU crashes ("device removed"), DRED provides breadcrumb data showing which GPU operations completed before the crash. Without DRED, a device-removed error gives no information about what went wrong.

The smoke tests check that `debug_error_count() == 0` after running, ensuring the renderer produces zero DX12 validation errors.

---

## The RHI Abstraction (`rhi.h`)

### Why an RHI?

Phase 1 implemented everything directly against DX12 COM objects in `renderer_dx12.cpp`. Phase 3 introduced the RHI (Render Hardware Interface) to separate the "what to do" from the "how to do it on DX12":

- **Clean separation of concerns.** The `Renderer` describes what to render (set camera, draw model, shadow pass). The `RhiDevice` executes the low-level GPU commands.
- **Testability.** The render graph can be unit-tested against the RHI's handle-based API without creating a real GPU device.
- **Future backend support.** If a Vulkan backend is added, only `rhi_dx12.cpp` would be replaced — the `Renderer` and render graph would not change.

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
void set_constant_buffer(u32 slot, BufferHandle);
void set_texture(u32 slot, TextureHandle);
void draw_indexed(u32 index_count, u32 start_index, i32 base_vertex);
```

**Frame management:**
```cpp
void begin_frame();        // Wait for frame slot, reset allocator/list
void end_frame();          // Close and submit command list
void present(bool vsync);  // Swap chain present + signal fence
```

### The RHI Type System (`rhi_types.h`)

All RHI types are plain data — no OS or DX12 headers leak through. This file defines:

- **Format enum:** Pixel/vertex element formats (`R8G8B8A8_UNORM`, `D32_FLOAT`, etc.).
- **Usage enums:** How a buffer or texture will be used (`Vertex`, `Index`, `Constant`, `ShaderResource`, `DepthStencil`, etc.).
- **ResourceState enum:** For barrier transitions (`RenderTarget`, `PixelShaderResource`, `DepthWrite`, `Present`, etc.).
- **Descriptor structs:** `BufferDesc`, `TextureDesc`, `ShaderDesc`, `PipelineDesc` — all the parameters needed to create a resource.

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

Phase 3 uses a single vertex format for all lit geometry:

```cpp
struct LitVertex {
    f32 position[3];  // 12 bytes — world-space position
    f32 normal[3];    // 12 bytes — unit surface normal
    f32 uv[2];        //  8 bytes — texture coordinates
};
// Total stride: 32 bytes
```

The input layout tells the GPU how to interpret this data:
- `POSITION`: `R32G32B32_FLOAT` at offset 0
- `NORMAL`: `R32G32B32_FLOAT` at offset 12
- `TEXCOORD`: `R32G32_FLOAT` at offset 24

### Mesh and SubMesh

A `Mesh` owns shared GPU vertex and index buffers. A `SubMesh` describes a range within those buffers:

```
Vertex Buffer: [v0 v1 v2 v3 v4 v5 v6 v7 v8 ...]
Index Buffer:  [0 1 2  2 1 3  4 5 6  6 5 7 ...]

SubMesh 0: index_offset=0,  index_count=6,  base_vertex=0,  material=0
SubMesh 1: index_offset=6,  index_count=6,  base_vertex=4,  material=1
```

This design packs an entire model into two GPU buffers (one vertex, one index), minimizing buffer-binding changes during rendering. Each submesh can reference a different material.

### Materials

A `Material` in Phase 3 is simple: an optional albedo texture and a base color fallback:

```cpp
struct Material {
    TextureHandle albedo_texture;
    f32           base_color[4] = {1,1,1,1};
    bool          has_albedo_texture = false;
};
```

If the model has a texture, `has_albedo_texture` is `true` and the albedo texture is bound for rendering. Otherwise, the shader uses the flat base color. This will be extended in the future with PBR parameters (roughness, metalness, normal maps).

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
  Extract vertices:      Convert cgltf accessors → LitVertex[]
  Extract indices:       Convert cgltf accessors → u32[]
       |
       v
  Create GPU buffers:    rhi.create_buffer(vertex_data)
                         rhi.create_buffer(index_data)
       |
       v
  Load textures:         stb_image to decode PNG/JPG → RGBA8 pixels
                         rhi.create_texture(pixel_data)
       |
       v
  Build materials:       Map cgltf material → ge::gfx::Material
       |
       v
  Build sub-meshes:      Map cgltf mesh primitives → ge::gfx::SubMesh[]
       |
       v
  Return Model struct    (mesh + materials + textures)
```

The loader merges all mesh primitives into a single pair of vertex/index buffers for efficiency. Each primitive becomes a `SubMesh` with its offset, count, and material index.

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

### What is Forward Rendering?

In forward rendering, each object is drawn once (or once per light), and the lighting calculation happens in the pixel shader during the draw call. This is the simplest rendering approach and is efficient for scenes with few lights.

The alternative is deferred rendering, where geometry and surface properties are written to a set of "G-buffers" in a first pass, and lighting is applied in a screen-space second pass. Deferred is better for many lights but has higher memory cost and cannot easily handle transparent objects. MyEngine uses forward rendering for Phase 3.

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

The shader receives lighting data through two constant buffers:

**Per-frame CB (b0)** — set once per frame, contains camera and light data:

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

**Per-object CB (b1)** — set once per object, contains the model transform:

| Field | Type | Description |
|-------|------|-------------|
| `model` | `float4x4` | Object's world transform matrix |
| `normal_matrix` | `float4x4` | Inverse-transpose of model (for transforming normals) |

The normal matrix is needed because normals must be transformed by the inverse-transpose of the model matrix, not the model matrix itself. If the model has non-uniform scale, using the model matrix directly would distort normals.

### The Vertex Shader

The vertex shader (`model_vs.hlsl`) transforms vertices from model space to clip space:

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

The pixel shader (`model_ps.hlsl`) evaluates the lighting model for each pixel:

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

The slope-scaled component is critical: surfaces at steep angles (where acne is worst) get more bias. Combined with front-face culling in the shadow pass (so the back faces of objects cast shadows, not the front faces), this effectively eliminates shadow acne in practice.
