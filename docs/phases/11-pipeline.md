# Phase 11 — Asset pipeline & content cooker (offline)

> Status: Expanded **ahead of schedule** 2026-07-14 (full pre-expansion
> requested, deliberately overriding the template's just-in-time rule).
> Re-validate scope and tech decisions against the codebase when this phase
> actually begins — by then Phases 7–10 will have added audio/UI/net asset
> types this design only sketches. Not started.

## Goal
A robust offline content pipeline and the Layer 2 runtime to consume it:
importers for every content type the engine uses, an asset build graph with
dependency tracking and incremental, parallel cooking into engine-native
binary, stable GUID references, and validation — one command cooks a raw
content folder, rebuilding only what changed, and the sandbox loads cooked
assets through a real resource system.

## Scope
- In scope: **runtime** — the long-empty Layer 2: new `engine/resources`
  module (`ge_res`) with GUID-keyed asset handles, refcounted load/unload,
  async loads on the job system, and a type-tagged blob container format;
  **offline** — a `cooker` command-line tool (application layer) with
  per-type importers (glTF meshes/skins/clips, PNG/JPG textures, WAV/OGG
  audio, TTF font atlases — reusing the parsers the subsystems already
  vendored), a build graph with dependency tracking (content-hash based),
  incremental + parallel cooking (job system), a manifest mapping GUID →
  cooked artifact, `.meta` sidecar files holding each source asset's
  stable GUID, deterministic cook output, and validation with graceful
  corrupt-input handling; the carried-over **HDR-cubemap IBL prefilter
  bake** (equirect → radiance mips + irradiance map + BRDF LUT) as a cooker
  step; sandbox switched to load cooked content.
- Out of scope (deferred): asset streaming beyond load-on-demand (no
  open-world residency budgets yet); hot reload's *file watcher* (Phase 12
  builds the watcher; this phase ships the runtime reload entry point it
  will call); texture block compression (BCn) and mip generation beyond
  what IBL needs (record as a follow-up — cooked textures start as raw
  RGBA + mips); asset versioning/migration tooling (a format version int
  that hard-rejects is enough now); a GUI asset browser (Phase 13);
  network/cloud cache; audio transcoding (cooked audio embeds the source
  OGG/decoded WAV as-is).

## Module layout
New `engine/resources` module (`ge_res`), **Layer 2** — above core, below
subsystems, exactly the slot the architecture reserved. Depends on
`ge_core` + `platform` (file I/O) only. Crucially it knows *no* subsystem
types: it stores and hands out **bytes + type tags**; each subsystem
registers how to make its thing from a blob.

- `include/resources/asset_id.h` — `AssetGuid` (128-bit, random once,
  persisted in `.meta`); `AssetType` (fourcc-style tag).
- `include/resources/asset_registry.h` + `src/…` — manifest load (GUID →
  {type, path/offset, size, dependency GUIDs}), lookup, dependency
  enumeration.
- `include/resources/asset_cache.h` + `src/…` — refcounted handles
  (`HandleTable`), synchronous + job-based async blob loads,
  `reload(guid)` entry point (the hook Phase 12's watcher will call),
  LIFO-safe unload.
- `include/resources/blob.h` — the container format: header (magic,
  format version, type tag, GUID, payload size/hash) + payload. Subsystem
  loaders gain `*_from_blob(payload)` entry points (`ge_gfx`, `ge_anim`,
  `ge_audio` parse their own payloads — Layer 3 depending on Layer 2,
  downward and legal).

New `tools/cooker/` (application layer, links whatever it needs):
- `cooker/main.cpp` — CLI: `cooker <content_dir> <out_dir> [--force]`.
- `cooker/importers/*.cpp` — one per source type, emitting blob payloads
  **by reusing the runtime import code** (cgltf/stb paths in gfx/anim)
  wherever it exists, so cooked output and dev-time direct loads can't
  drift apart.
- `cooker/build_graph.cpp` — scan sources + `.meta`, build the dependency
  DAG (a .gltf depends on its .png/.bin), content-hash inputs (FNV-1a
  over bytes + importer version + import settings), compare against the
  previous manifest, cook only dirty nodes, farm independent nodes to the
  job system.
- `cooker/ibl_bake.cpp` — the Phase 3 carried item: equirect HDR →
  cubemap → GGX-prefiltered radiance mips + irradiance SH/map + BRDF LUT
  (CPU-side bake; deterministic, cacheable — exactly why it waited for
  this phase). `ge_gfx` gains the runtime path to consume the baked IBL
  set, replacing the analytic-environment stopgap from Addendum 2.

## Tech decisions
- **GUIDs in `.meta` sidecars, committed to the repo.** Generated once
  (random 128-bit) when an asset is first seen; renames keep identity
  because the `.meta` moves with the file. References between assets are
  GUIDs, never paths, from the moment they enter the manifest.
- **Content-hash incremental, not timestamps.** Hash(source bytes,
  importer version, settings) — deterministic across clones and immune to
  clock skew; a `--force` flag bypasses. Cook output itself must be
  deterministic (stable ordering everywhere) so idempotency is testable.
- **Bytes-with-type-tags at Layer 2.** `ge_res` never includes subsystem
  headers; subsystems parse their own payloads. This is what keeps the
  layering clean and is the phase's likely ADR (record: Layer 2 contract +
  blob/manifest format + GUID scheme).
