# Phase 3 — Renderer proper

> Status: **Complete**

## Goal

Turn the Phase 1 monolithic DX12 renderer into a real graphics pipeline: a clean
RHI abstraction backed by handle-based resource management, a frame graph for
automatic barrier insertion, mesh/material/texture resources, glTF model import,
a fly-through camera, forward lighting, and shadow mapping.

**Definition of done:** load and render a real 3D model with lighting, shadows,
and a fly-through camera.

## Scope

**In scope:**
- RHI abstraction (handle-based, non-virtual, DX12 backend)
- Frame graph (pass declaration, automatic barriers, transient resources)
- Mesh / material / texture resource types
- Model import (cgltf for glTF parsing — third-party, wrapped behind engine API)
- Camera (perspective projection, fly-through controller)
- Forward lighting (directional + point lights, Blinn-Phong)
- Basic shadow mapping (single directional shadow map, PCF)
- Platform input extension (keyboard + mouse state in Window)

**Out of scope:**
- Vulkan / multi-backend (DX12 only — add abstraction layer if/when needed)
- PBR materials (Blinn-Phong first; PBR upgrade is a follow-up)
- Cascaded shadow maps, soft shadows, ambient occlusion
- Post-processing stack
- Scene graph / spatial queries (Phase 4)

## Tech decisions

### RHI design — handle-based, concrete class

The RHI uses the engine's existing `Handle<Tag>` + `HandleTable<T>` from core.
GPU resources (buffers, textures, shaders, pipelines) are identified by opaque
handles. The DX12 backend stores the real COM objects in handle tables inside a
pImpl struct. No virtual methods — DX12 is the only backend and there are no hot
virtual calls.

**Rationale:** Aligns with the data-oriented philosophy and reuses existing core
infrastructure. A virtual `IRhiDevice` can be introduced later if a second
backend is added — YAGNI for now.

### Binding model — keep ADR-0006 classic root signature

Phase 3 keeps the classic root-signature approach from ADR-0006. The root
signature grows to accommodate per-frame, per-object, and per-material data:

| Slot | Type | Register | Visibility | Content |
|------|------|----------|------------|---------|
| 0 | Root CBV | b0 | ALL | Per-frame data (view, proj, camera pos, lights) |
| 1 | Root CBV | b1 | VERTEX | Per-object data (model matrix, normal matrix) |
| 2 | Descriptor table | t0 | PIXEL | Albedo texture |
| 3 | Descriptor table | t1 | PIXEL | Shadow map |

Static samplers: s0 (linear wrap for textures), s1 (comparison sampler for
shadow mapping).

### Lighting path — forward Blinn-Phong

Start with forward rendering and Blinn-Phong shading. Supports one directional
light and up to 4 point lights (data packed in a per-frame constant buffer).
Forward+ (light culling) deferred until many-light scenarios demand it.

### Shadow technique — basic shadow mapping

Single shadow map for the directional light. 2048x2048 D32_FLOAT depth-only
pass. PCF (2x2 or 3x3 kernel) for filtering. No cascades — single frustum with
a manually tuned ortho projection. Cascaded shadows are future work.

### Model import — cgltf (header-only, MIT)

cgltf parses glTF 2.0 files. The engine converts parsed data into its own
Mesh/Material/Texture types at load time. cgltf is vendored under `third_party/`
and wrapped so it never leaks into public headers.

### Platform input — extend Window with key/mouse state

Add keyboard and mouse polling to `platform::Window`. The window procedure
handles `WM_KEYDOWN/UP`, `WM_MOUSEMOVE`, `WM_LBUTTONDOWN/UP`, etc. and stores
state in the pImpl. Public API exposes `key_down(KeyCode)`, `mouse_position()`,
`mouse_delta()`, `mouse_button_down(button)`. No full input system yet — that's
Phase 4.

## Architecture

