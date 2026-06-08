# Phase 6 — Animation

> Status: Stub — expand before starting this phase.

## Goal
Skeletal animation with GPU skinning, clip sampling, blend trees, and animation
state machines — a skinned character that smoothly blends idle/walk/run driven by
gameplay state.

## Scope (sketch)
Skeleton + skinning; clip sampling/looping; blend trees; state machines; root
motion; inverse kinematics later.

## Tech decisions
To be decided when this phase begins.

## Critical tests
See the "Critical tests by phase" table in `plan.md` (clip sampling, blend
weights, pose math, root motion; plus a manual blend-feel pass).

## Depends on
Phase 3 (renderer/skinning), Phase 4 (world), Phase 0 (math: quaternions/slerp).
