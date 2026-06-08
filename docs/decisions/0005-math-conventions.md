# ADR-0005 — Math conventions

> Status: Accepted
> Date: (set on commit)

## Context
The math library must be internally consistent and align with Direct3D 12 / HLSL
defaults, so we are not constantly transposing matrices or flipping axes at the
GPU boundary. Convention bugs are subtle and expensive; pick once, document
loudly.

## Options considered
- **Match D3D/HLSL** (left-handed, [0,1] depth) — fewest conversions at the GPU
  boundary.
- **Match a RH/OpenGL-style convention** — more familiar to some, but adds
  conversions for a DX12-only engine.

## Decision
- **Coordinate system:** left-handed; +X right, +Y up, +Z forward.
- **Angles:** radians.
- **Matrices:** row-major storage, row-vector convention. Vectors are rows;
  transform a point as `p' = p * M`. Composite transforms read left-to-right:
  `p' = p * S * R * T` (scale, then rotate, then translate).
- **Clip-space depth:** [0, 1] (D3D convention). Reverse-Z is an optional later
  refinement, not now.
- **Rotations:** quaternions are the canonical rotation representation; matrices
  are derived for the GPU.
- **HLSL interop:** account for HLSL's default column-major packing (use the
  `row_major` qualifier or transpose on upload — decide and document in the
  Phase 1 doc).
- **Implementation:** scalar first, in headers; SIMD only if profiling later
  demands it (no premature SIMD).

## Consequences
- Minimal conversion work between CPU math and DX12/HLSL.
- The chosen multiply order and handedness must be honored everywhere (camera,
  physics, animation) — it is documented here and is the reference.
- Switching conventions later is costly; treat this as sticky but revisitable if
  a hard reason emerges (e.g. adopting reverse-Z).
