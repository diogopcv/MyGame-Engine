# Phase 1 — First triangle (the DX12 gauntlet)

> Status: Complete — Milestones 1-4 done (device/swapchain/clear; triangle
> via root signature/PSO/vertex buffer; textured quad via index buffer,
> texture upload, SRV heap, and a static sampler; spinning textured cube via
> a root CBV constant buffer, depth buffer, and a golden-image test). DoD met:
> a spinning, textured cube at a stable, vsynced frame rate.

## Goal
A minimal DirectX 12 render path culminating in a spinning, textured cube driven
by an MVP matrix from the math library, presented at a stable vsynced frame rate.
This is the largest early time sink; budget for DX12 boilerplate.

## Scope
- In scope: adapter/device creation; flip-model swapchain; command
  queue/allocators/lists; fence-based frame synchronization; descriptor heaps;
  root signature; pipeline state object (PSO); vertex/index buffers + upload;
  a constant buffer for the MVP; depth buffer; one texture + sampler; the draw;
  vsynced present. Debug layer + GPU-based validation + DRED on in Debug.
- Out of scope: render/frame graph, materials system, lighting, shadows (Phase 3).

## Tech decisions
- **Frames in flight: 2 (double buffering).** Two back buffers, two per-frame
  command allocators, two fence values. Simplest correct model for Phase 1;
  triple buffering can be evaluated in Phase 3 if latency/throughput profiling
  calls for it. `kFrameCount = 2` is a single constant in `renderer_dx12.cpp`,
  so bumping it later is a small change.
- **Descriptor & binding model:** ADR-0006 — **decided**: classic root
  signature (root descriptors + descriptor tables), descriptors staged into a
  shader-visible CBV/SRV/UAV heap. Re-evaluate bindless before Phase 3.
- **Resource upload:** simple upload heap + `CopyResource`/mapped-pointer copy
  for Phase 1 (vertex buffer, texture). A dedicated copy queue comes later.
- **Resource state:** explicit `ResourceBarrier` calls now; the render graph
  automates them in Phase 3. Back buffers transition
  `PRESENT -> RENDER_TARGET -> PRESENT` each frame.
- **Shaders:** HLSL compiled offline with DXC via a CMake custom command;
  source committed under `engine/gfx/shaders/`, compiled `.cso` output goes to
  `build/` and is git-ignored.
- **Module:** lives as `ge::gfx` (Layer 3 subsystem), library target `ge_gfx`.
  Depends only on `ge_core` (and transitively `ge_platform`). Keep a single
  `Renderer` facade for Phase 1; do not over-abstract before the render graph
  exists (Phase 3).
- **Debug tooling:** in Debug builds, enable the D3D12 debug layer
  (`ID3D12Debug::EnableDebugLayer`), GPU-based validation
  (`ID3D12Debug1::SetEnableGPUBasedValidation`), and DRED
  (`D3D12DeviceRemovedExtendedDataSettings1`, auto breadcrumbs + page fault
  reporting). `RendererDesc::enable_debug_layer` lets the smoke test and
  Release builds turn this off.

## Design

### Module layout
```
engine/gfx/
├─ include/gfx/renderer.h   public Renderer facade (no DX12/<windows.h> types)
├─ src/renderer_dx12.cpp    all D3D12/DXGI code, pImpl-hidden
├─ shaders/                 HLSL sources (added in Milestone 2+)
├─ tests/                   smoke test(s), registered with CTest
└─ CMakeLists.txt
```
The public header exposes only `RendererDesc`, `Renderer`, and primitive types
— mirroring the `Window` pImpl pattern. `<d3d12.h>`/`<dxgi1_6.h>` stay inside
`renderer_dx12.cpp`.

### Renderer facade (Milestone 1 surface)
```cpp
struct RendererDesc {
    void*         hwnd               = nullptr; // from Window::native_handle()
    std::uint32_t width              = 1280;
    std::uint32_t height             = 720;
    bool          enable_debug_layer = true;    // Debug builds: true
    bool          vsync              = true;
};

class Renderer {
public:
    bool init(const RendererDesc& desc);
    void shutdown();

    void begin_frame(); // wait for this frame slot, reset allocator/list, set
                         // back buffer as render target, transition + clear
    void end_frame();   // transition back to PRESENT, close+execute, Present,
                         // signal fence

    void resize(std::uint32_t width, std::uint32_t height);

    void set_clear_color(float r, float g, float b, float a);
};
```

### Per-frame resources (`kFrameCount = 2`)
- `ID3D12Resource* back_buffers[kFrameCount]` — from `IDXGISwapChain3::GetBuffer`.
- `ID3D12CommandAllocator* allocators[kFrameCount]` — one per in-flight frame,
  reset only after the GPU has finished that frame's prior work.
- `u64 fence_values[kFrameCount]` — the fence value to wait on before reusing
  slot N.