```
engine/gfx/
├── include/gfx/
│   ├── rhi_types.h          enums, descriptor structs, handle typedefs
│   ├── rhi.h                RhiDevice class (pImpl)
│   ├── render_graph.h       frame graph: pass declaration + execution
│   ├── camera.h             Camera struct + projection helpers
│   ├── mesh.h               Mesh + SubMesh structs
│   ├── material.h           Material descriptor
│   ├── light.h              DirectionalLight, PointLight
│   ├── model_loader.h       glTF import (returns Mesh + Material arrays)
│   └── renderer.h           high-level Renderer (orchestrates RHI + graph)
├── src/
│   ├── rhi_dx12.cpp         DX12 backend (HandleTables of COM objects)
│   ├── render_graph.cpp     graph compilation + execution
│   ├── camera.cpp           (if needed, else header-only)
│   ├── model_loader.cpp     cgltf wrapper
│   └── renderer_dx12.cpp    high-level Renderer (rewritten to use RHI)
├── shaders/
│   ├── lit_vs.hlsl          forward-lit vertex shader
│   ├── lit_ps.hlsl          forward-lit pixel shader
│   ├── shadow_vs.hlsl       depth-only vertex shader
│   ├── depth_only_ps.hlsl   (empty or alpha-test pixel shader)
│   ├── cube_vs.hlsl         (kept for Phase 1 compat / tests)
│   └── cube_ps.hlsl         (kept for Phase 1 compat / tests)
└── tests/
    ├── test_smoke.cpp        boot/shutdown, debug-layer zero errors
    ├── test_golden.cpp       reference scene golden image
    ├── test_render_graph.cpp barrier correctness, pass ordering
    └── test_mesh.cpp         mesh/material/texture load round-trip
```

### RHI type overview

```cpp
// Handle types (using core Handle<Tag>)
struct BufferTag {};  using BufferHandle   = Handle<BufferTag>;
struct TextureTag {}; using TextureHandle  = Handle<TextureTag>;
struct ShaderTag {};  using ShaderHandle   = Handle<ShaderTag>;
struct PipelineTag {};using PipelineHandle = Handle<PipelineTag>;

// Key enums
enum class Format       { R8G8B8A8_UNORM, R32G32B32_FLOAT, R32G32_FLOAT,
                          D32_FLOAT, R16_UINT, R32_UINT, ... };
enum class BufferUsage  { Vertex, Index, Constant, Upload, Readback };
enum class TextureUsage : u8 { ShaderResource = 1, RenderTarget = 2,
                               DepthStencil = 4 };
enum class ResourceState{ Common, RenderTarget, DepthWrite, DepthRead,
                          PixelShaderResource, CopyDest, CopySrc, Present };

// Descriptor structs
struct BufferDesc     { u32 size; BufferUsage usage; const void* initial_data; };
struct TextureDesc    { u32 width, height, mip_levels; Format format;
                        u8 usage_flags; const void* initial_data; u32 row_pitch; };
struct ShaderDesc     { const u8* bytecode; u32 bytecode_size; };
struct InputElement   { const char* semantic; u32 semantic_index; Format format;
                        u32 offset; };
struct PipelineDesc   { ShaderHandle vs, ps; const InputElement* inputs;
                        u32 input_count, input_stride; CullMode cull;
                        bool depth_enable; CompareFunc depth_func;
                        Format rt_format, ds_format; };
```

### RhiDevice surface (key methods)

```cpp
class RhiDevice {
public:
    bool init(const RhiDeviceDesc& desc);
    void shutdown();

    // Resources
    BufferHandle   create_buffer(const BufferDesc&);
    TextureHandle  create_texture(const TextureDesc&);
    ShaderHandle   create_shader(const ShaderDesc&);
    PipelineHandle create_pipeline(const PipelineDesc&);
    void destroy_buffer/texture/shader/pipeline(Handle);

    // Data transfer
    void update_buffer(BufferHandle, const void* data, u32 size, u32 offset);
    void* map_buffer(BufferHandle);
    void  unmap_buffer(BufferHandle);

    // Frame lifecycle
    void begin_frame();
    void end_frame();    // submits + presents
    void present(bool vsync);
    void resize(u32 w, u32 h);

    // Command recording (single command list per frame for now)
    void barrier(TextureHandle, ResourceState before, ResourceState after);
    void set_render_targets(TextureHandle rt, TextureHandle depth);
    void clear_render_target(TextureHandle rt, const f32 color[4]);
    void clear_depth(TextureHandle ds, f32 depth);
    void set_pipeline(PipelineHandle);
    void set_vertex_buffer(BufferHandle);
    void set_index_buffer(BufferHandle, Format);
    void set_constant_buffer(u32 slot, BufferHandle);
    void set_texture(u32 slot, TextureHandle);
    void set_viewport(f32 x, y, w, h, min_d, max_d);
    void set_scissor(i32 x, y, u32 w, h);
    void draw_indexed(u32 count, u32 start_index, i32 base_vertex);

    // Swapchain
    TextureHandle back_buffer();
    TextureHandle depth_buffer();

    // Debug
    u32  debug_error_count() const;
    bool capture_back_buffer(...) const;
};
```

