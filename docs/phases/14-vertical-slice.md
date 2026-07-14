# Phase 14 — Vertical-slice game & shipping hardening

> Status: Expanded **ahead of schedule** 2026-07-14 (full pre-expansion
> requested, deliberately overriding the template's just-in-time rule).
> This is the doc most exposed to drift — the game concept and budgets
> below are placeholders to react to, and must be re-decided against the
> engine that actually exists when this phase begins. Not started.

## Goal
Build a small but complete game with the engine as the forcing function,
then harden for shipping: CPU/GPU/memory optimization passes against
explicit budgets, Release/Shipping build configs, packaging, and the
carried-over graphics-polish pass — a shippable playable demo that runs on
a clean machine from a packaged build.

## Scope
- In scope: **the game** — a small, complete, third-person game reusing
  every prior phase (see Tech decisions for the candidate concept): menu →
  play → win/lose → menu loop, authored in the Phase 13 editor, cooked by
  the Phase 11 pipeline; **graphics polish** (carried from Phase 3, done
  here by design): soft shadows (PCF kernel widening or PCSS), ambient
  occlusion (SSAO), a post-processing stack (bloom + the existing tone
  mapping; vignette cheap optional), and OIT *only if* the slice's actual
  content exposes a transparency-sorting failure — not speculatively;
  **optimization passes** — profile-driven (Phase 12 tools) CPU, GPU, and
  memory passes against budgets fixed at phase start; **shipping
  hardening** — a Shipping build config (asserts compiled out, logging
  minimized, debug layers off, profiler compiled out), packaging (cooked
  content + executable into a distributable folder/zip), clean-machine
  verification (no VS/SDK installed), and a documentation pass;
  **stability** — the full-game smoke gate and a crash-free playtest bar.
- Out of scope: anything multiplayer in the slice itself (the Phase 10
  demo stands on its own; shipping netcode hardening — NAT, cheat,
  scale — is a different project); storefront/installer/auto-update
  (a zip that runs is the bar); console/other-platform ports; engine
  features invented *for* the game beyond glue (a needed feature is a
  gap in its home phase — fix it there, tracked, not hacked in here);
  marketing/audio-mastering polish beyond the listening pass.

## Tech decisions
- **Game concept (placeholder — re-decide at phase start, one-pager
  first):** "Fox Trials" — a third-person time-trial: the Phase 6 fox
  character runs a physics-built obstacle arena collecting tokens against
  the clock; Phase 9 AI chasers add pressure; Phase 7 audio for feedback +
  music; Phase 8 HUD (timer, tokens, menus). Chosen because it exercises
  every subsystem with near-zero *new* engine surface — the forcing
  function is integration, not features. Whatever the final concept is,
  it must hold that property.
- **Budgets are set at phase start and enforced by test** — placeholders:
  16.6 ms frame (60 fps) on the dev machine at 1080p, fixed memory cap
  (arenas sized once, zero heap growth in steady state — ADR-0004 makes
  this checkable), <5 s boot-to-menu. The perf test asserts frame-time
  percentiles over a scripted replay, not a hand-timed vibe.
- **Optimization is profile-first:** every pass starts from a Phase 12
  capture; no speculative rewrites. The likely suspects (draw-call
  batching, shadow-pass cost, mixer voice count) are hypotheses to
  verify, not a to-do list.
- **Shipping config:** a third CMake configuration (`Shipping`) — NDEBUG,
  `GE_ASSERT` compiled to nothing, log level ERROR-only, DX12 debug layer
  off, profiler macros empty, static CRT (or bundled redist — decide
  during Milestone 5 against the clean-machine test). Release stays the
  optimized-with-tools config for development.
- **Packaging:** cook → copy exe + cooked content + licenses (third-party
  attributions: CC-BY assets, stb, cgltf, ImGui, doctest) into a
  versioned folder, zipped. Loose cooked files unless load times argue
  for the Phase 11 open-question pack format.
- **Soft shadows / SSAO / post stack** land in `ge_gfx` as render-graph
  passes (the architecture they waited for); each gets a golden-image
  update, diffed deliberately per the golden rules.

## Design
The slice's game code lives in `game/` (a sibling of `sandbox/`,
application layer — the sandbox stays the engine test bed). Game systems
are plain C++ free functions over the world (ADR-0009), authored scenes
are Phase 13 blob assets, and boot goes menu-scene → arena-scene through
the Phase 11 runtime. The Phase 12 profiler stays wired in Release for
the optimization milestones and compiles out in Shipping.