- A single `ID3D12GraphicsCommandList`, reset against the current frame's
  allocator each `begin_frame`.

### Initialization order
1. (Debug) Enable `ID3D12Debug` + GPU-based validation; enable DRED settings
   *before* device creation.
2. Create `IDXGIFactory6` (with `DXGI_CREATE_FACTORY_DEBUG` in Debug).
3. Enumerate adapters via `EnumAdapterByGpuPreference` (high-performance),
   skip software adapters, create `ID3D12Device` (feature level 11.0 floor).
4. (Debug) Query `ID3D12InfoQueue`, break on CORRUPTION/ERROR, push a deny
   filter for known-noisy IDs if needed.
5. Create the direct command queue, `kFrameCount` command allocators, one
   graphics command list (created in a closed state).
6. Create the flip-model swapchain (`DXGI_SWAP_EFFECT_FLIP_DISCARD`,
   `DXGI_FORMAT_R8G8B8A8_UNORM`, `kFrameCount` buffers) via
   `CreateSwapChainForHwnd`, then `QueryInterface` to `IDXGISwapChain3`.
7. Create an RTV descriptor heap (`kFrameCount` descriptors, non-shader-visible)
   and one RTV per back buffer.
8. Create the fence (initial value 0) + a Win32 event handle for CPU waits.

### Per-frame flow
`begin_frame()`:
1. `frame_index = swapchain->GetCurrentBackBufferIndex()`.
2. If `fence->GetCompletedValue() < fence_values[frame_index]`, `SetEventOnCompletion` + `WaitForSingleObject`.
3. Reset `allocators[frame_index]`, then reset the command list against it.
4. Barrier: back buffer `PRESENT -> RENDER_TARGET`.
5. `OMSetRenderTargets` to the back buffer's RTV; `ClearRenderTargetView` with
   the configured clear color.

`end_frame()`:
1. Barrier: back buffer `RENDER_TARGET -> PRESENT`.
2. Close the command list, `ExecuteCommandLists`.
3. `swapchain->Present(vsync ? 1 : 0, 0)`.
4. `Signal` the queue, store the new fence value in `fence_values[frame_index]`.

### Shutdown
Flush the GPU (signal + wait on the fence for the last submitted value) before
releasing any resource. ComPtr release order then follows reverse-creation via
member destruction order in the impl struct.

