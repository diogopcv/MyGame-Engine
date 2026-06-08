# Phase 13 — Editor & tools framework

> Status: Stub — expand before starting this phase.

## Goal
An editor: scene editing with gizmos, reflection-driven property panels, an asset
browser, undo/redo, play-in-editor, and serialization-backed save/load — build
and save a playable scene, then play it without leaving the app.

## Scope (sketch)
Transform gizmos; reflection-driven property panels; asset browser; undo/redo;
play-in-editor; serialization-backed save/load; multi-window docking (ImGui).

## Tech decisions
To be decided when this phase begins. Note the standing "editor scope" question
(engine+editor vs engine-only) — design earlier phases as if the editor is
coming (reflection, serialization, undo, hot-reload).

## Critical tests
See `plan.md` (undo/redo round-trip; save → load yields an equal scene; gizmo
transform math; plus an editing-workflow review).

## Depends on
Phase 2 (reflection/serialization), Phase 4 (world), Phase 8 (UI), Phase 12 (tools).
