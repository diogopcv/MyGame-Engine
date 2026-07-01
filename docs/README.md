# Documentation map

How the project's planning and design docs fit together. Read this once, then
follow the upkeep rules at the bottom.

## What lives where

- **`plan.md`** (repo root) — the high-level roadmap and **source of truth for
  scope and status**. Deliberately short and scannable; read at the start of
  every session.
- **`CLAUDE.md`** (repo root) — the working agreement: rules, conventions, the
  definition of done. Read in full before changing code.
- **`docs/phases/NN-name.md`** — one design doc per phase. Holds the depth:
  scope, concrete tech decisions, key data structures, module APIs, algorithms,
  in-phase milestones, test plan, open questions. **Expanded just-in-time**,
  right before the phase starts — far-future phases are intentionally stubs.
- **`docs/decisions/NNNN-title.md`** — Architecture Decision Records (ADRs):
  append-only, one per cross-cutting decision, capturing the rationale you will
  want months later. Indexed in `docs/decisions/README.md`.

## Why it is split this way

Cadence matches consumption. `plan.md` and `CLAUDE.md` are read every session and
must stay light. Phase docs are volatile and read only while working that phase.
ADRs are durable references read occasionally. Detail that would bloat `plan.md`
and rot goes into phase docs; rationale that must survive goes into ADRs. Each
decision lives in exactly one place — link to it, never copy it — so the docs
do not drift out of sync.

## Upkeep rules (for agents and humans)

1. Before starting a phase, open its `docs/phases/NN-*.md`. If it is still a
   stub, **expand it first** (scope, tech decisions, design, milestones, test
   plan), then write code.
2. When you make a cross-cutting decision — one that affects multiple modules or
   phases, or whose rationale you will want later — add an ADR from
   `docs/decisions/_TEMPLATE.md`, set its status, and link it from the phase doc
   and the `plan.md` decisions index.
3. Keep `plan.md` phases as light pointers; the detail belongs in the phase doc.
   Do not duplicate a decision into both — link instead.
4. Update statuses as work lands: the phase-doc header, the `plan.md` Status log
   and module checklist, and any ADR superseded by a newer one.
5. New phase docs and ADRs follow the templates and the `NN` / `NNNN` numbering.