### Resize
Flush the GPU, release back-buffer references, `ResizeBuffers`, recreate RTVs.
Triggered from `main.cpp` on `WM_SIZE` (Milestone 1 wires the call but the
sandbox loop doesn't yet resize dynamically — window is fixed-size for now).
The depth buffer (Milestone 4) is released and recreated alongside the RTVs.

### Milestone 4 design

**Geometry.** A unit cube as 24 vertices (4 per face, so each face gets its
own UVs spanning `[0,1]^2`) using the existing `Vertex { position[3]; uv[2]; }`
layout, plus a 36-entry `R16_UINT` index buffer (2 triangles/face). Reuses the
Milestone 3 checkerboard texture, root signature SRV table, and sampler.

**Constant buffer (MVP).** Root signature gains a second root parameter: a
root CBV at `b0` (`D3D12_ROOT_PARAMETER_TYPE_CBV`, vertex-shader visibility)
holding a single `float4x4 mvp`. `kFrameCount` upload-heap buffers (256-byte
aligned, persistently mapped) back this — one per in-flight frame, written
via `memcpy` each `begin_frame` so the GPU never reads a buffer the CPU is
still writing. The HLSL cbuffer declares `row_major float4x4 mvp` so
`ge::mat4`'s row-major storage can be uploaded byte-for-byte and
`mul(float4(pos,1), mvp)` matches the engine's row-vector convention
(`p' = p * M`).

**MVP composition.** `model = rotation_x(t * 0.5) * rotation_y(t * 0.8)`
(`t` = accumulated wall time in seconds, driving the spin). `view =
mat4_look_at_lh(eye=(0,1.5,-3), target=origin, up=+Y)`. `proj =
mat4_perspective_lh(fov_y=60deg, aspect=width/height, near=0.1, far=100)`.
`mvp = model * view * proj`, matching the left-to-right composite convention
(ADR-0005).

**Time.** `Renderer::begin_frame(f32 dt)` accumulates `total_time += dt` and
derives the model matrix from it. The smoke test and sandbox pass real frame
time; the golden-image test passes a fixed `dt = 1/60` for a fixed frame
count so the captured frame is fully deterministic.

**Depth buffer.** A single `DXGI_FORMAT_D32_FLOAT` resource (default heap,
`D3D12_RESOURCE_STATE_DEPTH_WRITE`, optimized clear value depth=1.0), one DSV
in a dedicated non-shader-visible DSV heap, sized to the swapchain and
recreated on `resize`. PSO: `DepthEnable = TRUE`, `DepthFunc =
D3D12_COMPARISON_FUNC_LESS`, `DepthWriteMask = ALL`, `DSVFormat =
DXGI_FORMAT_D32_FLOAT`. `begin_frame` clears the DSV (depth=1.0) alongside the
RTV and binds both via `OMSetRenderTargets`.

**Golden image.** `Renderer::capture_back_buffer(...)` transitions the current
back buffer `PRESENT -> COPY_SOURCE`, copies it into a readback buffer sized
via `GetCopyableFootprints`, transitions back to `PRESENT`, flushes, and maps
out a tightly-packed RGBA8 image. `stb_image` + `stb_image_write` are vendored
under `third_party/stb/` (decode for comparison, encode for the captured
frame/regenerated baseline — both solved, non-core problems per the
third-party policy). The test runs 90 frames at a fixed `dt = 1/60`
(`total_time = 1.5s` at capture), captures, and compares against
`engine/gfx/tests/golden/cube.png` with a small per-channel tolerance. Setting
`GE_GOLDEN_UPDATE=1` writes the captured frame as the new baseline instead of
comparing — used deliberately, never as part of a normal test run.

## Milestones (in-phase increments)
1. **Done.** Device + swapchain + clear → a solid color on screen.
   `ge_gfx` module, `Renderer::init/shutdown/begin_frame/end_frame`, sandbox
   clears to a color every frame, smoke test boots/runs N frames/shuts down
   cleanly with zero debug-layer errors.
2. **Done.** Triangle: root signature, PSO, vertex buffer, draw.
   `engine/gfx/shaders/triangle_{vs,ps}.hlsl` compiled offline to `.cso` via
   DXC (CMake custom command, output next to the executable under
   `shaders/`); empty root signature (`ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT`
   only); PSO with a position+color input layout, no depth/blend, culling off;
   a single static triangle in an upload-heap vertex buffer, drawn every frame
   in `begin_frame()` after the clear.
3. **Done.** Textured quad: SRV, sampler, texture upload.
   `engine/gfx/shaders/textured_quad_{vs,ps}.hlsl` (position+UV input layout,
   `Texture2D`/`SamplerState` in the pixel shader); root signature extended
   with a one-SRV descriptor table (t0, `D3D12_SHADER_VISIBILITY_PIXEL`) and
   a static point/wrap sampler (s0); a unit quad as 4 vertices + a
   `DXGI_FORMAT_R16_UINT` index buffer (two triangles); a small procedural
   4x4 checkerboard `R8G8B8A8_UNORM` texture uploaded via an upload-heap
   staging buffer + `CopyTextureRegion`, transitioned to
   `PIXEL_SHADER_RESOURCE`, with its SRV in a shader-visible CBV/SRV/UAV heap.
   Smoke test green (60 frames, zero debug-layer errors).
4. **Done.** Constant buffer + depth + the spinning cube (MVP from math).
   Root signature gained a root CBV (b0, vertex-visible) backing
   `kFrameCount` persistently-mapped per-frame upload buffers; cube geometry
   (24 vertices/36 indices) reuses the checkerboard texture; a D32_FLOAT
   depth buffer (DSV heap, recreated on resize) with depth test/write enabled
   in the PSO; `cube_{vs,ps}.hlsl` apply `mvp = model(t) * view * proj` via
   `mul(float4(pos,1), mvp)` (row-major cbuffer matches `ge::mat4` storage).
   `Renderer::begin_frame(dt)` accumulates time for the spin.
   `Renderer::capture_back_buffer()` + vendored stb_image/stb_image_write
   back a golden-image CTest (90 frames @ fixed dt, compared to
   `engine/gfx/tests/golden/cube.png`). Smoke + golden tests green (zero
   debug-layer errors).

## Test plan
- **Smoke** (`engine/gfx/tests/`, CTest): create a window (can stay off-screen
  size-wise but must be a real `HWND` for the swapchain), `Renderer::init`,
  run a fixed N (e.g. 60) frames of `begin_frame`/`end_frame`, `shutdown`,
  destroy the window. Assert `init` succeeds and, in Debug, that the
  `ID3D12InfoQueue` message count for ERROR/CORRUPTION severity is zero after
  teardown.
- **Golden image:** deferred to Milestone 4 (the cube) — Milestone 1 has no
  geometry to compare, just a clear color, which the smoke test's
  zero-debug-error check already covers.

## Open questions
- ~~Bindless now or in Phase 3~~ — resolved: classic root signature for now
  (ADR-0006).
- ~~Triple vs double buffering~~ — resolved: double buffering (`kFrameCount = 2`).
- ~~Offline vs runtime shader compilation~~ — resolved: offline DXC via a
  CMake custom command per shader (`engine/gfx/CMakeLists.txt`), `.cso`
  output placed next to the executable under `shaders/`.

## Depends on
- Phase 0: platform window + the math library.
