# ADR-0006 — DX12 descriptor & binding model

> Status: Accepted
> Date: 2026-06-09

## Context
How shaders access resources (CBVs/SRVs/UAVs/samplers) is one of the most
far-reaching DX12 decisions: it shapes the root signature, descriptor-heap
management, and how the eventual render graph binds resources.

## Options considered
- **Classic root signature** — root descriptors + descriptor tables, descriptors
  staged into a shader-visible heap per frame. Well-documented, simplest to get
  correct first.
- **Bindless** (SM6.6 `ResourceDescriptorHeap`, dynamic indexing) — scales to
  large scenes, simplifies binding, but needs SM6.6 / the Agility SDK and a
  descriptor-allocation strategy.

## Decision
Classic root signature + descriptor tables for Phase 1 (and likely through
Phase 2). A small CBV/SRV/UAV shader-visible heap holds per-frame descriptors
(starting with one CBV for the MVP matrix and one SRV + sampler for the
texture, per Phase 1 milestones 3-4). Root signature layout starts minimal
(one CBV root descriptor for the MVP, one descriptor table for the texture
SRV, one static sampler) and grows as new resource types are needed.

Bindless is deferred to a re-evaluation before Phase 3, once the renderer
proper and material system make the scaling benefits concrete and the team has
a feel for SM6.6 / Agility SDK requirements on the target hardware.

## Consequences
- Descriptor-heap lifetime is tied to the `kFrameCount = 2` frames-in-flight
  model (see `docs/phases/01-dx12-triangle.md`): per-frame descriptor ranges in
  the shader-visible heap, sized generously enough for Phase 1's small
  resource set.
- Root signature changes are expected each Phase 1 milestone (triangle ->
  textured quad -> cube); keep it centralized in one place in `renderer_dx12.cpp`
  so these additions are easy to track.
- Revisiting bindless before Phase 3 may require reworking descriptor
  management and root-signature binding call sites — acceptable cost for
  keeping Phase 1 simple.
