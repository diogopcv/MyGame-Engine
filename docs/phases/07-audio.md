# Phase 7 — Audio

> Status: Stub — expand before starting this phase.

## Goal
An audio engine: a mixing graph, 3D spatialization with distance attenuation,
streaming vs in-memory sounds, buses/groups, and basic DSP.

## Scope (sketch)
Mixing graph; spatialization/attenuation; streaming; master/SFX/music buses;
volume/pitch/reverb-send DSP. Backend from scratch (WASAPI) or via miniaudio.

## Tech decisions
To be decided when this phase begins (notably: WASAPI from scratch vs miniaudio).

## Critical tests
See `plan.md` (mixing/attenuation math, bus gain, buffer integrity; plus a
listening pass).

## Depends on
Phase 2 (jobs/resources), Phase 4 (world, for source positions).