- **One importer codebase for cooker and dev-time loads.** The existing
  direct-from-glTF paths stay (tests and tools use them); the cooker calls
  the same import code and serializes the result. Divergence between "what
  the cooker makes" and "what the engine loads" is the classic pipeline
  bug; sharing the code is the structural defense.
- **Parallel cooking via the Phase 2 job system** — the DAG's ready set
  fans out; the manifest write is the single join point.
- **Corrupt input is data, not a bug:** importers return errors (golden
  rule: validate at module boundaries, no asserts on external data);
  the cook reports and continues, failing the overall run at the end.

## Design
```
cook: scan content/ -> ensure .meta (mint GUID if new)
      -> build DAG (source -> source deps via importer probe)
      -> hash inputs; diff vs previous manifest -> dirty set
      -> job-system fan-out: importer -> blob(s) -> out_dir
      -> write manifest (GUID -> artifact, deps, hashes)  [deterministic order]

runtime: AssetRegistry::load(manifest)
         cache.request(guid) -> ref++ | async job: read blob -> validate
         -> subsystem from_blob() at the call site -> handle
         cache.reload(guid): re-read + notify registered on-reload hook
```

## Milestones
1. **Blob + GUID + manifest + one type end-to-end.** `ge_res` skeleton;
   textures cooked (PNG → RGBA blob) and loaded by `ge_gfx` from blob in
   the sandbox. Proves the Layer 2 contract.
2. **Build graph + incremental + parallel.** Hashing, DAG, dirty diffing,
   job fan-out; idempotency + incrementality tests green.
3. **Full importer coverage.** Meshes/skins/clips (glTF), audio, fonts;
   sandbox boots entirely from cooked content (dev-time direct load kept
   behind a CVar/flag).
4. **IBL prefilter bake.** The Phase 3 carry-over: bake + `ge_gfx`
   runtime consumption; golden-image update for the reference scene
   (diff reviewed deliberately, per the golden rules).
5. **Validation + reload hook + sign-off.** Corrupt-input suite, missing-
   dependency reporting, `reload()` entry point demonstrated manually;
   phase DoD: clean cook of the content folder, incremental no-op second
   run, sandbox plays from cooked data.

## Test plan
Per plan.md's Phase 11 critical-test line:
- **Unit/integration (idempotency):** cook twice with no changes — second
  run cooks zero assets and the manifest is byte-identical.
- **Unit/integration (incrementality):** touch one source byte — exactly
  that asset and its dependents recook (asserted from the cook report);
  clock changes alone trigger nothing.
- **Unit (GUID/reference resolution):** rename a source (with its
  `.meta`) — same GUID, references still resolve; a dangling reference is
  reported, not crashed on.
- **Unit (blob):** header round-trip; version mismatch and payload-hash
  mismatch rejected gracefully.
- **Unit (corrupt input):** truncated PNG, garbage glTF, zero-byte WAV —
  importer returns an error, cook run reports and survives.
- **Unit (IBL):** BRDF LUT spot values against known analytic points; a
  constant-color environment prefilters to that color at every roughness
  (energy sanity).
- **Baseline/golden:** existing gfx golden images still pass with cooked
  content feeding the renderer; IBL golden updated deliberately.
- **Smoke:** sandbox boots from a freshly cooked folder, N frames, clean
  exit.

## Open questions
- Single packed archive vs loose cooked files: start loose (simplest,
  hot-reload-friendly); pack becomes a Phase 14 packaging concern.
- Import settings UX (per-asset options like "sRGB?") — `.meta` fields
  edited by hand until the Phase 13 editor exposes them.
- Whether `ge_anim`/`ge_gfx`'s dual-parse of one .glb (noted in Phase 6)
  collapses here into one importer emitting two blobs — intended yes;
  confirm the split when writing the mesh/skin importer.

## Cross-phase watch items
- Phase 12's file watcher calls `AssetCache::reload`; Phase 13's asset
  browser reads the manifest and `.meta` settings; Phase 14's packaging
  packs the cooked folder. All three consume, none redesign.
- Texture BCn compression + full mip chains: first real cook-time-vs-
  quality tradeoff; deferred until GPU memory or bandwidth measurably
  hurts.

## Depends on
- Phase 2 (job system, serialization, FNV hashing, HandleTable, file I/O).
- Phases 3/6/7/8 (the subsystem import code the cooker reuses and the
  `from_blob` consumers).
- Phase 3 Addendum 2 (the IBL gap this phase closes).
