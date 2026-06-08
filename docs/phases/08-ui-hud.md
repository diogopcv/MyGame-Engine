# Phase 8 — UI / HUD

> Status: Stub — expand before starting this phase.

## Goal
An in-game UI/HUD system: batched quad + text rendering, layout, input routing
and focus, core widgets, and HUD anchoring.

## Scope (sketch)
Font-atlas text + batched quads; layout; input routing/focus; panel/button/
slider/text-input widgets; HUD anchoring. Dear ImGui for debug/editor UI; the
game UI uses the engine's own system.

## Tech decisions
To be decided when this phase begins (immediate-mode vs retained for game UI).

## Critical tests
See `plan.md` (layout math, hit-testing/focus routing; plus a look/feel review).

## Depends on
Phase 3 (renderer), Phase 0/2 (input).
