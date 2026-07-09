# ADR-0010 — Renderer draw submission & generalized RHI binding layout

> Status: Accepted
> Date: 2026-07-09

## Context

Through Phase 5 the renderer was a single-model demo facade: it owned exactly
one loaded `Model`, one `model_transform`, and one per-object constant buffer
per frame-in-flight.  Three structural limits followed:

1. **No multi-object rendering.** All draws shared one per-object CB, so a
   second submitted object would silently overwrite the first's constants
   (last-write-wins — the GPU reads mapped upload memory only after the CPU
   finishes recording).  The shadow and PBR passes already collided on this
   buffer and only rendered correctly by coincidence of layout.
2. **Frame indexing derived from a handle.** Per-frame CB slots were selected
   by `back_buffer().index % 2` — a `HandleTable` slot number, not a frame
   index.  It worked only because the two back buffers happened to occupy
   slots 0 and 1; any resize reshuffling slots could alias both frames onto
   one CB and race the GPU.
3. **Renderer-specific root signatures baked into the RHI.** A
   `RootLayout::{Default, PBR}` enum hardcoded two root-signature shapes in
   `rhi_dx12.cpp`; every new shader family would mean editing the RHI.
   Four identical root signatures were also created for the four PBR PSO
   variants.

Also: all vertex/index buffers lived on the UPLOAD heap (every draw streamed
geometry across PCIe), and resource uploads reset the frame command list,
which would corrupt recording if a resource were ever created mid-frame.

## Options considered

- **A — Retained render-world** (renderer owns persistent render proxies,
  world layer creates/destroys them): richer, but adds lifetime/sync
  machinery Phase 6 doesn't need yet, and belongs with the future asset
  system (Layer 2).
- **B — Immediate-mode draw list** (`submit(model, transform)` collected per
  frame, consumed by `end_frame()`): minimal API, no persistent state to keep
  in sync, natural fit for the existing "systems iterate the World and call
  the renderer" pattern.  Chosen.
- For per-draw constants: individual CBs per object (allocation churn,
  fixed caps) vs. **a per-frame upload ring** (one persistently-mapped buffer
  per frame in flight, 256-byte-aligned bump allocation, reset each frame —
  the standard D3D12 pattern, and it matches the engine's arena philosophy).
  Chosen the ring.
- For the RHI binding model: keep enum variants, full shader reflection, or
  **a `BindingLayout` count-based description** (`cbv_count` +
  `srv_table_count`) with an internal root-signature dedupe cache.  Reflection
  is the eventual destination (revisit alongside ADR-0006's bindless note);
  counts are policy-free, cover every current shader, and delete the layer
  violation today.  Chosen counts.

## Decision

1. **Submission API.** `Renderer::load_model()` returns a `ModelId` (up to 16
   concurrently loaded models, each with its own material CBs and lazily
   built PSO variants per sampler address mode).  Draws are submitted per
   frame as `submit(ModelId, mat4)` between `begin_frame()` and
   `end_frame()`; `end_frame()` builds the render graph (shadow + PBR passes
   over the draw list, reference cube when the list is empty), executes, and
   presents.  The Blinn-Phong fallback path (`model_vs/ps`) was retired — PBR
   is the model path; the cube remains the empty-scene/golden baseline.
2. **Per-draw constants from an upload ring.** All dynamic CBs (per-frame,
   per-object, shadow, cube) are bump-allocated from a per-frame-in-flight
   persistently-mapped ring (`UploadRing`, 1 MiB/frame) and bound by buffer +
   256-aligned offset via `set_constant_buffer(slot, buffer, offset)`.  Safe
   because `RhiDevice::begin_frame()` fences the frame slot; correct because
   every draw owns distinct GPU addresses.  `RhiDevice::frame_index()` is the
   only sanctioned way to select per-frame resources.
3. **Generalized binding layout.** `PipelineDesc` carries a `BindingLayout`
   (root CBVs `b0..bN-1` at slots `[0,N)`, then single-SRV tables `t0..tM-1`
   at slots `[N,N+M)`).  The RHI builds root signatures generically from the
   counts and dedupes them in a cache keyed by counts + static-sampler state.
4. **GPU-resident static geometry.** Non-CPU-visible buffers now live on the
   DEFAULT heap, uploaded once via a staging buffer on a dedicated upload
   command list (also used for texture uploads and readback, so resource
   creation never touches the frame command list).  Buffer state transitions
   ride D3D12's implicit COMMON promotion/decay.
5. **Renderer is backend-agnostic.** With shader-file lookup moved to
   `platform::executable_dir()`, `renderer.cpp` (renamed from
   `renderer_dx12.cpp`) contains no D3D12/Win32 usage; only `rhi_dx12.cpp`
   touches `<windows.h>`/`<d3d12.h>`.

## Consequences

- Many instances of many models render correctly in one frame; per-object
  data can never alias between draws or passes.  Phase 6 (skinned meshes)
  adds a shader family by describing its bindings, not by editing the RHI.
- The draw list is rebuilt every frame (no retained proxies); culling,
  sorting beyond transparency, and instancing (`DrawIndexedInstanced` with
  N > 1 / per-instance data) remain future work on top of this API.
- The ring is a hard per-frame budget (1 MiB ≈ 4k draws); overflow logs an
  error and drops the draw's constants.  Bump the constant if scenes outgrow it.
- Sampler state remains baked into PSOs/root signatures (per ADR-0006);
  per-address-mode PSO blocks are built lazily, so mixed-wrap-mode models
  coexist at the cost of up to 12 PBR PSOs.
- Uploads still flush the GPU once per resource; batch them when the asset
  system (Phase 11) arrives.  Shader reflection-driven layouts and bindless
  stay tied to ADR-0006's revisit trigger.
