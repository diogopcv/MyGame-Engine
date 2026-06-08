# Phase 12 — Profiling, debugging & developer tooling

> Status: Stub — expand before starting this phase.

## Goal
Grow the lightweight Phase 2 tools into a full developer suite: a CPU/GPU
profiler with frame capture, memory tracking and leak detection, crash
reporting, a mature console/CVar system, debug draw, and code/asset hot-reload.

## Scope (sketch)
CPU/GPU profiler + frame capture; memory tracking + leak detection; crash
reporting/dumps; mature console + CVars/config; debug draw; hot-reload.

## Tech decisions
To be decided when this phase begins.

## Critical tests
See `plan.md` (profiler timings track known sleeps; memory tracker detects an
injected leak; CVar parse/set round-trip).

## Depends on
Phase 2 (the lightweight tool seeds), Phase 1/3 (GPU timing).
