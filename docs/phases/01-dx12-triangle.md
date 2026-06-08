# Phase 1 — First triangle (the DX12 gauntlet)

> Status: Stub — expand fully before starting. (Pre-seeded with the plan.)

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
- **Frames in flight:** double or triple buffering with per-frame command
  allocators and fence values (decide 2 vs 3 here).
- **Descriptor & binding model:** ADR-0006 — classic root-signature tables to
  start; bindless evaluated before Phase 3.
- **Resource upload:** simple upload heap + copy for Phase 1; a dedicated copy
  queue comes later.
- **Resource state:** explicit barriers now; the render graph automates them in
  Phase 3.
- **Shaders:** HLSL compiled offline with DXC via a CMake custom command; source
  committed, `.cso`/`.dxil` output git-ignored.
- **Module:** lives as `ge::gfx` (a subsystem). Keep it minimal and
  self-contained now; the clean RHI abstraction is Phase 3 work.

## Design
- `Window::native_handle()` (the `HWND` as `void*`) feeds swapchain creation —
  the only platform coupling.
- The math library supplies the MVP; mind HLSL matrix packing per ADR-0005.
- Keep a single `Renderer` facade for Phase 1; do not over-abstract before the
  render graph exists.

## Milestones (in-phase increments)
1. Device + swapchain + clear → a solid color on screen.
2. Triangle: root signature, PSO, vertex buffer, draw.
3. Textured quad: SRV, sampler, texture upload.
4. Constant buffer + depth + the spinning cube (MVP from math).

## Test plan
- Smoke: device init, present N frames, clean shutdown, zero DX12 debug-layer
  errors. (Build the smoke harness here.)
- Golden image: the reference cube within per-pixel tolerance. (Build the
  golden-image comparator here.)

## Open questions
- Bindless now or in Phase 3 (ADR-0006).
- Triple vs double buffering.
- Offline vs runtime shader compilation (leaning offline DXC).

## Depends on
- Phase 0: platform window + the math library.