### Frame graph

Simple ordered-pass model for Phase 3:

1. Passes are added in user-specified order (no automatic reordering).
2. Each pass declares resources it reads/writes with their required states.
3. The graph tracks each resource's current state and inserts barriers between
   passes when a state transition is needed.
4. Transient resources (e.g. shadow map) are created at graph setup and
   destroyed after execution.

This is simpler than a full frame graph with automatic reordering and memory
aliasing but covers Phase 3's needs (shadow pass → main pass) and establishes
the abstraction for later growth.

### Shader constant buffer layout

**Per-frame CB (b0, 256 bytes):**
```hlsl
cbuffer PerFrame : register(b0) {
    row_major float4x4 view;
    row_major float4x4 proj;
    float3 camera_pos;
    float  _pad0;
    float3 sun_direction;
    float  _pad1;
    float3 sun_color;
    float  sun_intensity;
    // Point lights (4 max)
    float4 point_positions[4];   // xyz = pos, w = radius
    float4 point_colors[4];      // xyz = color, w = intensity
    int    point_light_count;
    float3 _pad2;
    row_major float4x4 light_view_proj;  // for shadow mapping
};
```

**Per-object CB (b1, 256 bytes):**
```hlsl
cbuffer PerObject : register(b1) {
    row_major float4x4 model;
    row_major float4x4 normal_matrix;  // inverse transpose of model
};
```

### Lit vertex format

```cpp
struct LitVertex {
    f32 position[3];
    f32 normal[3];
    f32 uv[2];
};
// Input layout: POSITION (R32G32B32_FLOAT, 0), NORMAL (R32G32B32_FLOAT, 12),
//               TEXCOORD (R32G32_FLOAT, 24). Stride = 32 bytes.
```

## Milestones

### Milestone 1 — RHI abstraction

Extract the monolithic `renderer_dx12.cpp` into a handle-based RHI:
- Define `rhi_types.h` with all enums, descriptors, handle typedefs
- Implement `RhiDevice` in `rhi_dx12.cpp` (pImpl, HandleTables for resources)
- Rewrite `Renderer` to use `RhiDevice` for all resource creation and commands
- The spinning cube still renders identically through the new abstraction

**Tests:** existing smoke + golden tests pass unchanged (no visual regression).

### Milestone 2 — Frame graph

- Implement `RenderGraph` with pass declaration and automatic barrier insertion
- Shadow pass + main pass as the two initial passes
- Refactor `Renderer::begin_frame/end_frame` to build and execute the graph

**Tests:** unit tests for graph compilation (correct barrier states, ordering).

### Milestone 3 — Mesh / material / texture + model import

- Define `Mesh`, `SubMesh`, `Material`, resource types
- Vendor cgltf under `third_party/cgltf/`
- Implement `model_loader.cpp` to load glTF → engine Mesh/Material
- Load textures via stb_image → `RhiDevice::create_texture`
- Render imported model (unlit initially, just to verify geometry)

**Tests:** mesh/material load round-trip; model renders without debug errors.

### Milestone 4 — Camera + forward lighting

- `Camera` struct with perspective projection and view matrix
- Extend `platform::Window` with keyboard + mouse input
- Fly-through camera controller in sandbox
- New `lit_vs.hlsl` / `lit_ps.hlsl` with Blinn-Phong shading
- Per-frame CB with light data, per-object CB with model/normal matrices

**Tests:** golden image of lit model.

### Milestone 5 — Shadows

- Depth-only shadow pass (shadow_vs.hlsl)
- 2048x2048 shadow map as transient render-graph resource
- Shadow sampling in lit_ps.hlsl with PCF
- Light view-projection matrix in per-frame CB

**Tests:** golden image of model with shadows.

## Test plan

Per `plan.md`:
- **Golden image:** reference scene(s) with lit, shadowed model
- **Unit:** render-graph resource-lifetime / barrier assertions
- **Unit:** mesh/material/texture load round-trip
- **Smoke:** boot → N frames → shutdown, zero debug-layer errors

