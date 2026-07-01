# Phase 11 — Asset pipeline & content cooker (offline)

> Status: Stub — expand before starting this phase.

## Goal
A robust offline content pipeline: importers for all content types, an asset
build graph with dependency tracking and incremental/parallel cooking, and
conversion to engine-native binary — one command cooks a content folder,
rebuilding only what changed.

## Scope (sketch)
Importers per content type; asset build graph + dependency tracking; incremental
+ parallel cooking; engine-native binary output; stable asset GUIDs/references;
validation. Pairs with the runtime resource system from Phases 2–3.

Carried over from Phase 3 (gfx), documented as a remaining gap in the PBR
addendum: **HDR-cubemap IBL prefiltering** — equirect/cubemap import, then an
offline prefilter bake to radiance mips + irradiance map + BRDF LUT. This is
cooker work by nature (offline, cacheable, depends on the build graph this
phase builds), so it wasn't buildable earlier. See
`docs/phases/03-renderer.md` Addendum 2.

## Tech decisions
To be decided when this phase begins.

## Critical tests
See `plan.md` (cook idempotency; incremental rebuild touches only changed assets;
reference/GUID resolution; graceful corrupt-input handling).

## Depends on
Phases 2–3 (runtime resource system), and each subsystem's importer needs.
