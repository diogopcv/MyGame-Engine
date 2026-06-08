# Phase 10 — Networking & multiplayer

> Status: Stub — expand before starting this phase.

## Goal
Authoritative-server multiplayer: transport, session management, state
replication, prediction, and reconciliation — two clients sharing a server
simulation with smooth, predicted movement.

## Scope (sketch)
Reliable/unreliable UDP transport; connection/session management;
reflection-driven replication; snapshot + delta compression; client-side
prediction + server reconciliation; lag compensation; RPCs.

## Tech decisions
To be decided when this phase begins. Replication leans on reflection (ADR-0007)
and the ECS (ADR-0008) — keep both replication-aware as they are built.

## Critical tests
See `plan.md` ((de)serialization + delta round-trip; packet-loss/out-of-order
handling; prediction/reconciliation convergence; client/server determinism).

## Depends on
Phase 2 (reflection/serialization), Phase 4 (ECS) — designed replication-aware.
