# ADR-0006 — DX12 descriptor & binding model

> Status: Proposed (decide during Phase 1, refine before Phase 3)
> Date: (set on commit)

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
TBD. Leaning: start with a classic root-signature + descriptor-table model in
Phase 1 to minimize moving parts, then evaluate bindless before the renderer
proper (Phase 3), where its benefits matter most. Record the final choice here.

## Consequences
To be filled when decided. Whatever is chosen, descriptor-heap lifetime and
per-frame staging must be designed alongside the frames-in-flight model.