```
game/ boot -> ge_res manifest -> menu scene (UI) -> arena scene
  fixed tick: input -> gameplay systems -> physics -> AI -> anim -> audio
  frame: renderer (shadow -> PBR -> SSAO -> post) -> HUD -> present
  win/lose -> results -> menu   (the complete loop is the point)
```

## Milestones
1. **Concept + graybox.** One-page design doc (committed alongside this
   file); budgets fixed and recorded; graybox arena in the editor; the
   full menu→play→result loop runs ugly but complete.
2. **Content + gameplay complete.** Real arena layout, tokens, chasers,
   timer, audio hooked, HUD final; playable start-to-finish; content
   locked except for tuning.
3. **Graphics polish.** Soft shadows, SSAO, post stack (bloom); OIT
   decision made against real content; goldens updated deliberately.
4. **Optimization to budget.** Captured baseline → CPU pass → GPU pass →
   memory pass; the scripted-replay perf test green on all budgets.
5. **Ship it.** Shipping config; packaging script; clean-machine
   verification; docs pass (README, how-to-run, attributions); final
   playtest sign-off. Phase DoD: the zip runs the whole game on a machine
   that has never seen the SDK.

## Test plan
Per plan.md's Phase 14 critical-test line:
- **Smoke (the gate):** packaged Shipping build on a clean machine —
  boot → scripted play of the full loop → quit; zero crashes, zero
  asserts (Release re-run for assert coverage), clean exit code. Plus the
  existing per-module suites staying green throughout — the slice must
  not rot the engine.
- **Perf:** a deterministic scripted replay (fixed-timestep input script)
  measured over N seconds: frame-time p50/p99 within budget, steady-state
  memory flat (arena high-water marks stable between minute 1 and minute
  5), boot time within budget. Runs in Release with profiler on; asserted
  numerically.
- **Golden image:** updated references for soft shadows/SSAO/post — each
  diff reviewed and justified in the commit, never regenerated to pass.
- **Unit:** gameplay logic that is deterministic and consequential —
  timer/score rules, win/lose conditions, token-collection edge cases
  (double-collect, collect-at-timeout). Proportionality applies: no tests
  for tuning values.
- **Manual:** playtest sign-off — completable, fair, readable, sounds
  right; recorded per plan.md.

## Open questions
- The game concept itself (the placeholder above is a proposal, not a
  decision — write the one-pager, decide, commit it).
- Static CRT vs bundled redist for Shipping — whichever survives the
  clean-machine test with less friction.
- Whether the perf budgets target the dev machine or a deliberately
  weaker reference machine — decide when setting budgets; affects
  Milestone 4's finish line.
- Does the slice ship the Phase 10 two-client demo as a menu item?
  Tempting, out of scope by default (see Scope) — revisit only if the
  slice lands early.

## Cross-phase watch items
- This phase is the final audit of every earlier deferral: cascaded
  shadow maps (Phases 4–6 watch item), job priorities (Phase 7), ORCA
  avoidance (Phase 9), BCn textures (Phase 11) — each gets a
  build-or-consciously-drop decision here, recorded in this doc as
  addenda when made.
- Gaps the game exposes in engine phases go back to their home module
  with a status-log entry — the slice's job is to *find* them, not to
  absorb hacks.

## Depends on
- All prior phases — by design; this phase exists to force their
  integration.
