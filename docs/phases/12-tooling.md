# Phase 12 — Profiling, debugging & developer tooling

> Status: Expanded **ahead of schedule** 2026-07-14 (full pre-expansion
> requested, deliberately overriding the template's just-in-time rule).
> Re-validate scope and tech decisions against the codebase when this phase
> actually begins — the Phase 2 seeds this grows from may have evolved by
> then. Not started.

## Goal
Grow the lightweight Phase 2 tools into a real developer suite: a CPU/GPU
profiler with frame capture, memory tracking with leak detection, crash
dumps, a mature in-engine console over the CVar system, debug draw, and
asset hot-reload — capture and inspect a frame's CPU/GPU timeline and
memory usage live from inside the engine.

## Scope
- In scope: **CPU profiler** — hierarchical zones (the existing
  `GE_PROFILE_SCOPE` grows begin/end nesting), per-thread ring buffers
  (job-system threads included), an in-engine flame/timeline view, and
  frame capture exported as Chrome-tracing JSON (`chrome://tracing` /
  Perfetto readable — a full offline viewer for free); **GPU profiler** —
  D3D12 timestamp query heaps in the RHI, calibrated to CPU time, zones
  around render-graph passes; **memory tracking** — a tracking layer on
  the `Allocator` interface + arena/pool stats (bytes, high-water,
  allocation counts by tag), a live view, and a Debug-build shutdown leak
  report; **crash handling** — an SEH unhandled-exception filter writing a
  minidump (`MiniDumpWriteDump`, platform layer); **console** — a
  drop-down in-engine console (Phase 8 text/quad path): log tail, command
  line with CVar get/set + autocomplete + history, `exec` of a startup
  config file; **debug draw** — immediate-mode `debug_line/box/sphere/
  axes/text3d` API batched through the renderer, cleared per frame;
  **asset hot-reload** — a platform file watcher
  (`ReadDirectoryChangesW`) driving Phase 11's `AssetCache::reload`
  (shaders and textures first, the two with the fastest iteration
  payoff).
- Out of scope (deferred): code hot-reload (explicitly deferred by
  ADR-0009 — unchanged); a standalone external profiler app (the tracing
  JSON export covers deep dives); sampling/instruction-level profiling
  (zones are the engine-appropriate granularity); remote/over-network
  tooling; full crash *reporting* backend (dump lands on disk; uploading
  is a shipping concern, Phase 14 at the earliest); allocation
  callstack capture (tags + counts first — stacks only if a real leak
  hunt needs them).

## Module layout
No new module — this phase deepens existing ones, keeping each piece in
its layer:

- `engine/core`: `profiler.h` upgrade (zones, per-thread rings, capture +
  JSON export); `memtrack.h` (tracking `Allocator` wrapper + global
  registry of arenas/pools with names); console *logic* (command parse,
  CVar binding, history) as testable core code, UI-free.
- `engine/platform`: `crash_win32.cpp` (SEH filter + minidump);
  `file_watch.h` + `file_watch_win32.cpp` (directory watch → queued
  change events, `void*` handle, poll-based drain — no callbacks across
  the platform boundary).
- `engine/gfx`: RHI timestamp queries (`begin_zone/end_zone` on the
  command list, query-heap resolve to a readback buffer, calibration via
  `GetClockCalibration`); render-graph auto-zones per pass; debug-draw
  batcher (line list pipeline, per-frame vertices from the upload ring).
- `engine/ui` / app glue: the console overlay + profiler/memory views are
  widgets (or ImGui panels, both acceptable — they're debug UI) assembled
  at the app layer.

## Tech decisions
- **Chrome-tracing JSON as the capture format** — one exporter instead of
  a custom viewer; the in-engine view stays a simple current-frame
  timeline, the heavy analysis happens in Perfetto.
- **Per-thread zone rings, merge on capture.** Zones record begin/end
  ticks + name (`StringId`) into a thread-local ring — no locks on the
  hot path; the capture pass walks all rings once.
- **GPU zones via timestamp pairs per render-graph pass**, resolved one
  frame late (per-frame-in-flight readback, same pattern as ADR-0010's
  ring) — no stalls.
- **Memory tracking rides the existing `Allocator` seam** (ADR-0004
  pays off): a wrapping allocator counts and tags; arenas/pools also
  self-report (they know their high-water marks natively). The Debug
  leak report asserts on non-empty tracked state at shutdown — that plus
  the existing "leak-clean" definition of done makes leaks a failing
  test, not a vibe.
- **Console logic lives in core, presentation at the app layer** — the
  parser/dispatcher is unit-testable headless; whether the overlay is
  `ge_ui` widgets or ImGui is a presentation detail decided when built.
- **Watcher events are polled, not called back** — the game thread drains
  a queue once per frame; debouncing (editors write files twice) lives in
  the drain.

## Design
```
CPU: GE_PROFILE_SCOPE -> thread-local ring
GPU: render-graph pass -> timestamp pair -> readback (frame+1) -> calibrated
capture key -> freeze rings + gpu zones -> merge -> frame.json (chrome tracing)
                                        -> in-engine timeline widget

memory: Allocator wrapper + arena registry -> live stats view
        Debug shutdown -> leak report -> assert if non-empty

crash: SEH filter -> minidump + log flush -> exit

console: ` key -> overlay: log tail + input
         parse -> CVar get/set | command registry -> autocomplete from both

hot-reload: watcher queue -> drain per frame -> debounce -> cache.reload(guid)
            -> shader/texture from_blob swap under the existing handle
```

## Milestones
1. **CPU profiler + capture.** Zones, per-thread rings, JSON export; the
   sandbox frame renders as a sane flame graph in Perfetto (job-system
   workers visible).
2. **GPU timing.** RHI query heaps + calibration; per-pass zones on the
   render graph; shadow vs PBR pass costs visible; in-engine timeline
   showing both CPU and GPU rows.
3. **Memory + crash.** Tracking wrapper + arena registry + live view;
   shutdown leak report (with the injected-leak test proving it); SEH
   minidump writer, verified by a deliberate test crash.
4. **Console + debug draw.** Core console logic + overlay; CVar
   autocomplete/history/exec; debug-draw API + batcher (retro-fit: Phase
   9's nav debug view moves onto it if a stopgap was built).
5. **Hot-reload + sign-off.** File watcher; shader + texture reload live
   in the sandbox; phase DoD — capture and inspect a frame live, edit a
   shader and see it without restarting.

## Test plan
Per plan.md's Phase 12 critical-test line:
- **Unit (profiler):** a zone wrapping a known `sleep(50ms)` reports a
  duration within tolerance; nesting reconstructs the correct parent
  hierarchy; per-thread rings survive wraparound without corrupting zone
  pairing; JSON export is well-formed and event-count-exact for a known
  zone set.
- **Unit (memory tracker):** an injected leak (tracked alloc, no free) is
  detected and named in the shutdown report; a balanced alloc/free set
  reports clean; high-water marks match a hand-computed sequence.
- **Unit (console/CVar):** parse/set round-trip for every CVar type;
  autocomplete prefix results; malformed input rejected with an error, no
  state change; `exec` applies a config file in order.
- **Unit (watcher):** touch/rename/delete under a temp dir produce the
  expected drained events; debounce collapses the double-write pattern.
- **Smoke:** capture during the gfx smoke test (profiler on, N frames,
  export succeeds, 0 debug-layer errors); crash-writer test spawns a
  child process that faults and asserts a dump file exists.
- **Manual:** timeline inspection sanity (GPU pass costs plausible),
  console feel, live shader-edit demonstration.

## Open questions
- In-engine views: `ge_ui` widgets vs ImGui panels — decide at Milestone
  2 based on how far Phase 8's widget set actually goes (either satisfies
  the DoD; ImGui is the likely pragmatic answer for dense tool UI).
- Whether GPU zone granularity stays per-pass or goes per-draw — per-pass
  until a real optimization question needs finer grain.
- Minidump size policy (mini vs full memory) — start mini + log flush;
  revisit if dumps prove undiagnosable.

## Cross-phase watch items
- Phase 13's editor embeds all of these views (console, profiler, memory)
  in docked panels — keep each view's data source presentation-agnostic.
- Phase 14's optimization pass is this phase's customer: budgets get
  enforced with these numbers. If the profiler can't answer "what got
  slower," fix that here, not there.

## Depends on
- Phase 2 (the profiler/CVar/allocator seeds this grows).
- Phase 3 + ADR-0010 (render graph for pass zones, readback plumbing).
- Phase 8 (text/quad rendering for overlays; or ImGui).
- Phase 11 (`AssetCache::reload` — hot reload's runtime half).
