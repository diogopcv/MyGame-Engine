# Phase 14 — Vertical-slice game & shipping hardening

> Status: Stub — expand before starting this phase.

## Goal
Build a small but complete game with the engine as the forcing function, then
harden for shipping: optimization passes, Release/Shipping configs, packaging,
and documentation — a shippable demo that runs on a clean machine from a
packaged build.

## Scope (sketch)
A small complete game; CPU/GPU/memory optimization passes; Release/Shipping build
configs; packaging/distribution; documentation.

Carried over from Phase 3 (gfx) as a graphics-polish pass, done here rather
than earlier because none of the intervening gameplay phases depend on it:
- Soft shadows, ambient occlusion (tone mapping already landed with the PBR
  addendum).
- Post-processing stack.
- Order-independent transparency (OIT) — only if the actual vertical-slice
  scene exposes a transparency-sorting failure the existing back-to-front sort
  can't handle; not built speculatively.

See `docs/phases/03-renderer.md` Out of scope notes for the origin of each item.

## Tech decisions
To be decided when this phase begins (perf budgets, packaging format).

## Critical tests
See `plan.md` (full-game boot → play → quit smoke on a clean machine; frame-time
and memory budgets met; a playtest sign-off).

## Depends on
All prior phases.