## Depends on

- Phase 1 (DX12 device, swapchain, command infrastructure)
- Phase 2 (Handle<T>, HandleTable, Allocator, Arena, containers, job system)

---

## Phase 3 Addendum — PBR Materials (2026-06-29)

> Status: **Complete**

### Motivation

The DamagedHelmet.glb reference model exposes the full glTF 2.0 PBR
metallic-roughness workflow. Blinn-Phong cannot represent metallic surfaces or
roughness-driven reflections correctly. PBR replaces (or co-exists alongside) the
Blinn-Phong path so the renderer can display physically plausible materials.

### What changes

| Area | Change |
|------|--------|
| Vertex format | Add `tangent[4]` (xyz + bitangent sign) to `LitVertex` → stride 48 bytes |
| Material struct | Add metallic_roughness, normal, occlusion, emissive texture handles + scalar factors |
| Root signature | New `RootLayout::PBR` option in `PipelineDesc` — 3 CBVs + 6 SRV descriptor tables |
| Model loader | Extract tangents; load all five PBR texture maps from cgltf |
| Shaders | `pbr_vs.hlsl` outputs world-space TBN frame; `pbr_ps.hlsl` — Cook-Torrance BRDF with GGX/D, Smith/G, Schlick/F |
| Renderer | PBR pipeline replaces Blinn-Phong for loaded models; per-material CB for scalar factors + texture-presence flags; default 1×1 white/flat-normal textures for missing maps |
| Model transform | `Renderer::set_model_transform(const mat4&)` added — sets model-to-world matrix used by shadow, PBR, and Blinn-Phong passes (normal matrix = transform for pure rotations) |
| Tone mapping | Reinhard tone map + gamma correction (pow 1/2.2) applied at end of PBR PS |
| Debug layer fix | `SetBreakOnSeverity(ERROR)` now gated on `IsDebuggerPresent()` — prevents the D3D12 INT3 breakpoint from crashing headless CTest runs as an unhandled SEH exception |

### Binding table — PBR root signature

| Root param | Type | Register | Content |
|------------|------|----------|---------|
| 0 | CBV | b0 | Per-frame (view, proj, camera, lights, shadow VP) |
| 1 | CBV | b1 | Per-object (model matrix, normal matrix) |
| 2 | CBV | b2 | Per-material (base color, metallic, roughness, emissive, texture flags) |
| 3 | Descriptor table | t0 | Base color texture (sRGB, linearised in PS) |
| 4 | Descriptor table | t1 | Metallic-roughness (G=roughness, B=metallic) |
| 5 | Descriptor table | t2 | Normal map (tangent-space) |
| 6 | Descriptor table | t3 | Occlusion (R channel) |
| 7 | Descriptor table | t4 | Emissive (RGB, sRGB, linearised in PS) |
| 8 | Descriptor table | t5 | Shadow map (comparison depth) |

Static samplers: s0 = linear wrap, s1 = comparison (shadow PCF). Unchanged.

### BRDF

Cook-Torrance microfacet specular BRDF:

```
f_specular = (D_GGX * G_Smith_Schlick * F_Schlick) / (4 * NdotV * NdotL + ε)
f_diffuse  = kD * albedo / π       where kD = (1 - F) * (1 - metallic)
F0         = lerp(0.04, base_color, metallic)
```

### Test

Dedicated smoke test `"DX12 renderer: PBR DamagedHelmet rotates 5 seconds, no debug errors"` in `engine/gfx/tests/test_smoke.cpp`:
- Loads `assets/models/DamagedHelmet.glb` (14 556 verts, 46 356 indices, all 5 PBR maps)
- Camera at `{0, 0, -2.5}` looking at origin — full helmet visible in 640×480 window
- White directional light (`{1,1,1}`, intensity 2.0) + white fill point light (`{1,1,1}`, intensity 0.8) from the left
- Model rotates via `set_model_transform(mat4_rotation_y(time))` each frame
- Runs 300 frames (fixed dt = 1/60 s, total ≈ 5 seconds)
- Asserts `debug_error_count() == 0`

Visual result: metallic visor reflection, roughness variation across surface, tangent-space normal map detail, AO darkening, emissive glow around face shield. All 3 CTest suites green, 14 gfx test cases, 0 debug errors.
